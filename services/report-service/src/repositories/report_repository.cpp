#include "repositories/report_repository.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_config.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>

namespace tutorflow::report {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

std::optional<std::string> EmptyToNull(std::string value) {
  if (value.empty()) return std::nullopt;
  return value;
}

std::string TimeField(std::string_view field, std::string_view alias) {
  return "COALESCE(to_char(" + std::string{field} +
         " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS " +
         std::string{alias};
}

FinanceSummary RowToFinance(const pg::Row& row) {
  return FinanceSummary{
      .balance_amount = row["balance_amount"].As<double>(),
      .debt_amount = row["debt_amount"].As<double>(),
      .overpaid_amount = row["overpaid_amount"].As<double>(),
      .currency = row["currency"].As<std::string>(),
      .pending_receipts_count = row["pending_receipts_count"].As<int>(),
      .pending_receipts_amount = row["pending_receipts_amount"].As<double>(),
      .last_payment_at = EmptyToNull(row["last_payment_at"].As<std::string>()),
      .updated_at = row["finance_updated_at"].As<std::string>(),
  };
}

ActivitySummary RowToActivity(const pg::Row& row) {
  return ActivitySummary{
      .upcoming_lessons_count = row["upcoming_lessons_count"].As<int>(),
      .completed_lessons_count = row["completed_lessons_count"].As<int>(),
      .cancelled_lessons_count = row["cancelled_lessons_count"].As<int>(),
      .active_assignments_count = row["active_assignments_count"].As<int>(),
      .submitted_assignments_count = row["submitted_assignments_count"].As<int>(),
      .reviewed_assignments_count = row["reviewed_assignments_count"].As<int>(),
      .last_lesson_at = EmptyToNull(row["last_lesson_at"].As<std::string>()),
      .next_lesson_at = EmptyToNull(row["next_lesson_at"].As<std::string>()),
      .updated_at = row["activity_updated_at"].As<std::string>(),
  };
}

StudentSummary RowToStudentSummary(const pg::Row& row) {
  return StudentSummary{
      .teacher_id = row["teacher_id"].As<std::string>(),
      .student_id = row["student_id"].As<std::string>(),
      .finance = RowToFinance(row),
      .activity = RowToActivity(row),
      .updated_at = row["updated_at"].As<std::string>(),
  };
}

std::string StudentSummaryFields() {
  std::string fields = R"(
    COALESCE(a.teacher_id::text, f.teacher_id::text) AS teacher_id,
    COALESCE(a.student_id::text, f.student_id::text) AS student_id,
    COALESCE(f.balance_amount, 0)::double precision AS balance_amount,
    COALESCE(f.debt_amount, 0)::double precision AS debt_amount,
    COALESCE(f.overpaid_amount, 0)::double precision AS overpaid_amount,
    COALESCE(f.currency, 'RUB') AS currency,
    COALESCE(f.pending_receipts_count, 0) AS pending_receipts_count,
    COALESCE(f.pending_receipts_amount, 0)::double precision AS pending_receipts_amount,
  )";
  fields += TimeField("f.last_payment_at", "last_payment_at");
  fields += R"(,
  )";
  fields += TimeField("COALESCE(f.updated_at, now())", "finance_updated_at");
  fields += R"(,
    COALESCE(a.upcoming_lessons_count, 0) AS upcoming_lessons_count,
    COALESCE(a.completed_lessons_count, 0) AS completed_lessons_count,
    COALESCE(a.cancelled_lessons_count, 0) AS cancelled_lessons_count,
    COALESCE(a.active_assignments_count, 0) AS active_assignments_count,
    COALESCE(a.submitted_assignments_count, 0) AS submitted_assignments_count,
    COALESCE(a.reviewed_assignments_count, 0) AS reviewed_assignments_count,
  )";
  fields += TimeField("a.last_lesson_at", "last_lesson_at");
  fields += R"(,
  )";
  fields += TimeField("a.next_lesson_at", "next_lesson_at");
  fields += R"(,
  )";
  fields += TimeField("COALESCE(a.updated_at, now())", "activity_updated_at");
  fields += R"(,
  )";
  fields += TimeField(
      "GREATEST(COALESCE(a.updated_at, '-infinity'::timestamptz), "
      "COALESCE(f.updated_at, '-infinity'::timestamptz), now())",
      "updated_at");
  return fields;
}

