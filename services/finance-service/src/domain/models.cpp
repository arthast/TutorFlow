#include "domain/models.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::finance {
namespace {
namespace common = userver::formats::common;
using userver::formats::json::ValueBuilder;

template <typename T>
void SetNullable(ValueBuilder &json, std::string_view key,
                 const std::optional<T> &value) {
  if (value.has_value()) {
    json[std::string{key}] = *value;
  } else {
    json[std::string{key}] = nullptr;
  }
}

} // namespace

userver::formats::json::Value ToJson(const Transaction &transaction) {
  ValueBuilder json(common::Type::kObject);
  json["id"] = transaction.id;
  json["teacher_id"] = transaction.teacher_id;
  json["student_id"] = transaction.student_id;
  json["type"] = transaction.type;
  json["amount"] = transaction.amount;
  json["currency"] = transaction.currency;
  SetNullable(json, "lesson_id", transaction.lesson_id);
  SetNullable(json, "receipt_id", transaction.receipt_id);
  SetNullable(json, "comment", transaction.comment);
  json["created_at"] = transaction.created_at;
  return json.ExtractValue();
}

} // namespace tutorflow::finance
