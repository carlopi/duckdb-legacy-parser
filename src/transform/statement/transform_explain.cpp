#include "duckdb/parser/statement/explain_statement.hpp"
#include "duckdb/main/profiler/profiler_print_format.hpp"
#include "duckdb/parser/transformer.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"

namespace duckdb {

static ProfilerPrintFormat ParseFormat(const Value &val) {
	if (val.type().id() != LogicalTypeId::VARCHAR) {
		throw InvalidInputException("Expected a string as argument to FORMAT");
	}
	return ProfilerPrintFormat(StringUtil::Lower(val.GetValue<string>()));
}

unique_ptr<ExplainStatement> Transformer::TransformExplain(duckdb_libpgquery::PGExplainStmt &stmt) {
	auto explain_type = ExplainType::EXPLAIN_STANDARD;
	auto explain_format = ProfilerPrintFormat::Default();
	bool format_is_set = false;
	if (stmt.options) {
		for (auto n = stmt.options->head; n; n = n->next) {
			auto def_elem = PGPointerCast<duckdb_libpgquery::PGDefElem>(n->data.ptr_value);
			auto def_name = def_elem->defname;
			auto elem = StringUtil::Lower(def_name);
			if (elem == "analyze") {
				explain_type = ExplainType::EXPLAIN_ANALYZE;
			} else if (elem == "format") {
				if (def_elem->arg) {
					if (format_is_set) {
						throw InvalidInputException("FORMAT can not be provided more than once");
					}
					auto &pg_value = *PGPointerCast<duckdb_libpgquery::PGValue>(def_elem->arg);
					if (pg_value.type != duckdb_libpgquery::T_PGString) {
						throw InvalidInputException("Expected a string as argument to FORMAT");
					}
					Value val(string(pg_value.val.str));
					format_is_set = true;
					explain_format = ParseFormat(val);
				}
			} else {
				throw NotImplementedException("Unimplemented explain type: %s", elem);
			}
		}
	}
	return make_uniq<ExplainStatement>(TransformStatement(*stmt.query), explain_type, explain_format);
}

} // namespace duckdb
