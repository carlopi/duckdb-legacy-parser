#include "duckdb/parser/statement/alter_statement.hpp"
#include "duckdb/parser/transformer.hpp"

namespace duckdb {

unique_ptr<AlterStatement> Transformer::TransformRename(duckdb_libpgquery::PGRenameStmt &stmt) {
	if (!stmt.relation) {
		throw NotImplementedException("Altering schemas is not yet supported");
	}

	unique_ptr<AlterInfo> info;

	auto catalog = stmt.relation->catalogname ? Identifier(stmt.relation->catalogname) : Identifier::InvalidCatalog();
	auto schema = stmt.relation->schemaname ? Identifier(stmt.relation->schemaname) : Identifier::InvalidSchema();
	auto name = stmt.relation->relname ? Identifier(stmt.relation->relname) : Identifier();
	AlterEntryData data(QualifiedName(std::move(catalog), std::move(schema), std::move(name)),
	                    TransformOnEntryNotFound(stmt.missing_ok));
	// first we check the type of ALTER
	switch (stmt.renameType) {
	case duckdb_libpgquery::PG_OBJECT_COLUMN: {
		// change column name

		// get the old name and the new name
		auto names = TransformNameList(*stmt.name_list);
		string new_name = stmt.newname;
		if (names.size() == 1) {
			info = make_uniq<RenameColumnInfo>(std::move(data), std::move(names[0]), Identifier(std::move(new_name)));
		} else {
			info = make_uniq<RenameFieldInfo>(std::move(data), std::move(names), Identifier(std::move(new_name)));
		}
		break;
	}
	case duckdb_libpgquery::PG_OBJECT_TABLE: {
		// change table name
		string new_name = stmt.newname;
		info = make_uniq<RenameTableInfo>(std::move(data), Identifier(new_name));
		break;
	}
	case duckdb_libpgquery::PG_OBJECT_VIEW: {
		// change view name
		string new_name = stmt.newname;
		info = make_uniq<RenameViewInfo>(std::move(data), Identifier(new_name));
		break;
	}
	case duckdb_libpgquery::PG_OBJECT_DATABASE:
	default:
		throw NotImplementedException("Schema element not supported yet!");
	}
	D_ASSERT(info);

	auto result = make_uniq<AlterStatement>();
	result->info = std::move(info);
	return result;
}

} // namespace duckdb