void RefreshTeacherSummary(
    const userver::storages::postgres::ClusterPtr& pg,
    const std::string& teacher_id) {
  pg->Execute(kMaster,
              R"(
              INSERT INTO teacher_summary (
                teacher_id, students_count, upcoming_lessons_count,
                pending_submissions_count, pending_receipts_count,
                pending_receipts_amount, total_debt_amount,
                total_overpaid_amount, students_with_debt_count, updated_at
              )
              SELECT $1::uuid,
                     COUNT(DISTINCT COALESCE(a.student_id, f.student_id))::int,
                     COALESCE(SUM(a.upcoming_lessons_count), 0)::int,
                     COALESCE(SUM(a.submitted_assignments_count), 0)::int,
                     COALESCE(SUM(f.pending_receipts_count), 0)::int,
                     COALESCE(SUM(f.pending_receipts_amount), 0),
                     COALESCE(SUM(f.debt_amount), 0),
                     COALESCE(SUM(f.overpaid_amount), 0),
                     COUNT(*) FILTER (WHERE COALESCE(f.debt_amount, 0) > 0)::int,
                     now()
              FROM student_activity_summary a
              FULL JOIN student_finance_summary f
                ON f.teacher_id = a.teacher_id AND f.student_id = a.student_id
              WHERE COALESCE(a.teacher_id, f.teacher_id) = $1::uuid
              ON CONFLICT (teacher_id) DO UPDATE SET
                students_count = EXCLUDED.students_count,
                upcoming_lessons_count = EXCLUDED.upcoming_lessons_count,
                pending_submissions_count = EXCLUDED.pending_submissions_count,
                pending_receipts_count = EXCLUDED.pending_receipts_count,
                pending_receipts_amount = EXCLUDED.pending_receipts_amount,
                total_debt_amount = EXCLUDED.total_debt_amount,
                total_overpaid_amount = EXCLUDED.total_overpaid_amount,
                students_with_debt_count = EXCLUDED.students_with_debt_count,
                updated_at = now()
              )",
              teacher_id);
}

}  // namespace

ReportRepository::ReportRepository(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("report-db")
              .GetCluster()) {}

