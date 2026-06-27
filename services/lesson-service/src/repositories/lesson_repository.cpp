#include "repositories/lesson_repository.hpp"

#include <optional>

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::lesson {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

// EXCLUDE-constraint из миграции 004: запрещает пересечение по времени двух
// scheduled-занятий одного преподавателя. Именно он атомарно закрывает гонку
// конкурентных create/reschedule (code-level проверки недостаточно).
constexpr std::string_view kNoOverlapConstraint = "no_overlap_teacher";

// Выполняет мутацию и переводит exclusion_violation (SQLSTATE 23P01) от
// no_overlap_teacher в 409 Conflict (единый envelope). Прочие нарушения
// пробрасываем как есть.
template <typename Fn>
auto WithOverlapGuard(Fn &&fn) -> decltype(fn()) {
  try {
    return fn();
  } catch (const pg::ExclusionViolation &e) {
    if (e.GetConstraint() == kNoOverlapConstraint) {
      throw tutorflow::common::ServiceError::Conflict(
          "lesson time overlaps another scheduled lesson");
    }
    throw;
  }
}

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
      {},
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

std::vector<std::string>
LoadLessonFileIds(const userver::storages::postgres::ClusterPtr &pg,
                  const std::string &lesson_id) {
  const auto result =
      pg->Execute(kSlave,
                  "SELECT file_id::text FROM lesson_files "
                  "WHERE lesson_id = $1::uuid ORDER BY created_at, id",
                  lesson_id);
  std::vector<std::string> ids;
  ids.reserve(result.Size());
  for (const auto &row : result) {
    ids.push_back(row["file_id"].As<std::string>());
  }
  return ids;
}

void AttachLessonFileIds(const userver::storages::postgres::ClusterPtr &pg,
                         Lesson &lesson) {
  lesson.file_ids = LoadLessonFileIds(pg, lesson.id);
}

void AttachLessonFileIds(const userver::storages::postgres::ClusterPtr &pg,
                         std::vector<Lesson> &lessons) {
  for (auto &lesson : lessons) {
    AttachLessonFileIds(pg, lesson);
  }
}

void InsertLessonFiles(const userver::storages::postgres::ClusterPtr &pg,
                       const std::string &lesson_id,
                       const std::vector<std::string> &file_ids) {
  for (const auto &file_id : file_ids) {
    pg->Execute(kMaster,
                "INSERT INTO lesson_files (lesson_id, file_id) "
                "VALUES ($1::uuid, $2::uuid) ON CONFLICT DO NOTHING",
                lesson_id, file_id);
  }
}

bool SameOptionalString(const std::optional<std::string> &lhs,
                        const std::optional<std::string> &rhs) {
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return !lhs.has_value() || *lhs == *rhs;
}

std::string OptionalUuidParam(const std::optional<std::string> &value) {
  return value.value_or("");
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
    const auto result = WithOverlapGuard([&] {
      return pg_->Execute(
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
            std::string{kLessonFields} +
            "), outbox AS ("
            "  INSERT INTO outbox_events "
            "    (aggregate_type, aggregate_id, event_type, event_version, "
            "payload) "
            "  SELECT 'lesson', id::uuid, 'lesson.scheduled', 1, "
            "         jsonb_build_object("
            "           'lesson_id', id, "
            "           'teacher_id', teacher_id, "
            "           'student_id', student_id, "
            "           'starts_at', starts_at, "
            "           'ends_at', ends_at, "
            "           'scheduled_at', to_char(now() AT TIME ZONE 'UTC', "
            "               'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), "
            "           'origin', 'created') "
            "  FROM inserted"
            ") SELECT * FROM inserted",
        teacher_id, request.student_id, *request.price, *request.slot_id,
        request.starts_at, request.ends_at, request.topic, request.notes);
    });
    if (result.IsEmpty()) {
      throw tutorflow::common::ServiceError::Conflict(
          "slot is not open or does not belong to teacher");
    }
    auto lesson = RowToLesson(result[0]);
    InsertLessonFiles(pg_, lesson.id, request.file_ids);
    AttachLessonFileIds(pg_, lesson);
    return lesson;
  }

  const auto result = WithOverlapGuard([&] {
    return pg_->Execute(
      kMaster,
      "WITH inserted AS ("
      "  INSERT INTO lessons (teacher_id, student_id, starts_at, ends_at, "
      "                       topic, notes, price) "
      "  SELECT $1::uuid, $2::uuid, $3::timestamptz, $4::timestamptz, $5, "
      "         $6, $7::numeric "
      "  WHERE $3::timestamptz < $4::timestamptz "
      "  RETURNING " +
          std::string{kLessonFields} +
          "), outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, "
          "payload) "
          "  SELECT 'lesson', id::uuid, 'lesson.scheduled', 1, "
          "         jsonb_build_object("
          "           'lesson_id', id, "
          "           'teacher_id', teacher_id, "
          "           'student_id', student_id, "
          "           'starts_at', starts_at, "
          "           'ends_at', ends_at, "
          "           'scheduled_at', to_char(now() AT TIME ZONE 'UTC', "
          "               'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), "
          "           'origin', 'created') "
          "  FROM inserted"
          ") SELECT * FROM inserted",
      teacher_id, request.student_id, request.starts_at, request.ends_at,
      request.topic, request.notes, *request.price);
  });
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Validation(
        "starts_at must be earlier than ends_at");
  }
  auto lesson = RowToLesson(result[0]);
  InsertLessonFiles(pg_, lesson.id, request.file_ids);
  AttachLessonFileIds(pg_, lesson);
  return lesson;
}

