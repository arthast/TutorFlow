#include "clients/assignment_grpc_client.hpp"

#include "clients/json_helpers.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/handler_helpers.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::assignment::v1;
namespace common_proto = tutorflow::common::v1;

json::Value ToJson(const proto::Assignment &assignment) {
  json::ValueBuilder body;
  body["id"] = assignment.id();
  body["teacher_id"] = assignment.teacher_id();
  body["student_id"] = assignment.student_id();
  body["title"] = assignment.title();
  body["description"] =
      NullableString(assignment.has_description(), assignment.description());
  body["due_at"] = NullableString(assignment.has_due_at(), assignment.due_at());
  body["status"] = assignment.status();
  body["created_at"] = assignment.created_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::Submission &submission) {
  json::ValueBuilder body;
  body["id"] = submission.id();
  body["assignment_id"] = submission.assignment_id();
  body["student_id"] = submission.student_id();
  body["text_answer"] =
      NullableString(submission.has_text_answer(), submission.text_answer());
  body["status"] = submission.status();
  body["submitted_at"] = submission.submitted_at();
  body["file_ids"] = StringArray(submission.file_ids());
  return body.ExtractValue();
}

json::Value ToJson(const proto::Comment &comment) {
  json::ValueBuilder body;
  body["id"] = comment.id();
  body["assignment_id"] = comment.assignment_id();
  body["author_id"] = comment.author_id();
  body["text"] = comment.text();
  body["created_at"] = comment.created_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::AssignmentDetail &detail) {
  json::ValueBuilder body(ToJson(detail.assignment()));
  body["file_ids"] = StringArray(detail.file_ids());
  json::ValueBuilder submissions(common_formats::Type::kArray);
  for (const auto &submission : detail.submissions()) {
    submissions.PushBack(ToJson(submission));
  }
  body["submissions"] = submissions.ExtractValue();
  json::ValueBuilder comments(common_formats::Type::kArray);
  for (const auto &comment : detail.comments()) {
    comments.PushBack(ToJson(comment));
  }
  body["comments"] = comments.ExtractValue();
  return body.ExtractValue();
}

} // namespace

GrpcAssignmentClient::GrpcAssignmentClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::AssignmentServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout =
              std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)},
      } {}

json::Value GrpcAssignmentClient::ListAssignments(
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ListAssignmentsRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListAssignments(request,
                                   tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &assignment : response.assignments()) {
    array.PushBack(ToJson(assignment));
  }
  return array.ExtractValue();
}

json::Value GrpcAssignmentClient::CreateAssignment(
    const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::CreateAssignmentRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_student_id(tutorflow::common::RequireString(body, "student_id"));
  request.set_title(tutorflow::common::RequireString(body, "title"));
  if (const auto description =
          tutorflow::common::OptionalString(body, "description")) {
    request.set_description(*description);
  }
  if (const auto due_at = tutorflow::common::OptionalString(body, "due_at")) {
    request.set_due_at(*due_at);
  }
  for (auto &file_id : RequireStringArray(body, "file_ids")) {
    request.add_file_ids(std::move(file_id));
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CreateAssignment(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcAssignmentClient::GetAssignment(
    std::string_view assignment_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::GetAssignmentRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_assignment_id(std::string{assignment_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.GetAssignment(request,
                                 tutorflow::clients::IdempotentCall(call_context, options_));
  }));
}

json::Value GrpcAssignmentClient::SubmitAssignment(
    std::string_view assignment_id, const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::SubmitAssignmentRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_assignment_id(std::string{assignment_id});
  if (const auto text_answer =
          tutorflow::common::OptionalString(body, "text_answer")) {
    request.set_text_answer(*text_answer);
  }
  for (auto &file_id : RequireStringArray(body, "file_ids")) {
    request.add_file_ids(std::move(file_id));
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.SubmitAssignment(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcAssignmentClient::ReviewAssignment(
    std::string_view assignment_id, const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ReviewAssignmentRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_assignment_id(std::string{assignment_id});
  request.set_status(tutorflow::common::RequireString(body, "status"));
  if (const auto comment = tutorflow::common::OptionalString(body, "comment")) {
    request.set_comment(*comment);
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.ReviewAssignment(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcAssignmentClient::AddComment(
    std::string_view assignment_id, const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::AddCommentRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_assignment_id(std::string{assignment_id});
  request.set_text(tutorflow::common::RequireString(body, "text"));
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.AddComment(request,
                              tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

userver::yaml_config::Schema GrpcAssignmentClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway assignment gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: assignment gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

} // namespace tutorflow::gateway
