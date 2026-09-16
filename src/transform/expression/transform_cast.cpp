#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/transformer.hpp"
#include "duckdb/function/cast/default_casts.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/common/types/blob.hpp"

namespace duckdb {

unique_ptr<ParsedExpression> Transformer::TransformTypeCast(duckdb_libpgquery::PGTypeCast &root) {
	// get the type to cast to
	auto type_name = root.typeName;
	LogicalType target_type = TransformTypeName(*type_name);

	// check for a constant BLOB value, then return ConstantExpression with BLOB
	if (!root.tryCast && target_type == LogicalType::BLOB && root.arg->type == duckdb_libpgquery::T_PGAConst) {
		auto c = PGPointerCast<duckdb_libpgquery::PGAConst>(root.arg);
		if (c->val.type == duckdb_libpgquery::T_PGString) {
			CastParameters parameters;
			if (root.location >= 0) {
				parameters.query_location = QueryLocation(optional_idx(NumericCast<idx_t>(root.location)));
			}
			auto blob_data = Blob::ToBlob(string(c->val.val.str), parameters);
			auto result = ConstantExpression::FromValue(Value::BLOB_RAW(blob_data));
			SetQueryLocation(*result, root.location);
			return std::move(result);
		}
	}
	// TRUE and FALSE arrive as a synthesized cast of 't' / 'f' to bool (makeBoolAConst); emit the boolean literal
	// so that the binder sees a constant, as it does with the PEG parser
	if (!root.tryCast && root.location == -1 && root.arg->type == duckdb_libpgquery::T_PGAConst && type_name->names &&
	    type_name->names->tail) {
		auto &last_name = *PGPointerCast<duckdb_libpgquery::PGValue>(type_name->names->tail->data.ptr_value);
		const bool is_bool = last_name.type == duckdb_libpgquery::T_PGString && last_name.val.str &&
		                     (StringUtil::CIEquals(last_name.val.str, "bool") || StringUtil::CIEquals(last_name.val.str, "boolean"));
		auto &bool_const = PGCast<duckdb_libpgquery::PGAConst>(*root.arg);
		if (is_bool && bool_const.val.type == duckdb_libpgquery::T_PGString && bool_const.val.val.str) {
			string text(bool_const.val.val.str);
			if (text == "t" || text == "f") {
				auto literal = ConstantExpression::Boolean(text == "t");
				SetQueryLocation(*literal, bool_const.location);
				return std::move(literal);
			}
		}
	}
	// transform the expression node
	auto expression = TransformExpression(root.arg);
	bool try_cast = root.tryCast;

	// now create a cast operation
	auto result = make_uniq<CastExpression>(target_type, std::move(expression), try_cast);
	SetQueryLocation(*result, root.location);
	return std::move(result);
}

} // namespace duckdb
