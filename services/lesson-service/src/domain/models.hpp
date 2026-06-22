#pragma once

#include <optional>
#include <string>

#include <userver/formats/json/value.hpp>

namespace tutorflow::lesson {

struct Slot {
  std::string id;
  std::string teacher_id;
  std::string starts_at;
  std::string ends_at;
  std::string status;
  std::string created_at;
};

struct Lesson {
  std::string id;
  std::string teacher_id;
  std::string student_id;
  std::optional<std::string> slot_id;
  std::string starts_at;
  std::string ends_at;
  std::string status;
  std::optional<std::string> topic;
  std::optional<std::string> notes;
  double price{};
  std::string created_at;
  std::optional<std::string> completed_at;
};

struct CreateSlotRequest {
  std::string starts_at;
  std::string ends_at;
};

struct CreateLessonRequest {
  std::string student_id;
  std::optional<std::string> slot_id;
  std::string starts_at;
  std::string ends_at;
  std::optional<std::string> topic;
  std::optional<std::string> notes;
  std::optional<double> price;
};

userver::formats::json::Value ToJson(const Slot &slot);
userver::formats::json::Value ToJson(const Lesson &lesson);

} // namespace tutorflow::lesson
