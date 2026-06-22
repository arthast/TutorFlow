#include "handlers/lesson_handlers.hpp"

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

#include "domain/lesson_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::lesson {
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
  if (!body.HasMember(key) || body[key].IsNull())
    return std::nullopt;
  auto value = body[key].As<std::string>("");
  if (value.empty())
    return std::nullopt;
  return value;
}

std::optional<double> OptionalDouble(const json::Value &body,
                                     std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull())
    return std::nullopt;
  try {
    return body[key].As<double>();
  } catch (const std::exception &) {
    throw ServiceError::Validation("field must be a number: " + key);
  }
}

CreateSlotRequest ParseCreateSlotRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return CreateSlotRequest{
      .starts_at = RequireString(body, "starts_at"),
      .ends_at = RequireString(body, "ends_at"),
  };
}

CreateLessonRequest ParseCreateLessonRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return CreateLessonRequest{
      .student_id = RequireString(body, "student_id"),
      .slot_id = OptionalString(body, "slot_id"),
      .starts_at = RequireString(body, "starts_at"),
      .ends_at = RequireString(body, "ends_at"),
      .topic = OptionalString(body, "topic"),
      .notes = OptionalString(body, "notes"),
      .price = OptionalDouble(body, "price"),
  };
}

template <typename T> json::Value ToJsonArray(const std::vector<T> &items) {
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &item : items) {
    array.PushBack(ToJson(item));
  }
  return array.ExtractValue();
}

std::string LessonId(const http::HttpRequest &request) {
  auto lesson_id = request.GetPathArg("lessonId");
  if (lesson_id.empty()) {
    throw ServiceError::Validation("missing lessonId path parameter");
  }
  return lesson_id;
}

} // namespace

CreateAvailabilityHandler::CreateAvailabilityHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string CreateAvailabilityHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    const auto slot =
        service_.CreateSlot(auth, ParseCreateSlotRequest(request));
    return JsonResponse(request, ToJson(slot), http::HttpStatus::kCreated);
  });
}

ListAvailabilityHandler::ListAvailabilityHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string ListAvailabilityHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request, ToJsonArray(service_.ListSlots(auth)));
  });
}

CreateLessonHandler::CreateLessonHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string CreateLessonHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    const auto lesson =
        service_.CreateLesson(auth, ParseCreateLessonRequest(request));
    return JsonResponse(request, ToJson(lesson), http::HttpStatus::kCreated);
  });
}

ListLessonsHandler::ListLessonsHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string ListLessonsHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request, ToJsonArray(service_.ListLessons(auth)));
  });
}

GetLessonHandler::GetLessonHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string GetLessonHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    return JsonResponse(request, ToJson(service_.GetLesson(LessonId(request))));
  });
}

CompleteLessonHandler::CompleteLessonHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string CompleteLessonHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(
        request, ToJson(service_.CompleteLesson(auth, LessonId(request))));
  });
}

CancelLessonHandler::CancelLessonHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<LessonService>()) {}

std::string CancelLessonHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJson(service_.CancelLesson(auth, LessonId(request))));
  });
}

} // namespace tutorflow::lesson
