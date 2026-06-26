#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tutorflow::report {

struct FinanceSummary {
  double balance_amount{};
  double debt_amount{};
  double overpaid_amount{};
  std::string currency{"RUB"};
  int pending_receipts_count{};
  double pending_receipts_amount{};
  std::optional<std::string> last_payment_at;
  std::string updated_at;
};

struct ActivitySummary {
  int upcoming_lessons_count{};
  int completed_lessons_count{};
  int cancelled_lessons_count{};
  int active_assignments_count{};
  int submitted_assignments_count{};
  int reviewed_assignments_count{};
  std::optional<std::string> last_lesson_at;
  std::optional<std::string> next_lesson_at;
  std::string updated_at;
};

struct StudentSummary {
  std::string teacher_id;
  std::string student_id;
  FinanceSummary finance;
  ActivitySummary activity;
  std::string updated_at;
};

struct TeacherDashboard {
  std::string teacher_id;
  int students_count{};
  int upcoming_lessons_count{};
  int pending_submissions_count{};
  int pending_receipts_count{};
  double pending_receipts_amount{};
  double total_debt_amount{};
  double total_overpaid_amount{};
  int students_with_debt_count{};
  std::vector<StudentSummary> students;
  std::string updated_at;
};

struct StudentDashboard {
  std::string student_id;
  double total_debt_amount{};
  double total_overpaid_amount{};
  int pending_receipts_count{};
  double pending_receipts_amount{};
  std::vector<StudentSummary> summaries;
  std::string updated_at;
};

struct LessonEvent {
  std::string lesson_id;
  std::string teacher_id;
  std::string student_id;
  std::string status;
  std::optional<std::string> starts_at;
  std::optional<std::string> ends_at;
  std::string event_at;
};

struct AssignmentEvent {
  std::string assignment_id;
  std::string teacher_id;
  std::string student_id;
  std::string status;
  std::string event_at;
};

struct BalanceEvent {
  std::string teacher_id;
  std::string student_id;
  double balance_amount{};
  std::string currency{"RUB"};
  std::string changed_at;
};

struct ReceiptEvent {
  std::string receipt_id;
  std::string teacher_id;
  std::string student_id;
  std::string status;
  double amount{};
  std::string currency{"RUB"};
  std::string event_at;
};

}  // namespace tutorflow::report