bool ReportRepository::ApplyLessonEvent(const std::string& event_id,
                                        const std::string& event_type,
                                        const LessonEvent& event) const {
  const auto result = pg_->Execute(
      kMaster,
      R"(
      WITH accepted_event AS (
        INSERT INTO report_processed_events (event_id, event_type)
        VALUES ($1::uuid, $2)
        ON CONFLICT (event_id) DO NOTHING
        RETURNING event_id
      ), upserted AS (
        INSERT INTO report_lessons (
          lesson_id, teacher_id, student_id, status, starts_at, ends_at, event_at
        )
        SELECT $5::uuid, $3::uuid, $4::uuid, $6,
               NULLIF($7, '')::timestamptz,
               NULLIF($8, '')::timestamptz,
               $9::timestamptz
        FROM accepted_event
        ON CONFLICT (lesson_id) DO UPDATE SET
          teacher_id = EXCLUDED.teacher_id,
          student_id = EXCLUDED.student_id,
          status = EXCLUDED.status,
          starts_at = COALESCE(EXCLUDED.starts_at, report_lessons.starts_at),
          ends_at = COALESCE(EXCLUDED.ends_at, report_lessons.ends_at),
          event_at = EXCLUDED.event_at,
          updated_at = now()
        WHERE report_lessons.event_at IS NULL
           OR EXCLUDED.event_at > report_lessons.event_at
           OR (
             EXCLUDED.event_at = report_lessons.event_at
             AND CASE EXCLUDED.status
                   WHEN 'scheduled' THEN 1
                   WHEN 'completed' THEN 2
                   WHEN 'cancelled' THEN 3
                   ELSE 0
                 END >= CASE report_lessons.status
                   WHEN 'scheduled' THEN 1
                   WHEN 'completed' THEN 2
                   WHEN 'cancelled' THEN 3
                   ELSE 0
                 END
           )
        RETURNING lesson_id, teacher_id, student_id, status, starts_at, event_at
      ), lesson_state AS (
        SELECT status, starts_at, event_at
        FROM report_lessons
        WHERE teacher_id = $3::uuid AND student_id = $4::uuid AND lesson_id <> $5::uuid
        UNION ALL
        SELECT status, starts_at, event_at FROM upserted
        UNION ALL
        SELECT status, starts_at, event_at
        FROM report_lessons
        WHERE lesson_id = $5::uuid
          AND NOT EXISTS(SELECT 1 FROM upserted)
          AND EXISTS(SELECT 1 FROM accepted_event)
      ), refreshed AS (
        INSERT INTO student_activity_summary (
          teacher_id, student_id,
          upcoming_lessons_count, completed_lessons_count, cancelled_lessons_count,
          last_lesson_at, next_lesson_at, updated_at
        )
        SELECT $3::uuid, $4::uuid,
               COUNT(*) FILTER (WHERE status = 'scheduled')::int,
               COUNT(*) FILTER (WHERE status = 'completed')::int,
               COUNT(*) FILTER (WHERE status = 'cancelled')::int,
               MAX(COALESCE(starts_at, event_at)) FILTER (WHERE status IN ('completed', 'cancelled')),
               MIN(starts_at) FILTER (WHERE status = 'scheduled'),
               now()
        FROM lesson_state
        HAVING EXISTS(SELECT 1 FROM accepted_event)
        ON CONFLICT (teacher_id, student_id) DO UPDATE SET
          upcoming_lessons_count = EXCLUDED.upcoming_lessons_count,
          completed_lessons_count = EXCLUDED.completed_lessons_count,
          cancelled_lessons_count = EXCLUDED.cancelled_lessons_count,
          last_lesson_at = EXCLUDED.last_lesson_at,
          next_lesson_at = EXCLUDED.next_lesson_at,
          updated_at = now()
      )
      SELECT EXISTS(SELECT 1 FROM accepted_event) AS accepted
      )",
      event_id, event_type, event.teacher_id, event.student_id, event.lesson_id,
      event.status, event.starts_at.value_or(""), event.ends_at.value_or(""),
      event.event_at);
  const auto accepted = !result.IsEmpty() && result[0]["accepted"].As<bool>();
  if (accepted) {
    RefreshTeacherSummary(pg_, event.teacher_id);
  }
  return accepted;
}

