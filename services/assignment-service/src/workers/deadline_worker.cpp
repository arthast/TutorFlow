#include "workers/deadline_worker.hpp"

#include <string>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::assignment {
namespace {

namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr std::string_view kTaskName = "assignment-deadline-worker";
constexpr int kDefaultPeriodMs = 60000;

// Одна транзакция (один CTE-стейтмент): снимок previous_status -> UPDATE в
// 'expired' -> по строке outbox-событие assignment.deadline_expired. Payload —
// строго по docs/event-contracts/assignment.deadline_expired.v1.json.
// Просрочиваем только статусы, где мяч на стороне ученика: 'assigned' и
// 'needs_fix'. submitted/reviewed/done не трогаем; due_at IS NULL не
// просрочивается никогда. SKIP LOCKED — на случай параллельного прогона.
constexpr std::string_view kExpireSql = R"(
  WITH to_expire AS (
    SELECT id, status AS previous_status
    FROM assignments
    WHERE status IN ('assigned', 'needs_fix')
      AND due_at IS NOT NULL
      AND due_at < now()
    FOR UPDATE SKIP LOCKED
  ), updated AS (
    UPDATE assignments a
    SET status = 'expired'
    FROM to_expire t
    WHERE a.id = t.id
    RETURNING a.id, a.teacher_id, a.student_id, a.title, a.due_at,
              t.previous_status
  ), outbox AS (
    INSERT INTO outbox_events
      (aggregate_type, aggregate_id, event_type, event_version, payload)
    SELECT 'assignment', id, 'assignment.deadline_expired', 1,
           jsonb_build_object(
             'assignment_id', id::text,
             'teacher_id', teacher_id::text,
             'student_id', student_id::text,
             'title', title,
             'due_at', to_char(due_at AT TIME ZONE 'UTC',
                               'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
             'previous_status', previous_status,
             'expired_at', to_char(now() AT TIME ZONE 'UTC',
                                   'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
    FROM updated
  )
  SELECT count(*)::int AS expired_count FROM updated
)";

}  // namespace

DeadlineWorker::DeadlineWorker(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("assignment-db")
              .GetCluster()),
      period_(std::chrono::milliseconds{
          config["period-ms"].As<int>(kDefaultPeriodMs)}) {}

void DeadlineWorker::OnAllComponentsLoaded() {
  task_.Start(std::string{kTaskName},
              userver::utils::PeriodicTask::Settings{period_},
              [this] { ExpireOverdue(); });
}

void DeadlineWorker::ExpireOverdue() const {
  const auto result = pg_->Execute(kMaster, std::string{kExpireSql});
  const auto expired_count =
      result.IsEmpty() ? 0 : result[0]["expired_count"].As<int>();
  if (expired_count > 0) {
    LOG_INFO() << "[deadline-worker] expired " << expired_count
               << " assignment(s)";
  }
}

userver::yaml_config::Schema DeadlineWorker::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: assignment deadline auto-expiry worker
additionalProperties: false
properties:
    period-ms:
        type: integer
        description: tick interval in milliseconds
        defaultDescription: '60000'
)");
}

}  // namespace tutorflow::assignment
