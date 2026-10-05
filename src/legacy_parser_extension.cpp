#define DUCKDB_EXTENSION_MAIN

#include "legacy_parser_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception/parser_exception.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/settings.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parser_extension.hpp"
#include "duckdb/parser/statement/create_statement.hpp"
#include "duckdb/parser/transformer.hpp"
#include "postgres_parser.hpp"

namespace duckdb {

struct LegacyParserInfo : public ParserExtensionInfo {
	explicit LegacyParserInfo(DatabaseInstance &db_p) : db(db_p) {
	}
	DatabaseInstance &db;
};

static constexpr const char *ENABLE_OPTION = "enable_legacy_parser";

//! The extension's opt-in: unless set, every query is declined and the built-in parser handles it
static bool LegacyParserEnabled(DatabaseInstance &db) {
	Value value;
	auto &config = DBConfig::GetConfig(db);
	if (!config.TryGetCurrentSetting(Identifier(ENABLE_OPTION), value)) {
		return false;
	}
	return !value.IsNull() && BooleanValue::Get(value);
}

//! Parses a query with the 1.5 Postgres-derived grammar and transformer.
//! Returns DISPLAY_ORIGINAL_ERROR when the grammar rejects the query, so that the PEG parser takes over.
static ParserOverrideResult LegacyParse(ParserExtensionInfo *info, const string &query_p, ParserOptions &options) {
	if (!LegacyParserEnabled(info->Cast<LegacyParserInfo>().db)) {
		return ParserOverrideResult();
	}
	string query = query_p;
	{
		string stripped;
		if (Parser::StripUnicodeSpaces(query, stripped)) {
			query = std::move(stripped);
		}
	}
	// the 1.5 grammar can lowercase or preserve; the 2.0 uppercase mode is treated as preserve
	PostgresParser::SetPreserveIdentifierCase(options.identifier_case_mode != IdentifierCaseMode::LOWERCASE);

	vector<unique_ptr<SQLStatement>> statements;
	string error_message;
	optional_idx error_location;
	{
		PostgresParser parser;
		parser.Parse(query);
		if (!parser.success) {
			error_message = parser.error_message;
			if (parser.error_location > 0) {
				error_location = NumericCast<idx_t>(parser.error_location - 1);
			}
		} else if (parser.parse_tree) {
			Transformer transformer(options);
			transformer.TransformParseTree(parser.parse_tree, statements);
		}
	}
	if (!error_message.empty()) {
		if (options.parser_override_setting == AllowParserOverride::STRICT_OVERRIDE) {
			auto exception = ParserException::SyntaxError(query, error_message, error_location);
			return ParserOverrideResult(exception);
		}
		return ParserOverrideResult();
	}
	if (!statements.empty()) {
		for (idx_t i = 0; i + 1 < statements.size(); i++) {
			auto start = statements[i]->stmt_location.offset;
			statements[i]->stmt_location = QueryLocation(start, statements[i + 1]->stmt_location.offset - start);
		}
		auto last_start = statements.back()->stmt_location.offset;
		statements.back()->stmt_location = QueryLocation(last_start, query.size() - last_start);
		for (auto &statement : statements) {
			statement->query = query.substr(statement->stmt_location.offset, statement->stmt_location.length);
			statement->stmt_location = QueryLocation(0, statement->query.size());
			if (statement->type == StatementType::CREATE_STATEMENT) {
				auto &create = statement->Cast<CreateStatement>();
				create.info->sql = statement->query;
			}
		}
	}
	return ParserOverrideResult(std::move(statements));
}

class LegacyParserExtensionHook : public ParserExtension {
public:
	explicit LegacyParserExtensionHook(DatabaseInstance &db) {
		parser_override = LegacyParse;
		parser_info = make_shared_ptr<LegacyParserInfo>(db);
	}
};

//! Enabling moves the core setting off its default, where no parser override is consulted: "parse like 1.5" means
//! the legacy grammar is the parser, without PEG fallback. An override mode that was chosen explicitly is kept.
static void SetEnableLegacyParser(ClientContext &context, SetScope scope, Value &parameter) {
	if (parameter.IsNull() || !BooleanValue::Get(parameter)) {
		return;
	}
	auto &db = DatabaseInstance::GetDatabase(context);
	if (Settings::Get<AllowParserOverrideExtensionSetting>(db) == AllowParserOverride::DEFAULT_OVERRIDE) {
		DBConfig::GetConfig(db).SetOptionByName(Identifier("allow_parser_override_extension"), Value("strict"));
	}
}

static void LoadInternal(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(db);
	ParserExtension::Register(config, LegacyParserExtensionHook(db));
	config.AddExtensionOption(Identifier(ENABLE_OPTION),
	                          "Makes the 1.5 grammar the parser; when unset every query goes to the built-in parser",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false), SetEnableLegacyParser, SetScope::GLOBAL);
}

void LegacyParserExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string LegacyParserExtension::Name() {
	return "legacy_parser";
}

std::string LegacyParserExtension::Version() const {
#ifdef EXT_VERSION_LEGACY_PARSER
	return EXT_VERSION_LEGACY_PARSER;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(legacy_parser, loader) {
	duckdb::LoadInternal(loader);
}
}