bool ReportRepository::ApplyAssignmentEvent(
    const std::string& event_id, const std::string& event_type,
    const AssignmentEvent& event) const {
  const auto result = pg_->Execute(
      kMaster,
      R"(
      WITH accepted_event AS (
        INSERT INTO report_processed_events (event_id, event_type)
        VALUES ($1::uuid, $2)
        ON CONFLICT (event_id) DO NOTHING
        RETURNING event_id
      ), upserted AS (
        INSERT INTO report_assignments (
          assignment_id, teacher_id, student_id, status, event_at
        )
        SELECT $5::uuid, $3::uuid, $4::uuid, $6, $7::timestamptz
        FROM accepted_event
        ON CONFLICT (assignment_id) DO UPDATE SET
          teacher_id = EXCLUDED.teacher_id,
          student_id = EXCLUDED.student_id,
          status = EXCLUDED.status,
          event_at = EXCLUDED.event_at,
          updated_at = now()
        WHERE report_assignments.event_at IS NULL
           OR EXCLUDED.event_at > report_assignments.event_at
           OR (
             EXCLUDED.event_at = report_assignments.event_at
             AND CASE EXCLUDED.status
                   WHEN 'assigned' THEN 1
                   WHEN 'submitted' THEN 2
                   WHEN 'needs_fix' THEN 3
                   WHEN 'reviewed' THEN 3
                   WHEN 'accepted' THEN 3
                   WHEN 'done' THEN 3
                   ELSE 0
                 END >= CASE report_assignments.status
                   WHEN 'assigned' THEN 1
                   WHEN 'submitted' THEN 2
                   WHEN 'needs_fix' THEN 3
                   WHEN 'reviewed' THEN 3
                   WHEN 'accepted' THEN 3
                   WHEN 'done' THEN 3
                   ELSE 0
                 END
           )
        RETURNING assignment_id, status
      ), assignment_state AS (
        SELECT status
        FROM report_assignments
        WHERE teacher_id = $3::uuid AND student_id = $4::uuid
          AND assignment_id <> $5::uuid
        UNION ALL
        SELECT status FROM upserted
        UNION ALL
        SELECT status
        FROM report_assignments
        WHERE assignment_id = $5::uuid
          AND NOT EXISTS(SELECT 1 FROM upserted)
          AND EXISTS(SELECT 1 FROM accepted_event)
      ), refreshed AS (
        INSERT INTO student_activity_summary (
          teacher_id, student_id,
          active_assignments_count, submitted_assignments_count,
          reviewed_assignments_count, updated_at
        )
        SELECT $3::uuid, $4::uuid,
               COUNT(*) FILTER (WHERE status IN ('assigned', 'needs_fix'))::int,
               COUNT(*) FILTER (WHERE status = 'submitted')::int,
               COUNT(*) FILTER (WHERE status IN ('reviewed', 'needs_fix', 'done'))::int,
               now()
        FROM assignment_state
        HAVING EXISTS(SELECT 1 FROM accepted_event)
        ON CONFLICT (teacher_id, student_id) DO UPDATE SET
          active_assignments_count = EXCLUDED.active_assignments_count,
          submitted_assignments_count = EXCLUDED.submitted_assignments_count,
          reviewed_assignments_count = EXCLUDED.reviewed_assignments_count,
          updated_at = now()
      )
      SELECT EXISTS(SELECT 1 FROM accepted_event) AS accepted
      )",
      event_id, event_type, event.teacher_id, event.student_id,
      event.assignment_id, event.status, event.event_at);
  const auto accepted = !result.IsEmpty() && result[0]["accepted"].As<bool>();
  if (accepted) {
    RefreshTeacherSummary(pg_, event.teacher_id);
  }
  return accepted;
}

bool ReportRepository::ApplyBalanceEvent(const std::string& event_id,
                                        const std::string& event_type,
                                        const BalanceEvent& event) const {
  const auto result = pg_->Execute(
      kMaster,
      R"(
      WITH accepted_event AS (
        INSERT INTO report_processed_events (event_id, event_type)
        VALUES ($1::uuid, $2)
        ON CONFLICT (event_id) DO NOTHING
        RETURNING event_id
      ), refreshed AS (
        INSERT INTO student_finance_summary (
          teacher_id, student_id, balance_amount, debt_amount, overpaid_amount,
          currency, last_balance_event_at, updated_at
        )
        SELECT $3::uuid, $4::uuid, $5::numeric,
               GREATEST($5::numeric, 0),
               GREATEST(-$5::numeric, 0),
               $6, $7::timestamptz, now()
        FROM accepted_event
        ON CONFLICT (teacher_id, student_id) DO UPDATE SET
          balance_amount = EXCLUDED.balance_amount,
          debt_amount = EXCLUDED.debt_amount,
          overpaid_amount = EXCLUDED.overpaid_amount,
          currency = EXCLUDED.currency,
          last_balance_event_at = EXCLUDED.last_balance_event_at,
          updated_at = now()
      )
      SELECT EXISTS(SELECT 1 FROM accepted_event) AS accepted
      )",
      event_id, event_type, event.teacher_id, event.student_id,
      event.balance_amount, event.currency, event.changed_at);
  const auto accepted = !result.IsEmpty() && result[0]["accepted"].As<bool>();
  if (accepted) {
    RefreshTeacherSummary(pg_, event.teacher_id);
  }
  return accepted;
}

