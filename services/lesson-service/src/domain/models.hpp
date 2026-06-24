#pragma once

#include <optional>
#include <string>
#include <vector>

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
  std::vector<std::string> file_ids;
  std::string created_at;
  std::optional<std::string> completed_at;
};

struct CompleteLessonOutcome {
  Lesson lesson;
  std::string charge_status;
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
  std::vector<std::string> file_ids;
};

} // namespace tutorflow::lesson
