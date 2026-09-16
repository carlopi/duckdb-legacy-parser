#include "duckdb/parser/transformer.hpp"
#include "duckdb/parser/statement/set_statement.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"

namespace duckdb {

unique_ptr<SetStatement> Transformer::TransformUse(duckdb_libpgquery::PGUseStmt &stmt) {
	auto qualified_name = TransformQualifiedName(*stmt.name);
	if (!IsInvalidCatalog(qualified_name.Catalog())) {
		throw ParserException("Expected \"USE database\" or \"USE database.schema\"");
	}
	string name;
	if (IsInvalidSchema(qualified_name.Schema())) {
		name = KeywordHelper::WriteOptionallyQuoted(qualified_name.Name().GetIdentifierName(), '"');
	} else {
		name = KeywordHelper::WriteOptionallyQuoted(qualified_name.Schema().GetIdentifierName(), '"') + "." +
		       KeywordHelper::WriteOptionallyQuoted(qualified_name.Name().GetIdentifierName(), '"');
	}
	auto name_expr = ConstantExpression::FromValue(Value(name));
	return make_uniq<SetVariableStatement>("schema", std::move(name_expr), SetScope::AUTOMATIC);
}

} // namespace duckdb