bool ReportRepository::ApplyReceiptEvent(const std::string& event_id,
                                        const std::string& event_type,
                                        const ReceiptEvent& event) const {
  const auto result = pg_->Execute(
      kMaster,
      R"(
      WITH accepted_event AS (
        INSERT INTO report_processed_events (event_id, event_type)
        VALUES ($1::uuid, $2)
        ON CONFLICT (event_id) DO NOTHING
        RETURNING event_id
      ), upserted AS (
        INSERT INTO report_receipts (
          receipt_id, teacher_id, student_id, status, amount, currency, event_at
        )
        SELECT $5::uuid, $3::uuid, $4::uuid, $6, $7::numeric, $8, $9::timestamptz
        FROM accepted_event
        ON CONFLICT (receipt_id) DO UPDATE SET
          teacher_id = EXCLUDED.teacher_id,
          student_id = EXCLUDED.student_id,
          status = EXCLUDED.status,
          amount = EXCLUDED.amount,
          currency = EXCLUDED.currency,
          event_at = EXCLUDED.event_at,
          updated_at = now()
        WHERE report_receipts.event_at IS NULL
           OR EXCLUDED.event_at > report_receipts.event_at
           OR (
             EXCLUDED.event_at = report_receipts.event_at
             AND CASE EXCLUDED.status
                   WHEN 'pending_review' THEN 1
                   WHEN 'confirmed' THEN 2
                   WHEN 'rejected' THEN 2
                   ELSE 0
                 END >= CASE report_receipts.status
                   WHEN 'pending_review' THEN 1
                   WHEN 'confirmed' THEN 2
                   WHEN 'rejected' THEN 2
                   ELSE 0
                 END
           )
        RETURNING receipt_id, status, amount, event_at
      ), receipt_state AS (
        SELECT status, amount, event_at
        FROM report_receipts
        WHERE teacher_id = $3::uuid AND student_id = $4::uuid
          AND receipt_id <> $5::uuid
        UNION ALL
        SELECT status, amount, event_at FROM upserted
        UNION ALL
        SELECT status, amount, event_at
        FROM report_receipts
        WHERE receipt_id = $5::uuid
          AND NOT EXISTS(SELECT 1 FROM upserted)
          AND EXISTS(SELECT 1 FROM accepted_event)
      ), refreshed AS (
        INSERT INTO student_finance_summary (
          teacher_id, student_id, currency,
          pending_receipts_count, pending_receipts_amount,
          last_payment_at, updated_at
        )
        SELECT $3::uuid, $4::uuid, $8,
               COUNT(*) FILTER (WHERE status = 'pending_review')::int,
               COALESCE(SUM(amount) FILTER (WHERE status = 'pending_review'), 0),
               MAX(event_at) FILTER (WHERE status = 'confirmed'),
               now()
        FROM receipt_state
        HAVING EXISTS(SELECT 1 FROM accepted_event)
        ON CONFLICT (teacher_id, student_id) DO UPDATE SET
          currency = COALESCE(student_finance_summary.currency, EXCLUDED.currency),
          pending_receipts_count = EXCLUDED.pending_receipts_count,
          pending_receipts_amount = EXCLUDED.pending_receipts_amount,
          last_payment_at = COALESCE(
              GREATEST(student_finance_summary.last_payment_at,
                       EXCLUDED.last_payment_at),
              student_finance_summary.last_payment_at,
              EXCLUDED.last_payment_at),
          updated_at = now()
      )
      SELECT EXISTS(SELECT 1 FROM accepted_event) AS accepted
      )",
      event_id, event_type, event.teacher_id, event.student_id, event.receipt_id,
      event.status, event.amount, event.currency, event.event_at);
  const auto accepted = !result.IsEmpty() && result[0]["accepted"].As<bool>();
  if (accepted) {
    RefreshTeacherSummary(pg_, event.teacher_id);
  }
  return accepted;
}

