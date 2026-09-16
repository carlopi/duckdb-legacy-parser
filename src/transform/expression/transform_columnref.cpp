#include "duckdb/common/exception.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/star_expression.hpp"
#include "duckdb/parser/transformer.hpp"

namespace duckdb {

QualifiedColumnName TransformQualifiedColumnName(duckdb_libpgquery::PGList &list) {
	QualifiedColumnName result;
	switch (list.length) {
	case 1:
		result.column = const_char_ptr_cast(list.head->data.ptr_value);
		break;
	case 2:
		result.table = const_char_ptr_cast(list.head->data.ptr_value);
		result.column = const_char_ptr_cast(list.head->next->data.ptr_value);
		break;
	case 3:
		result.schema = const_char_ptr_cast(list.head->data.ptr_value);
		result.table = const_char_ptr_cast(list.head->next->data.ptr_value);
		result.column = const_char_ptr_cast(list.head->next->next->data.ptr_value);
		break;
	case 4:
		result.catalog = const_char_ptr_cast(list.head->data.ptr_value);
		result.schema = const_char_ptr_cast(list.head->next->data.ptr_value);
		result.table = const_char_ptr_cast(list.head->next->next->data.ptr_value);
		result.column = const_char_ptr_cast(list.head->next->next->next->data.ptr_value);
		break;
	default:
		throw ParserException("Qualified column name must have between 1 and 4 elements");
	}
	return result;
}

unique_ptr<ParsedExpression> Transformer::TransformStarExpression(duckdb_libpgquery::PGAStar &star) {
	auto result = make_uniq<StarExpression>(Identifier(star.relation ? star.relation : string()));
	if (star.except_list) {
		for (auto head = star.except_list->head; head; head = head->next) {
			auto exclude_column_list = PGPointerCast<duckdb_libpgquery::PGList>(head->data.ptr_value);
			auto exclude_column = TransformQualifiedColumnName(*exclude_column_list);
			// qualified - add to exclude list
			if (result->ExcludeListMutable().find(exclude_column) != result->ExcludeListMutable().end()) {
				throw ParserException("Duplicate entry \"%s\" in EXCLUDE list", exclude_column.ToString());
			}
			result->ExcludeListMutable().insert(std::move(exclude_column));
		}
	}
	if (star.replace_list) {
		for (auto head = star.replace_list->head; head; head = head->next) {
			auto list = PGPointerCast<duckdb_libpgquery::PGList>(head->data.ptr_value);
			D_ASSERT(list->length == 2);
			auto replace_expression =
			    TransformExpression(PGPointerCast<duckdb_libpgquery::PGNode>(list->head->data.ptr_value));
			auto value = PGPointerCast<duckdb_libpgquery::PGValue>(list->tail->data.ptr_value);
			D_ASSERT(value->type == duckdb_libpgquery::T_PGString);
			Identifier replace_entry(value->val.str);
			if (result->ReplaceListMutable().find(replace_entry) != result->ReplaceListMutable().end()) {
				throw ParserException("Duplicate entry \"%s\" in REPLACE list", replace_entry);
			}
			if (result->ExcludeListMutable().find(QualifiedColumnName(Identifier(), replace_entry)) != result->ExcludeListMutable().end()) {
				throw ParserException("Column \"%s\" cannot occur in both EXCLUDE and REPLACE list", replace_entry);
			}
			result->ReplaceListMutable().insert(make_pair(std::move(replace_entry), std::move(replace_expression)));
		}
	}
	if (star.rename_list) {
		for (auto head = star.rename_list->head; head; head = head->next) {
			auto list = PGPointerCast<duckdb_libpgquery::PGList>(head->data.ptr_value);
			D_ASSERT(list->length == 2);
			auto rename_column_list = PGPointerCast<duckdb_libpgquery::PGList>(list->head->data.ptr_value);
			auto rename_column = TransformQualifiedColumnName(*rename_column_list);
			string new_name = char_ptr_cast(list->tail->data.ptr_value);
			if (result->RenameListMutable().find(rename_column) != result->RenameListMutable().end()) {
				throw ParserException("Duplicate entry \"%s\" in EXCLUDE list", rename_column.ToString());
			}
			if (result->ExcludeListMutable().find(rename_column) != result->ExcludeListMutable().end()) {
				throw ParserException("Column \"%s\" cannot occur in both EXCLUDE and RENAME list",
				                      rename_column.ToString());
			}
			if (result->ReplaceListMutable().find(rename_column.column) != result->ReplaceListMutable().end()) {
				throw ParserException("Column \"%s\" cannot occur in both REPLACE and RENAME list",
				                      rename_column.ToString());
			}
			result->RenameListMutable().insert(make_pair(std::move(rename_column), std::move(new_name)));
		}
	}
	if (star.expr) {
		D_ASSERT(star.columns);
		D_ASSERT(result->RelationName().empty());
		D_ASSERT(result->ExcludeList().empty());
		D_ASSERT(result->ReplaceList().empty());
		result->ExpressionMutable() = TransformExpression(star.expr);
		if (StarExpression::IsStar(*result->ExpressionMutable())) {
			auto &child_star = result->ExpressionMutable()->Cast<StarExpression>();
			result->RelationNameMutable() = child_star.RelationNameMutable();
			result->ExcludeListMutable() = std::move(child_star.ExcludeListMutable());
			result->ReplaceListMutable() = std::move(child_star.ReplaceListMutable());
			result->RenameListMutable() = std::move(child_star.RenameListMutable());
			result->ExpressionMutable().reset();
		} else if (result->ExpressionMutable()->GetExpressionType() == ExpressionType::LAMBDA) {
			vector<unique_ptr<ParsedExpression>> children;
			children.push_back(make_uniq<StarExpression>());
			children.push_back(std::move(result->ExpressionMutable()));
			auto list_filter = make_uniq<FunctionExpression>("list_filter", std::move(children));
			result->ExpressionMutable() = std::move(list_filter);
		}
	}
	result->IsColumnsMutable() = star.columns;
	SetQueryLocation(*result, star.location);
	return std::move(result);
}

unique_ptr<ParsedExpression> Transformer::TransformColumnRef(duckdb_libpgquery::PGColumnRef &root) {
	auto fields = root.fields;
	auto head_node = PGPointerCast<duckdb_libpgquery::PGNode>(fields->head->data.ptr_value);
	switch (head_node->type) {
	case duckdb_libpgquery::T_PGString: {
		if (fields->length < 1) {
			throw InternalException("Unexpected field length");
		}
		vector<Identifier> column_names;
		for (auto node = fields->head; node; node = node->next) {
			column_names.emplace_back(PGPointerCast<duckdb_libpgquery::PGValue>(node->data.ptr_value)->val.str);
		}
		auto colref = make_uniq<ColumnRefExpression>(std::move(column_names));
		SetQueryLocation(*colref, root.location);
		return std::move(colref);
	}
	case duckdb_libpgquery::T_PGAStar: {
		return TransformStarExpression(PGCast<duckdb_libpgquery::PGAStar>(*head_node));
	}
	default:
		throw NotImplementedException("ColumnRef not implemented!");
	}
}

} // namespace duckdb
