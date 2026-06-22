#include "domain/models.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::assignment {
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

userver::formats::json::Value ToJsonStringArray(
    const std::vector<std::string> &items) {
  ValueBuilder array(common::Type::kArray);
  for (const auto &item : items) {
    array.PushBack(item);
  }
  return array.ExtractValue();
}

template <typename T>
userver::formats::json::Value ToJsonArray(const std::vector<T> &items) {
  ValueBuilder array(common::Type::kArray);
  for (const auto &item : items) {
    array.PushBack(ToJson(item));
  }
  return array.ExtractValue();
}

} // namespace

userver::formats::json::Value ToJson(const Assignment &assignment) {
  ValueBuilder json(common::Type::kObject);
  json["id"] = assignment.id;
  json["teacher_id"] = assignment.teacher_id;
  json["student_id"] = assignment.student_id;
  json["title"] = assignment.title;
  SetNullable(json, "description", assignment.description);
  SetNullable(json, "due_at", assignment.due_at);
  json["status"] = assignment.status;
  json["created_at"] = assignment.created_at;
  return json.ExtractValue();
}

userver::formats::json::Value ToJson(const Submission &submission) {
  ValueBuilder json(common::Type::kObject);
  json["id"] = submission.id;
  json["assignment_id"] = submission.assignment_id;
  json["student_id"] = submission.student_id;
  SetNullable(json, "text_answer", submission.text_answer);
  json["status"] = submission.status;
  json["submitted_at"] = submission.submitted_at;
  json["file_ids"] = ToJsonStringArray(submission.file_ids);
  return json.ExtractValue();
}

userver::formats::json::Value ToJson(const Comment &comment) {
  ValueBuilder json(common::Type::kObject);
  json["id"] = comment.id;
  json["assignment_id"] = comment.assignment_id;
  json["author_id"] = comment.author_id;
  json["text"] = comment.text;
  json["created_at"] = comment.created_at;
  return json.ExtractValue();
}

userver::formats::json::Value ToJson(const AssignmentDetail &detail) {
  ValueBuilder json(ToJson(detail.assignment));
  json["file_ids"] = ToJsonStringArray(detail.file_ids);
  json["submissions"] = ToJsonArray(detail.submissions);
  json["comments"] = ToJsonArray(detail.comments);
  return json.ExtractValue();
}

} // namespace tutorflow::assignment
