#include "domain/models.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::lesson {
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

userver::formats::json::Value ToJson(const Slot &slot) {
  ValueBuilder json(common::Type::kObject);
  json["id"] = slot.id;
  json["teacher_id"] = slot.teacher_id;
  json["starts_at"] = slot.starts_at;
  json["ends_at"] = slot.ends_at;
  json["status"] = slot.status;
  json["created_at"] = slot.created_at;
  return json.ExtractValue();
}

userver::formats::json::Value ToJson(const Lesson &lesson) {
  ValueBuilder json(common::Type::kObject);
  json["id"] = lesson.id;
  json["teacher_id"] = lesson.teacher_id;
  json["student_id"] = lesson.student_id;
  SetNullable(json, "slot_id", lesson.slot_id);
  json["starts_at"] = lesson.starts_at;
  json["ends_at"] = lesson.ends_at;
  json["status"] = lesson.status;
  SetNullable(json, "topic", lesson.topic);
  SetNullable(json, "notes", lesson.notes);
  json["price"] = lesson.price;
  json["created_at"] = lesson.created_at;
  SetNullable(json, "completed_at", lesson.completed_at);
  return json.ExtractValue();
}

} // namespace tutorflow::lesson
