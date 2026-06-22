#include "handlers/identity_handlers.hpp"

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

#include "domain/identity_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::identity {
namespace {
namespace http = userver::server::http;
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;
using tutorflow::common::ServiceError;

std::string JsonResponse(const http::HttpRequest& req, json::Value body,
                         http::HttpStatus status = http::HttpStatus::kOk) {
    req.GetHttpResponse().SetStatus(status);
    req.GetHttpResponse().SetHeader(std::string{"Content-Type"},
                                    std::string{"application/json; charset=utf-8"});
    return json::ToString(body);
}

std::string ErrorResponse(const http::HttpRequest& req, const ServiceError& e) {
    return JsonResponse(req, tutorflow::common::MakeErrorBody(e), e.Status());
}

template <typename Func>
std::string HandleEnvelope(const http::HttpRequest& req, Func&& func) {
    try {
        return func();
    } catch (const ServiceError& e) {
        return ErrorResponse(req, e);
    } catch (const std::exception& e) {
        return ErrorResponse(req, ServiceError::Internal(e.what()));
    }
}

json::Value ParseJsonBody(const http::HttpRequest& req) {
    try {
        return json::FromString(req.RequestBody());
    } catch (const std::exception&) {
        throw ServiceError::Validation("request body must be valid JSON");
    }
}

std::string RequireString(const json::Value& body, std::string_view field) {
    const std::string key{field};
    if (!body.HasMember(key) || body[key].IsNull()) {
        throw ServiceError::Validation("missing required field: " + key);
    }
    auto v = body[key].As<std::string>("");
    if (v.empty()) {
        throw ServiceError::Validation("field must not be empty: " + key);
    }
    return v;
}

std::optional<std::string> OptionalString(const json::Value& body,
                                          std::string_view field) {
    const std::string key{field};
    if (!body.HasMember(key) || body[key].IsNull()) return std::nullopt;
    auto v = body[key].As<std::string>("");
    if (v.empty()) return std::nullopt;
    return v;
}

std::optional<double> OptionalDouble(const json::Value& body,
                                     std::string_view field) {
    const std::string key{field};
    if (!body.HasMember(key) || body[key].IsNull()) return std::nullopt;
    return body[key].As<double>(0.0);
}

std::string RequiredPathArg(const http::HttpRequest& req, std::string_view name) {
    auto v = req.GetPathArg(std::string{name});
    if (v.empty()) {
        throw ServiceError::Validation("missing path parameter: " + std::string{name});
    }
    return v;
}

template <typename T>
json::Value ToJsonArray(const std::vector<T>& items) {
    json::ValueBuilder arr(common_formats::Type::kArray);
    for (const auto& item : items) arr.PushBack(ToJson(item));
    return arr.ExtractValue();
}

}  // namespace

// --- GetUserHandler ---

GetUserHandler::GetUserHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string GetUserHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        return JsonResponse(
            request,
            ToJson(service_.GetUser(RequiredPathArg(request, "userId"))));
    });
}

// --- CheckAccessHandler ---

CheckAccessHandler::CheckAccessHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string CheckAccessHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        const auto body       = ParseJsonBody(request);
        const auto teacher_id = RequireString(body, "teacher_id");
        const auto student_id = RequireString(body, "student_id");
        return JsonResponse(request,
                            ToJson(service_.CheckAccess(teacher_id, student_id)));
    });
}

// --- CreateStudentHandler ---

CreateStudentHandler::CreateStudentHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string CreateStudentHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        const auto auth = tutorflow::common::ParseAuthContext(request);
        tutorflow::common::RequireTeacher(auth);
        const auto body = ParseJsonBody(request);
        CreateStudentRequest req{
            .email        = RequireString(body, "email"),
            .password     = RequireString(body, "password"),
            .display_name = RequireString(body, "display_name"),
            .subject      = OptionalString(body, "subject"),
            .goal         = OptionalString(body, "goal"),
            .hourly_rate  = OptionalDouble(body, "hourly_rate"),
        };
        return JsonResponse(request,
                            ToJson(service_.CreateStudent(auth.user_id, req)),
                            http::HttpStatus::kCreated);
    });
}

// --- GetStudentLinkHandler ---

GetStudentLinkHandler::GetStudentLinkHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string GetStudentLinkHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        return JsonResponse(
            request,
            ToJson(service_.GetStudentLink(
                RequiredPathArg(request, "studentId"))));
    });
}

// --- ListStudentsHandler ---

ListStudentsHandler::ListStudentsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string ListStudentsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        return JsonResponse(
            request,
            ToJsonArray(service_.ListStudents(
                RequiredPathArg(request, "teacherId"))));
    });
}

}  // namespace tutorflow::identity
