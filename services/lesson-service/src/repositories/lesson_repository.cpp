#include "repositories/lesson_repository.hpp"

#include <optional>

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::lesson {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

constexpr std::string_view kSlotFields = R"(
  id::text,
  teacher_id::text,
  to_char(starts_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS starts_at,
  to_char(ends_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS ends_at,
  status,
  to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)";

constexpr std::string_view kLessonFields = R"(
  id::text,
  teacher_id::text,
  student_id::text,
  COALESCE(slot_id::text, '') AS slot_id,
  to_char(starts_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS starts_at,
  to_char(ends_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS ends_at,
  status,
  COALESCE(topic, '') AS topic,
  COALESCE(notes, '') AS notes,
  price::double precision AS price,
  to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
  COALESCE(to_char(completed_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'), '') AS completed_at
)";

std::optional<std::string> EmptyToNull(std::string value) {
  if (value.empty())
    return std::nullopt;
  return value;
}

Slot RowToSlot(const pg::Row &row) {
  return Slot{
      row["id"].As<std::string>(),        row["teacher_id"].As<std::string>(),
      row["starts_at"].As<std::string>(), row["ends_at"].As<std::string>(),
      row["status"].As<std::string>(),    row["created_at"].As<std::string>(),
  };
}

Lesson RowToLesson(const pg::Row &row) {
  return Lesson{
      row["id"].As<std::string>(),
      row["teacher_id"].As<std::string>(),
      row["student_id"].As<std::string>(),
      EmptyToNull(row["slot_id"].As<std::string>()),
      row["starts_at"].As<std::string>(),
      row["ends_at"].As<std::string>(),
      row["status"].As<std::string>(),
      EmptyToNull(row["topic"].As<std::string>()),
      EmptyToNull(row["notes"].As<std::string>()),
      row["price"].As<double>(),
      row["created_at"].As<std::string>(),
      EmptyToNull(row["completed_at"].As<std::string>()),
  };
}

template <typename T>
std::vector<T> RowsToVector(const pg::ResultSet &result,
                            T (*mapper)(const pg::Row &)) {
  std::vector<T> items;
  items.reserve(result.Size());
  for (const auto &row : result) {
    items.push_back(mapper(row));
  }
  return items;
}

Lesson RequireSingleLesson(const pg::ResultSet &result,
                           std::string_view not_found_message) {
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::NotFound(
        std::string{not_found_message});
  }
  return RowToLesson(result[0]);
}

} // namespace

LessonRepository::LessonRepository(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("lesson-db")
              .GetCluster()) {}

Slot LessonRepository::CreateSlot(const std::string &teacher_id,
                                  const CreateSlotRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "INSERT INTO availability_slots (teacher_id, starts_at, ends_at) "
      "SELECT $1::uuid, $2::timestamptz, $3::timestamptz "
      "WHERE $2::timestamptz < $3::timestamptz "
      "RETURNING " +
          std::string{kSlotFields},
      teacher_id, request.starts_at, request.ends_at);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Validation(
        "starts_at must be earlier than ends_at");
  }
  return RowToSlot(result[0]);
}

std::vector<Slot>
LessonRepository::ListSlots(const std::string &teacher_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kSlotFields} +
                       " FROM availability_slots WHERE teacher_id = $1::uuid "
                       "ORDER BY starts_at, created_at",
                   teacher_id);
  return RowsToVector<Slot>(result, RowToSlot);
}

