#include "handlers/assignment_handlers.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>

#include "domain/assignment_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::assignment {
namespace {
namespace http = userver::server::http;
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;
using tutorflow::common::ServiceError;

std::string JsonResponse(const http::HttpRequest &request, json::Value body,
                         http::HttpStatus status = http::HttpStatus::kOk) {
  request.GetHttpResponse().SetStatus(status);
  request.GetHttpResponse().SetHeader(
      std::string{"Content-Type"},
      std::string{"application/json; charset=utf-8"});
  return json::ToString(body);
}

std::string ErrorResponse(const http::HttpRequest &request,
                          const ServiceError &error) {
  return JsonResponse(request, tutorflow::common::MakeErrorBody(error),
                      error.Status());
}

template <typename Func>
std::string HandleEnvelope(const http::HttpRequest &request, Func &&func) {
  try {
    return func();
  } catch (const ServiceError &error) {
    return ErrorResponse(request, error);
  } catch (const std::exception &error) {
    return ErrorResponse(request, ServiceError::Internal(error.what()));
  }
}

json::Value ParseJsonBody(const http::HttpRequest &request) {
  try {
    return json::FromString(request.RequestBody());
  } catch (const std::exception &) {
    throw ServiceError::Validation("request body must be valid JSON");
  }
}

std::string RequireString(const json::Value &body, std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    throw ServiceError::Validation("missing required field: " + key);
  }
  auto value = body[key].As<std::string>("");
  if (value.empty()) {
    throw ServiceError::Validation("field must not be empty: " + key);
  }
  return value;
}

std::optional<std::string> OptionalString(const json::Value &body,
                                          std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    return std::nullopt;
  }
  auto value = body[key].As<std::string>("");
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::string> OptionalStringArray(const json::Value &body,
                                             std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    return {};
  }
  if (!body[key].IsArray()) {
    throw ServiceError::Validation("field must be an array: " + key);
  }
  std::vector<std::string> values;
  for (const auto &item : body[key]) {
    auto value = item.As<std::string>("");
    if (value.empty()) {
      throw ServiceError::Validation("array item must not be empty: " + key);
    }
    values.push_back(std::move(value));
  }
  return values;
}

CreateAssignmentRequest
ParseCreateAssignmentRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return CreateAssignmentRequest{
      .student_id = RequireString(body, "student_id"),
      .title = RequireString(body, "title"),
      .description = OptionalString(body, "description"),
      .due_at = OptionalString(body, "due_at"),
      .file_ids = OptionalStringArray(body, "file_ids"),
  };
}

SubmitRequest ParseSubmitRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return SubmitRequest{
      .text_answer = OptionalString(body, "text_answer"),
      .file_ids = OptionalStringArray(body, "file_ids"),
  };
}

ReviewRequest ParseReviewRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return ReviewRequest{
      .status = RequireString(body, "status"),
      .comment = OptionalString(body, "comment"),
  };
}

CommentRequest ParseCommentRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return CommentRequest{.text = RequireString(body, "text")};
}

std::string RequiredPathArg(const http::HttpRequest &request,
                            std::string_view name) {
  auto value = request.GetPathArg(name);
  if (value.empty()) {
    throw ServiceError::Validation("missing " + std::string{name} +
                                   " path parameter");
  }
  return value;
}

template <typename T>
json::Value ToJsonArray(const std::vector<T> &items) {
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &item : items) {
    array.PushBack(ToJson(item));
  }
  return array.ExtractValue();
}

} // namespace

CreateAssignmentHandler::CreateAssignmentHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

std::string CreateAssignmentHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJson(service_.CreateAssignment(
                            auth, ParseCreateAssignmentRequest(request))),
                        http::HttpStatus::kCreated);
  });
}

ListAssignmentsHandler::ListAssignmentsHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

std::string ListAssignmentsHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJsonArray(service_.ListAssignments(auth)));
  });
}

GetAssignmentHandler::GetAssignmentHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

std::string GetAssignmentHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    return JsonResponse(
        request, ToJson(service_.GetAssignment(
                     RequiredPathArg(request, "assignmentId"))));
  });
}

SubmitAssignmentHandler::SubmitAssignmentHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

std::string SubmitAssignmentHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(
        request,
        ToJson(service_.SubmitAssignment(
            auth, RequiredPathArg(request, "assignmentId"),
            ParseSubmitRequest(request))),
        http::HttpStatus::kCreated);
  });
}

ReviewAssignmentHandler::ReviewAssignmentHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

std::string ReviewAssignmentHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(
        request,
        ToJson(service_.ReviewAssignment(
            auth, RequiredPathArg(request, "assignmentId"),
            ParseReviewRequest(request))));
  });
}

CreateCommentHandler::CreateCommentHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

std::string CreateCommentHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(
        request,
        ToJson(service_.CreateComment(
            auth, RequiredPathArg(request, "assignmentId"),
            ParseCommentRequest(request))),
        http::HttpStatus::kCreated);
  });
}

} // namespace tutorflow::assignment
