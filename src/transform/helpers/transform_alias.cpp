#include "duckdb/parser/transformer.hpp"

namespace duckdb {

vector<Identifier> Transformer::TransformStringList(duckdb_libpgquery::PGList *list) {
	vector<Identifier> result;
	if (!list) {
		return result;
	}
	for (auto node = list->head; node != nullptr; node = node->next) {
		auto value = PGPointerCast<duckdb_libpgquery::PGValue>(node->data.ptr_value);
		result.emplace_back(value->val.str);
	}
	return result;
}

Identifier Transformer::TransformAlias(duckdb_libpgquery::PGAlias *root, vector<Identifier> &column_name_alias) {
	if (!root) {
		return Identifier();
	}
	column_name_alias = TransformStringList(root->colnames);
	return Identifier(root->aliasname);
}

} // namespace duckdb