std::vector<Lesson>
LessonRepository::ListLessonsForTeacher(const std::string &teacher_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kLessonFields} +
                       " FROM lessons WHERE teacher_id = $1::uuid "
                       "ORDER BY starts_at, created_at",
                   teacher_id);
  auto lessons = RowsToVector<Lesson>(result, RowToLesson);
  AttachLessonFileIds(pg_, lessons);
  return lessons;
}

std::vector<Lesson>
LessonRepository::ListLessonsForStudent(const std::string &student_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kLessonFields} +
                       " FROM lessons WHERE student_id = $1::uuid "
                       "ORDER BY starts_at, created_at",
                   student_id);
  auto lessons = RowsToVector<Lesson>(result, RowToLesson);
  AttachLessonFileIds(pg_, lessons);
  return lessons;
}

std::optional<Lesson>
LessonRepository::FindLesson(const std::string &lesson_id) const {
  const auto result = pg_->Execute(kSlave,
                                   "SELECT " + std::string{kLessonFields} +
                                       " FROM lessons WHERE id = $1::uuid",
                                   lesson_id);
  if (result.IsEmpty())
    return std::nullopt;
  auto lesson = RowToLesson(result[0]);
  AttachLessonFileIds(pg_, lesson);
  return lesson;
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

  // Transactional outbox (5E-1): меняем статус и пишем событие
  // lesson.completed в ОДНОЙ транзакции (один CTE-стейтмент = одна транзакция).
  // payload самодостаточен (docs/event-contracts/lesson.completed.v1.json).
  const std::string sql =
      R"(WITH completed AS (
           UPDATE lessons SET status = 'completed', completed_at = now()
           WHERE id = $1::uuid AND teacher_id = $2::uuid AND status = 'scheduled'
           RETURNING *
         ), outbox AS (
           INSERT INTO outbox_events
             (aggregate_type, aggregate_id, event_type, event_version, payload)
           SELECT 'lesson', id, 'lesson.completed', 1, jsonb_build_object(
             'lesson_id', id::text,
             'teacher_id', teacher_id::text,
             'student_id', student_id::text,
             'price', price::double precision,
             'currency', 'RUB',
             'completed_at', to_char(completed_at AT TIME ZONE 'UTC',
                                     'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
           FROM completed
         ) SELECT )" +
      std::string{kLessonFields} + " FROM completed";
  const auto result = pg_->Execute(kMaster, sql, lesson_id, teacher_id);
  auto lesson = RequireSingleLesson(result, "lesson not found");
  AttachLessonFileIds(pg_, lesson);
  return lesson;
}

