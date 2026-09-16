#include "duckdb/parser/statement/drop_statement.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/parser/transformer.hpp"
#include "duckdb/parser/parsed_data/extra_drop_info.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"

namespace duckdb {

unique_ptr<ExtraDropInfo> Transformer::TransformDropTrigger(duckdb_libpgquery::PGList &obj_list,
                                                            Identifier &trigger_name_out) {
	// Grammar produces [catalog.][schema.]table, trigger_name as a flat list.
	vector<string> parts;
	for (auto *cell = obj_list.head; cell; cell = cell->next) {
		parts.push_back(PGPointerCast<duckdb_libpgquery::PGValue>(cell->data.ptr_value)->val.str);
	}
	// parts: [table] | [schema, table] | [catalog, schema, table], then trigger_name last
	if (parts.size() < 2 || parts.size() > 4) {
		throw ParserException("Expected \"DROP TRIGGER name ON [catalog.][schema.]table\"");
	}
	trigger_name_out = Identifier(parts.back());
	if (trigger_name_out.empty()) {
		throw ParserException("Trigger name cannot be empty");
	}
	auto base_table = make_uniq<BaseTableRef>();
	auto table_name = Identifier(parts[parts.size() - 2]);
	auto schema_name = parts.size() >= 3 ? Identifier(parts[parts.size() - 3]) : Identifier();
	auto catalog_name = parts.size() == 4 ? Identifier(parts[0]) : Identifier();
	base_table->SetQualifiedName(std::move(catalog_name), std::move(schema_name), std::move(table_name));
	auto extra_info = make_uniq<ExtraDropTriggerInfo>();
	extra_info->base_table = std::move(base_table);
	return extra_info;
}

unique_ptr<SQLStatement> Transformer::TransformDrop(duckdb_libpgquery::PGDropStmt &stmt) {
	auto result = make_uniq<DropStatement>();
	auto &info = *result->info.get();
	if (stmt.objects->length != 1) {
		throw NotImplementedException("Can only drop one object at a time");
	}
	switch (stmt.removeType) {
	case duckdb_libpgquery::PG_OBJECT_TABLE:
		info.type = CatalogType::TABLE_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_SCHEMA:
		info.type = CatalogType::SCHEMA_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_INDEX:
		info.type = CatalogType::INDEX_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_VIEW:
		info.type = CatalogType::VIEW_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_SEQUENCE:
		info.type = CatalogType::SEQUENCE_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_FUNCTION:
		info.type = CatalogType::MACRO_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_TABLE_MACRO:
		info.type = CatalogType::TABLE_MACRO_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_TYPE:
		info.type = CatalogType::TYPE_ENTRY;
		break;
	case duckdb_libpgquery::PG_OBJECT_TRIGGER:
		info.type = CatalogType::TRIGGER_ENTRY;
		break;
	default:
		throw NotImplementedException("Cannot drop this type yet");
	}

	switch (stmt.removeType) {
	case duckdb_libpgquery::PG_OBJECT_SCHEMA: {
		// the dotted path as written ([schema] / [catalog, schema]); the binder resolves the leading component
		auto view_list = PGPointerCast<duckdb_libpgquery::PGList>(stmt.objects->head->data.ptr_value);
		if (view_list->length < 1 || view_list->length > 2) {
			throw ParserException("Expected \"catalog.schema\" or \"schema\"");
		}
		vector<Identifier> schema_path;
		for (auto cell = view_list->head; cell != view_list->tail; cell = cell->next) {
			schema_path.emplace_back(PGPointerCast<duckdb_libpgquery::PGValue>(cell->data.ptr_value)->val.str);
		}
		Identifier schema_name(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->tail->data.ptr_value)->val.str);
		info.SetQualifiedName(QualifiedName(std::move(schema_path), std::move(schema_name)));
		break;
	}
	case duckdb_libpgquery::PG_OBJECT_TRIGGER: {
		auto &obj_list = *PGPointerCast<duckdb_libpgquery::PGList>(stmt.objects->head->data.ptr_value);
		Identifier trigger_name;
		info.extra_drop_info = TransformDropTrigger(obj_list, trigger_name);
		info.SetName(std::move(trigger_name));
		break;
	}
	default: {
		auto view_list = PGPointerCast<duckdb_libpgquery::PGList>(stmt.objects->head->data.ptr_value);
		if (view_list->length == 3) {
			info.SetCatalog(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->head->data.ptr_value)->val.str);
			info.SetSchema(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->head->next->data.ptr_value)->val.str);
			info.SetName(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->head->next->next->data.ptr_value)->val.str);
		} else if (view_list->length == 2) {
			info.SetSchema(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->head->data.ptr_value)->val.str);
			info.SetName(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->head->next->data.ptr_value)->val.str);
		} else if (view_list->length == 1) {
			info.SetName(PGPointerCast<duckdb_libpgquery::PGValue>(view_list->head->data.ptr_value)->val.str);
		} else {
			throw ParserException("Expected \"catalog.schema.name\", \"schema.name\"or \"name\"");
		}
		break;
	}
	}
	info.cascade = stmt.behavior == duckdb_libpgquery::PGDropBehavior::PG_DROP_CASCADE;
	info.if_not_found = TransformOnEntryNotFound(stmt.missing_ok);
	return std::move(result);
}

unique_ptr<DropStatement> Transformer::TransformDropSecret(duckdb_libpgquery::PGDropSecretStmt &stmt) {
	auto result = make_uniq<DropStatement>();
	auto info = make_uniq<DropInfo>();
	auto extra_info = make_uniq<ExtraDropSecretInfo>();

	info->type = CatalogType::SECRET_ENTRY;
	info->SetName(stmt.secret_name);
	info->if_not_found = stmt.missing_ok ? OnEntryNotFound::RETURN_NULL : OnEntryNotFound::THROW_EXCEPTION;

	extra_info->persist_mode = EnumUtil::FromString<SecretPersistType>(StringUtil::Upper(stmt.persist_type));
	extra_info->secret_storage = stmt.secret_storage;

	if (extra_info->persist_mode == SecretPersistType::TEMPORARY) {
		if (!extra_info->secret_storage.empty()) {
			throw ParserException("Can not combine TEMPORARY with specifying a storage for drop secret");
		}
	}

	info->extra_drop_info = std::move(extra_info);
	result->info = std::move(info);

	return result;
}

} // namespace duckdb
