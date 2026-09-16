#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/transformer.hpp"

namespace duckdb {

unique_ptr<TableRef> Transformer::TransformRangeVar(duckdb_libpgquery::PGRangeVar &root) {
	auto result = make_uniq<BaseTableRef>();

	result->alias = TransformAlias(root.alias, result->column_name_alias);
	result->SetQualifiedName(TransformQualifiedName(root));
	if (root.at_clause) {
		auto &at_clause = PGCast<duckdb_libpgquery::PGAtClause>(*root.at_clause);
		auto at_expr = TransformExpression(*at_clause.expr);
		result->at_clause = make_uniq<AtClause>(at_clause.unit, std::move(at_expr));
	}
	if (root.sample) {
		result->sample = TransformSampleOptions(root.sample);
	}
	SetQueryLocation(*result, root.location);
	return std::move(result);
}

QualifiedName Transformer::TransformQualifiedName(duckdb_libpgquery::PGRangeVar &root) {
	if (!root.relname) {
		throw ParserException("Empty table name not supported");
	}
	auto catalog = root.catalogname ? Identifier(root.catalogname) : Identifier::InvalidCatalog();
	auto schema = root.schemaname ? Identifier(root.schemaname) : Identifier::InvalidSchema();
	return QualifiedName(std::move(catalog), std::move(schema), Identifier(root.relname));
}

} // namespace duckdb