Lesson LessonRepository::RescheduleLesson(
    const std::string &teacher_id,
    const RescheduleLessonRequest &request) const {
  const auto current = FindLesson(request.lesson_id);
  if (!current.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  if (current->teacher_id != teacher_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own lesson");
  }
  if (current->status != "scheduled") {
    throw tutorflow::common::ServiceError::Conflict(
        "only scheduled lesson can be rescheduled");
  }

  const auto range =
      pg_->Execute(kMaster,
                   "SELECT $1::timestamptz < $2::timestamptz AS valid",
                   request.new_starts_at, request.new_ends_at);
  if (range.IsEmpty() || !range[0]["valid"].As<bool>()) {
    throw tutorflow::common::ServiceError::Validation(
        "new_starts_at must be earlier than new_ends_at");
  }

  if (current->starts_at == request.new_starts_at &&
      current->ends_at == request.new_ends_at &&
      SameOptionalString(current->slot_id, request.new_slot_id)) {
    return *current;
  }

  const auto old_slot_id = OptionalUuidParam(current->slot_id);
  const auto requested_slot_id = OptionalUuidParam(request.new_slot_id);
  const bool has_new_slot = request.new_slot_id.has_value();
  const bool same_slot = has_new_slot && current->slot_id.has_value() &&
                         *request.new_slot_id == *current->slot_id;

  if (has_new_slot && !same_slot) {
    const std::string sql =
        R"(WITH booked_slot AS (
             UPDATE availability_slots
             SET status = 'booked'
             WHERE id = NULLIF($5, '')::uuid
               AND teacher_id = $2::uuid
               AND status = 'open'
             RETURNING id
           ), updated AS (
             UPDATE lessons
             SET starts_at = $3::timestamptz,
                 ends_at = $4::timestamptz,
                 slot_id = (SELECT id FROM booked_slot)
             WHERE id = $1::uuid
               AND teacher_id = $2::uuid
               AND status = 'scheduled'
               AND EXISTS (SELECT 1 FROM booked_slot)
             RETURNING )" +
        std::string{kLessonFields} +
        R"(
           ), reopened AS (
             UPDATE availability_slots
             SET status = 'open'
             WHERE id = NULLIF($6, '')::uuid
               AND id <> NULLIF($5, '')::uuid
           ), outbox AS (
             INSERT INTO outbox_events
               (aggregate_type, aggregate_id, event_type, event_version, payload)
             SELECT 'lesson', id::uuid, 'lesson.rescheduled', 1,
                    jsonb_build_object(
                      'lesson_id', id,
                      'teacher_id', teacher_id,
                      'student_id', student_id,
                      'old_starts_at', $7,
                      'old_ends_at', $8,
                      'new_starts_at', starts_at,
                      'new_ends_at', ends_at,
                      'rescheduled_at',
                        to_char(now() AT TIME ZONE 'UTC',
                                'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
             FROM updated
           ) SELECT * FROM updated)";
    const auto result = WithOverlapGuard([&] {
      return pg_->Execute(kMaster, sql, request.lesson_id, teacher_id,
                          request.new_starts_at, request.new_ends_at,
                          requested_slot_id, old_slot_id, current->starts_at,
                          current->ends_at);
    });
    if (result.IsEmpty()) {
      throw tutorflow::common::ServiceError::Conflict(
          "new slot is not open or does not belong to teacher");
    }
    auto lesson = RowToLesson(result[0]);
    AttachLessonFileIds(pg_, lesson);
    return lesson;
  }

  const std::optional<std::string> next_slot =
      same_slot ? current->slot_id : std::nullopt;
  const auto next_slot_id = OptionalUuidParam(next_slot);
  const std::string sql =
      R"(WITH updated AS (
           UPDATE lessons
           SET starts_at = $3::timestamptz,
               ends_at = $4::timestamptz,
               slot_id = NULLIF($5, '')::uuid
           WHERE id = $1::uuid
             AND teacher_id = $2::uuid
             AND status = 'scheduled'
           RETURNING )" +
      std::string{kLessonFields} +
      R"(
         ), reopened AS (
           UPDATE availability_slots
           SET status = 'open'
           WHERE id = NULLIF($6, '')::uuid
             AND (NULLIF($5, '')::uuid IS NULL OR id <> NULLIF($5, '')::uuid)
         ), outbox AS (
           INSERT INTO outbox_events
             (aggregate_type, aggregate_id, event_type, event_version, payload)
           SELECT 'lesson', id::uuid, 'lesson.rescheduled', 1,
                  jsonb_build_object(
                    'lesson_id', id,
                    'teacher_id', teacher_id,
                    'student_id', student_id,
                    'old_starts_at', $7,
                    'old_ends_at', $8,
                    'new_starts_at', starts_at,
                    'new_ends_at', ends_at,
                    'rescheduled_at',
                      to_char(now() AT TIME ZONE 'UTC',
                              'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
           FROM updated
         ) SELECT * FROM updated)";
  const auto result = WithOverlapGuard([&] {
    return pg_->Execute(kMaster, sql, request.lesson_id, teacher_id,
                        request.new_starts_at, request.new_ends_at, next_slot_id,
                        old_slot_id, current->starts_at, current->ends_at);
  });
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Conflict(
        "only scheduled lesson can be rescheduled");
  }
  auto lesson = RowToLesson(result[0]);
  AttachLessonFileIds(pg_, lesson);
  return lesson;
}