StudentSummary ReportRepository::GetStudentSummary(
    const std::string& teacher_id, const std::string& student_id) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT " + StudentSummaryFields() +
          " FROM student_activity_summary a "
          "FULL JOIN student_finance_summary f "
          "  ON f.teacher_id = a.teacher_id AND f.student_id = a.student_id "
          "WHERE COALESCE(a.teacher_id, f.teacher_id) = $1::uuid "
          "  AND COALESCE(a.student_id, f.student_id) = $2::uuid",
      teacher_id, student_id);
  if (!result.IsEmpty()) {
    return RowToStudentSummary(result[0]);
  }
  StudentSummary summary;
  summary.teacher_id = teacher_id;
  summary.student_id = student_id;
  summary.finance.updated_at = "";
  summary.activity.updated_at = "";
  summary.updated_at = "";
  return summary;
}

TeacherDashboard
ReportRepository::GetTeacherDashboard(const std::string& teacher_id) const {
  TeacherDashboard dashboard;
  dashboard.teacher_id = teacher_id;
  const auto totals = pg_->Execute(
      kSlave,
      R"(
      SELECT students_count, upcoming_lessons_count, pending_submissions_count,
             pending_receipts_count,
             pending_receipts_amount::double precision AS pending_receipts_amount,
             total_debt_amount::double precision AS total_debt_amount,
             total_overpaid_amount::double precision AS total_overpaid_amount,
             students_with_debt_count,
             COALESCE(to_char(updated_at AT TIME ZONE 'UTC',
                              'YYYY-MM-DD"T"HH24:MI:SS"Z"'), '') AS updated_at
      FROM teacher_summary WHERE teacher_id = $1::uuid
      )",
      teacher_id);
  if (!totals.IsEmpty()) {
    dashboard.students_count = totals[0]["students_count"].As<int>();
    dashboard.upcoming_lessons_count =
        totals[0]["upcoming_lessons_count"].As<int>();
    dashboard.pending_submissions_count =
        totals[0]["pending_submissions_count"].As<int>();
    dashboard.pending_receipts_count =
        totals[0]["pending_receipts_count"].As<int>();
    dashboard.pending_receipts_amount =
        totals[0]["pending_receipts_amount"].As<double>();
    dashboard.total_debt_amount = totals[0]["total_debt_amount"].As<double>();
    dashboard.total_overpaid_amount =
        totals[0]["total_overpaid_amount"].As<double>();
    dashboard.students_with_debt_count =
        totals[0]["students_with_debt_count"].As<int>();
    dashboard.updated_at = totals[0]["updated_at"].As<std::string>();
  }

  const auto rows = pg_->Execute(
      kSlave,
      "SELECT " + StudentSummaryFields() +
          " FROM student_activity_summary a "
          "FULL JOIN student_finance_summary f "
          "  ON f.teacher_id = a.teacher_id AND f.student_id = a.student_id "
          "WHERE COALESCE(a.teacher_id, f.teacher_id) = $1::uuid "
          "ORDER BY COALESCE(a.updated_at, f.updated_at) DESC",
      teacher_id);
  dashboard.students.reserve(rows.Size());
  for (const auto& row : rows) {
    dashboard.students.push_back(RowToStudentSummary(row));
  }
  return dashboard;
}

StudentDashboard
ReportRepository::GetStudentDashboard(const std::string& student_id) const {
  StudentDashboard dashboard;
  dashboard.student_id = student_id;
  const auto rows = pg_->Execute(
      kSlave,
      "SELECT " + StudentSummaryFields() +
          " FROM student_activity_summary a "
          "FULL JOIN student_finance_summary f "
          "  ON f.teacher_id = a.teacher_id AND f.student_id = a.student_id "
          "WHERE COALESCE(a.student_id, f.student_id) = $1::uuid "
          "ORDER BY COALESCE(a.updated_at, f.updated_at) DESC",
      student_id);
  dashboard.summaries.reserve(rows.Size());
  for (const auto& row : rows) {
    auto summary = RowToStudentSummary(row);
    dashboard.total_debt_amount += summary.finance.debt_amount;
    dashboard.total_overpaid_amount += summary.finance.overpaid_amount;
    dashboard.pending_receipts_count += summary.finance.pending_receipts_count;
    dashboard.pending_receipts_amount += summary.finance.pending_receipts_amount;
    dashboard.updated_at = summary.updated_at;
    dashboard.summaries.push_back(std::move(summary));
  }
  return dashboard;
}

}  // namespace tutorflow::report
