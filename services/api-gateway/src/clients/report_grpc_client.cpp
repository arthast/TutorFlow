#include "clients/report_grpc_client.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::report::v1;

std::string StudentName(
    const proto::StudentSummary& summary,
    const std::unordered_map<std::string, std::string>& student_names) {
  const auto found = student_names.find(summary.student_id());
  if (found != student_names.end()) return found->second;
  return summary.student_name();
}

json::Value ToJson(const proto::FinanceSummary& finance) {
  json::ValueBuilder body;
  body["balance_amount"] = finance.balance_amount();
  body["debt_amount"] = finance.debt_amount();
  body["overpaid_amount"] = finance.overpaid_amount();
  body["currency"] = finance.currency();
  body["pending_receipts_count"] = finance.pending_receipts_count();
  body["pending_receipts_amount"] = finance.pending_receipts_amount();
  body["last_payment_at"] = finance.last_payment_at();
  body["updated_at"] = finance.updated_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::ActivitySummary& activity) {
  json::ValueBuilder body;
  body["upcoming_lessons_count"] = activity.upcoming_lessons_count();
  body["completed_lessons_count"] = activity.completed_lessons_count();
  body["cancelled_lessons_count"] = activity.cancelled_lessons_count();
  body["active_assignments_count"] = activity.active_assignments_count();
  body["submitted_assignments_count"] = activity.submitted_assignments_count();
  body["reviewed_assignments_count"] = activity.reviewed_assignments_count();
  body["last_lesson_at"] = activity.last_lesson_at();
  body["next_lesson_at"] = activity.next_lesson_at();
  body["updated_at"] = activity.updated_at();
  return body.ExtractValue();
}

json::Value ToJson(
    const proto::StudentSummary& summary,
    const std::unordered_map<std::string, std::string>& student_names = {}) {
  json::ValueBuilder body;
  body["teacher_id"] = summary.teacher_id();
  body["student_id"] = summary.student_id();
  body["student_name"] = StudentName(summary, student_names);
  body["finance"] = ToJson(summary.finance());
  body["activity"] = ToJson(summary.activity());
  body["updated_at"] = summary.updated_at();
  return body.ExtractValue();
}

json::Value ToJson(
    const proto::TeacherDashboard& dashboard,
    const std::unordered_map<std::string, std::string>& student_names) {
  json::ValueBuilder body;
  body["teacher_id"] = dashboard.teacher_id();
  body["students_count"] = dashboard.students_count();
  body["upcoming_lessons_count"] = dashboard.upcoming_lessons_count();
  body["pending_submissions_count"] = dashboard.pending_submissions_count();
  body["pending_receipts_count"] = dashboard.pending_receipts_count();
  body["pending_receipts_amount"] = dashboard.pending_receipts_amount();
  body["total_debt_amount"] = dashboard.total_debt_amount();
  body["total_overpaid_amount"] = dashboard.total_overpaid_amount();
  body["students_with_debt_count"] = dashboard.students_with_debt_count();
  body["updated_at"] = dashboard.updated_at();
  json::ValueBuilder students(common_formats::Type::kArray);
  for (const auto& student : dashboard.students()) {
    students.PushBack(ToJson(student, student_names));
  }
  body["students"] = students.ExtractValue();
  return body.ExtractValue();
}

json::Value ToJson(const proto::StudentDashboard& dashboard) {
  json::ValueBuilder body;
  body["student_id"] = dashboard.student_id();
  body["total_debt_amount"] = dashboard.total_debt_amount();
  body["total_overpaid_amount"] = dashboard.total_overpaid_amount();
  body["pending_receipts_count"] = dashboard.pending_receipts_count();
  body["pending_receipts_amount"] = dashboard.pending_receipts_amount();
  body["updated_at"] = dashboard.updated_at();
  json::ValueBuilder summaries(common_formats::Type::kArray);
  for (const auto& summary : dashboard.summaries()) {
    summaries.PushBack(ToJson(summary));
  }
  body["summaries"] = summaries.ExtractValue();
  return body.ExtractValue();
}

}  // namespace

GrpcReportClient::GrpcReportClient(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::ReportServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout =
              std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)},
      } {}

json::Value GrpcReportClient::GetTeacherDashboard(
    const tutorflow::clients::GrpcCallContext& call_context,
    const std::unordered_map<std::string, std::string>& student_names) const {
  proto::GetTeacherDashboardRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.GetTeacherDashboard(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  }), student_names);
}

json::Value GrpcReportClient::GetStudentDashboard(
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::GetStudentDashboardRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.GetStudentDashboard(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  }));
}

json::Value GrpcReportClient::GetStudentSummary(
    std::string_view student_id,
    const tutorflow::clients::GrpcCallContext& call_context,
    std::string_view student_name) const {
  proto::GetStudentSummaryRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_student_id(std::string{student_id});
  auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.GetStudentSummary(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  });
  std::unordered_map<std::string, std::string> names;
  names.emplace(std::string{student_id}, std::string{student_name});
  return ToJson(response, names);
}

userver::yaml_config::Schema GrpcReportClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway report gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: report gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

}  // namespace tutorflow::gateway