Lesson LessonRepository::ReactivateLesson(const std::string &lesson_id,
                                          const std::string &teacher_id) const {
  const auto current = FindLesson(lesson_id);
  if (!current.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  if (current->teacher_id != teacher_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own lesson");
  }
  if (current->status == "scheduled") {
    return *current;
  }
  const bool restore_completed = current->completed_at.has_value();
  const std::string target_status =
      restore_completed ? std::string{"completed"} : std::string{"scheduled"};
  if (current->status == target_status) {
    return *current;
  }
  if (current->status != "cancelled") {
    throw tutorflow::common::ServiceError::Conflict(
        "only cancelled lesson can be reactivated");
  }

  const auto slot_id = OptionalUuidParam(current->slot_id);
  if (current->slot_id.has_value()) {
    const std::string sql =
        R"(WITH booked_slot AS (
             UPDATE availability_slots
             SET status = 'booked'
             WHERE id = NULLIF($3, '')::uuid
               AND teacher_id = $2::uuid
               AND status = 'open'
             RETURNING id
           ), reactivated AS (
             UPDATE lessons
             SET status = $4
             WHERE id = $1::uuid
               AND teacher_id = $2::uuid
               AND status = 'cancelled'
               AND EXISTS (SELECT 1 FROM booked_slot)
             RETURNING )" +
        std::string{kLessonFields} +
        R"(
           ), outbox AS (
             INSERT INTO outbox_events
               (aggregate_type, aggregate_id, event_type, event_version, payload)
             SELECT 'lesson', id::uuid,
                    CASE WHEN $4 = 'completed'
                         THEN 'lesson.restored'
                         ELSE 'lesson.scheduled'
                    END,
                    1,
                    CASE WHEN $4 = 'completed' THEN
                      jsonb_build_object(
                        'lesson_id', id,
                        'teacher_id', teacher_id,
                        'student_id', student_id,
                        'price', price,
                        'currency', 'RUB',
                        'restored_at',
                          to_char(now() AT TIME ZONE 'UTC',
                                  'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
                    ELSE
                      jsonb_build_object(
                        'lesson_id', id,
                        'teacher_id', teacher_id,
                        'student_id', student_id,
                        'starts_at', starts_at,
                        'ends_at', ends_at,
                        'scheduled_at',
                          to_char(now() AT TIME ZONE 'UTC',
                                  'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
                        'origin', 'reactivated')
                    END
             FROM reactivated
           ) SELECT * FROM reactivated)";
    const auto result = WithOverlapGuard([&] {
      return pg_->Execute(kMaster, sql, lesson_id, teacher_id, slot_id,
                          target_status);
    });
    if (result.IsEmpty()) {
      throw tutorflow::common::ServiceError::Conflict(
          "lesson slot is not open or does not belong to teacher");
    }
    auto lesson = RowToLesson(result[0]);
    AttachLessonFileIds(pg_, lesson);
    return lesson;
  }

  const std::string sql =
      R"(WITH reactivated AS (
           UPDATE lessons
           SET status = $3
           WHERE id = $1::uuid
             AND teacher_id = $2::uuid
             AND status = 'cancelled'
           RETURNING )" +
      std::string{kLessonFields} +
      R"(
         ), outbox AS (
           INSERT INTO outbox_events
             (aggregate_type, aggregate_id, event_type, event_version, payload)
           SELECT 'lesson', id::uuid,
                  CASE WHEN $3 = 'completed'
                       THEN 'lesson.restored'
                       ELSE 'lesson.scheduled'
                  END,
                  1,
                  CASE WHEN $3 = 'completed' THEN
                    jsonb_build_object(
                      'lesson_id', id,
                      'teacher_id', teacher_id,
                      'student_id', student_id,
                      'price', price,
                      'currency', 'RUB',
                      'restored_at',
                        to_char(now() AT TIME ZONE 'UTC',
                                'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
                  ELSE
                    jsonb_build_object(
                      'lesson_id', id,
                      'teacher_id', teacher_id,
                      'student_id', student_id,
                      'starts_at', starts_at,
                      'ends_at', ends_at,
                      'scheduled_at',
                        to_char(now() AT TIME ZONE 'UTC',
                                'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
                      'origin', 'reactivated')
                  END
           FROM reactivated
         ) SELECT * FROM reactivated)";
  const auto result = WithOverlapGuard([&] {
    return pg_->Execute(kMaster, sql, lesson_id, teacher_id, target_status);
  });
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Conflict(
        "only cancelled lesson can be reactivated");
  }
  auto lesson = RowToLesson(result[0]);
  AttachLessonFileIds(pg_, lesson);
  return lesson;
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
  // 5L.3: разрешаем отмену и scheduled, и completed. previous_status фиксируем
  // из наблюдённого статуса ($3) — он же уходит в lesson.cancelled и задаёт,
  // нужна ли финансовая компенсация (только при 'completed', с price/currency).
  if (current->status != "scheduled" && current->status != "completed") {
    throw tutorflow::common::ServiceError::Conflict(
        "lesson cannot be cancelled from its current status");
  }

  // Transactional outbox: смена статуса + событие lesson.cancelled в ОДНОЙ
  // транзакции (CTE). Слот, если был, освобождаем (open). price/currency в
  // payload только при previous_status='completed' (нужны finance для
  // компенсирующей correction); при 'scheduled' — null (charge не было).
  const std::string sql =
      R"(WITH cancelled AS (
           UPDATE lessons SET status = 'cancelled'
           WHERE id = $1::uuid AND teacher_id = $2::uuid AND status = $3
           RETURNING )" +
      std::string{kLessonFields} +
      R"(
         ), reopened AS (
           UPDATE availability_slots SET status = 'open'
           WHERE id IN (SELECT NULLIF(slot_id, '')::uuid
                        FROM cancelled WHERE slot_id <> '')
         ), outbox AS (
           INSERT INTO outbox_events
             (aggregate_type, aggregate_id, event_type, event_version, payload)
           SELECT 'lesson', id::uuid, 'lesson.cancelled', 1,
                  jsonb_build_object(
                    'lesson_id', id,
                    'teacher_id', teacher_id,
                    'student_id', student_id,
                    'previous_status', $3,
                    'price', CASE WHEN $3 = 'completed'
                                  THEN price::double precision ELSE NULL END,
                    'currency', CASE WHEN $3 = 'completed'
                                     THEN 'RUB' ELSE NULL END,
                    'cancelled_at',
                      to_char(now() AT TIME ZONE 'UTC',
                              'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
           FROM cancelled
         ) SELECT * FROM cancelled)";
  const auto result =
      pg_->Execute(kMaster, sql, lesson_id, teacher_id, current->status);
  auto lesson = RequireSingleLesson(result, "lesson not found");
  AttachLessonFileIds(pg_, lesson);
  return lesson;
}

} // namespace tutorflow::lesson
