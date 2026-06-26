#include "grpc/report_grpc_service.hpp"

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>

namespace tutorflow::report {
namespace {
namespace proto = tutorflow::report::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

void FillFinance(proto::FinanceSummary& proto_summary,
                 const FinanceSummary& summary) {
  proto_summary.set_balance_amount(summary.balance_amount);
  proto_summary.set_debt_amount(summary.debt_amount);
  proto_summary.set_overpaid_amount(summary.overpaid_amount);
  proto_summary.set_currency(summary.currency);
  proto_summary.set_pending_receipts_count(summary.pending_receipts_count);
  proto_summary.set_pending_receipts_amount(summary.pending_receipts_amount);
  if (summary.last_payment_at) {
    proto_summary.set_last_payment_at(*summary.last_payment_at);
  }
  proto_summary.set_updated_at(summary.updated_at);
}

void FillActivity(proto::ActivitySummary& proto_summary,
                  const ActivitySummary& summary) {
  proto_summary.set_upcoming_lessons_count(summary.upcoming_lessons_count);
  proto_summary.set_completed_lessons_count(summary.completed_lessons_count);
  proto_summary.set_cancelled_lessons_count(summary.cancelled_lessons_count);
  proto_summary.set_active_assignments_count(summary.active_assignments_count);
  proto_summary.set_submitted_assignments_count(
      summary.submitted_assignments_count);
  proto_summary.set_reviewed_assignments_count(
      summary.reviewed_assignments_count);
  if (summary.last_lesson_at) {
    proto_summary.set_last_lesson_at(*summary.last_lesson_at);
  }
  if (summary.next_lesson_at) {
    proto_summary.set_next_lesson_at(*summary.next_lesson_at);
  }
  proto_summary.set_updated_at(summary.updated_at);
}

proto::StudentSummary ToProto(const StudentSummary& summary) {
  proto::StudentSummary response;
  response.set_teacher_id(summary.teacher_id);
  response.set_student_id(summary.student_id);
  FillFinance(*response.mutable_finance(), summary.finance);
  FillActivity(*response.mutable_activity(), summary.activity);
  response.set_updated_at(summary.updated_at);
  return response;
}

proto::TeacherDashboard ToProto(const TeacherDashboard& dashboard) {
  proto::TeacherDashboard response;
  response.set_teacher_id(dashboard.teacher_id);
  response.set_students_count(dashboard.students_count);
  response.set_upcoming_lessons_count(dashboard.upcoming_lessons_count);
  response.set_pending_submissions_count(dashboard.pending_submissions_count);
  response.set_pending_receipts_count(dashboard.pending_receipts_count);
  response.set_pending_receipts_amount(dashboard.pending_receipts_amount);
  response.set_total_debt_amount(dashboard.total_debt_amount);
  response.set_total_overpaid_amount(dashboard.total_overpaid_amount);
  response.set_students_with_debt_count(dashboard.students_with_debt_count);
  response.set_updated_at(dashboard.updated_at);
  for (const auto& student : dashboard.students) {
    *response.add_students() = ToProto(student);
  }
  return response;
}

proto::StudentDashboard ToProto(const StudentDashboard& dashboard) {
  proto::StudentDashboard response;
  response.set_student_id(dashboard.student_id);
  response.set_total_debt_amount(dashboard.total_debt_amount);
  response.set_total_overpaid_amount(dashboard.total_overpaid_amount);
  response.set_pending_receipts_count(dashboard.pending_receipts_count);
  response.set_pending_receipts_amount(dashboard.pending_receipts_amount);
  response.set_updated_at(dashboard.updated_at);
  for (const auto& summary : dashboard.summaries) {
    *response.add_summaries() = ToProto(summary);
  }
  return response;
}

}  // namespace

ReportGrpcService::ReportGrpcService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : proto::ReportServiceBase::Component(config, context),
      service_(context.FindComponent<ReportService>()) {}

ReportGrpcService::GetTeacherDashboardResult
ReportGrpcService::GetTeacherDashboard(
    CallContext& context, proto::GetTeacherDashboardRequest&& request) {
  return InvokeServerUnary<proto::TeacherDashboard>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.GetTeacherDashboard(auth));
  });
}

ReportGrpcService::GetStudentDashboardResult
ReportGrpcService::GetStudentDashboard(
    CallContext& context, proto::GetStudentDashboardRequest&& request) {
  return InvokeServerUnary<proto::StudentDashboard>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.GetStudentDashboard(auth));
  });
}

ReportGrpcService::GetStudentSummaryResult ReportGrpcService::GetStudentSummary(
    CallContext& context, proto::GetStudentSummaryRequest&& request) {
  return InvokeServerUnary<proto::StudentSummary>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.GetStudentSummary(auth, request.student_id()));
  });
}

}  // namespace tutorflow::report