Lesson
LessonRepository::CreateLesson(const std::string &teacher_id,
                               const CreateLessonRequest &request) const {
  if (request.slot_id.has_value()) {
    const auto result = pg_->Execute(
        kMaster,
        "WITH booked_slot AS ("
        "  UPDATE availability_slots "
        "  SET status = 'booked' "
        "  WHERE id = $4::uuid AND teacher_id = $1::uuid AND status = 'open' "
        "  RETURNING id"
        "), inserted AS ("
        "  INSERT INTO lessons (teacher_id, student_id, slot_id, starts_at, "
        "                       ends_at, topic, notes, price) "
        "  SELECT $1::uuid, $2::uuid, id, $5::timestamptz, $6::timestamptz, "
        "         $7, $8, $3::numeric "
        "  FROM booked_slot "
        "  WHERE $5::timestamptz < $6::timestamptz "
        "  RETURNING " +
            std::string{kLessonFields} + ") SELECT * FROM inserted",
        teacher_id, request.student_id, *request.price, *request.slot_id,
        request.starts_at, request.ends_at, request.topic, request.notes);
    if (result.IsEmpty()) {
      throw tutorflow::common::ServiceError::Conflict(
          "slot is not open or does not belong to teacher");
    }
    return RowToLesson(result[0]);
  }

  const auto result = pg_->Execute(
      kMaster,
      "INSERT INTO lessons (teacher_id, student_id, starts_at, ends_at, topic, "
      "                     notes, price) "
      "SELECT $1::uuid, $2::uuid, $3::timestamptz, $4::timestamptz, $5, $6, "
      "       $7::numeric "
      "WHERE $3::timestamptz < $4::timestamptz "
      "RETURNING " +
          std::string{kLessonFields},
      teacher_id, request.student_id, request.starts_at, request.ends_at,
      request.topic, request.notes, *request.price);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Validation(
        "starts_at must be earlier than ends_at");
  }
  return RowToLesson(result[0]);
}

std::vector<Lesson>
LessonRepository::ListLessonsForTeacher(const std::string &teacher_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kLessonFields} +
                       " FROM lessons WHERE teacher_id = $1::uuid "
                       "ORDER BY starts_at, created_at",
                   teacher_id);
  return RowsToVector<Lesson>(result, RowToLesson);
}

std::vector<Lesson>
LessonRepository::ListLessonsForStudent(const std::string &student_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kLessonFields} +
                       " FROM lessons WHERE student_id = $1::uuid "
                       "ORDER BY starts_at, created_at",
                   student_id);
  return RowsToVector<Lesson>(result, RowToLesson);
}

std::optional<Lesson>
LessonRepository::FindLesson(const std::string &lesson_id) const {
  const auto result = pg_->Execute(kSlave,
                                   "SELECT " + std::string{kLessonFields} +
                                       " FROM lessons WHERE id = $1::uuid",
                                   lesson_id);
  if (result.IsEmpty())
    return std::nullopt;
  return RowToLesson(result[0]);
}

Lesson LessonRepository::CompleteLesson(const std::string &lesson_id,
                                        const std::string &teacher_id) const {
  const auto current = FindLesson(lesson_id);
  if (!current.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  if (current->teacher_id != teacher_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own lesson");
  }
  if (current->status == "completed")
    return *current;
  if (current->status == "cancelled") {
    throw tutorflow::common::ServiceError::Conflict(
        "cancelled lesson cannot be completed");
  }

  const auto result = pg_->Execute(
      kMaster,
      "UPDATE lessons SET status = 'completed', completed_at = now() "
      "WHERE id = $1::uuid AND teacher_id = $2::uuid AND status = 'scheduled' "
      "RETURNING " +
          std::string{kLessonFields},
      lesson_id, teacher_id);
  return RequireSingleLesson(result, "lesson not found");
}

Lesson LessonRepository::CancelLesson(const std::string &lesson_id,
                                      const std::string &teacher_id) const {
  const auto current = FindLesson(lesson_id);
  if (!current.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  if (current->teacher_id != teacher_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own lesson");
  }
  if (current->status == "cancelled")
    return *current;
  if (current->status == "completed") {
    throw tutorflow::common::ServiceError::Conflict(
        "completed lesson cannot be cancelled");
  }

  const auto result =
      pg_->Execute(kMaster,
                   "WITH cancelled AS ("
                   "  UPDATE lessons SET status = 'cancelled' "
                   "  WHERE id = $1::uuid AND teacher_id = $2::uuid AND status "
                   "= 'scheduled' "
                   "  RETURNING " +
                       std::string{kLessonFields} +
                       "), reopened AS ("
                       "  UPDATE availability_slots SET status = 'open' "
                       "  WHERE id IN (SELECT slot_id FROM cancelled WHERE "
                       "slot_id IS NOT NULL)"
                       ") SELECT * FROM cancelled",
                   lesson_id, teacher_id);
  return RequireSingleLesson(result, "lesson not found");
}

} // namespace tutorflow::lesson
