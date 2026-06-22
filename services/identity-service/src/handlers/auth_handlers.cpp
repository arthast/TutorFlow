#include "handlers/auth_handlers.hpp"

#include <optional>
#include <string>

#include <userver/components/component_context.hpp>
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
    auto value = body[key].As<std::string>("");
    if (value.empty()) {
        throw ServiceError::Validation("field must not be empty: " + key);
    }
    return value;
}

std::optional<std::string> OptionalString(const json::Value& body,
                                          std::string_view field) {
    const std::string key{field};
    if (!body.HasMember(key) || body[key].IsNull()) return std::nullopt;
    auto v = body[key].As<std::string>("");
    if (v.empty()) return std::nullopt;
    return v;
}

RegisterRequest ParseRegisterRequest(const http::HttpRequest& req) {
    const auto body = ParseJsonBody(req);
    return RegisterRequest{
        .email        = RequireString(body, "email"),
        .password     = RequireString(body, "password"),
        .role         = RequireString(body, "role"),
        .display_name = RequireString(body, "display_name"),
        .timezone     = OptionalString(body, "timezone"),
    };
}

LoginRequest ParseLoginRequest(const http::HttpRequest& req) {
    const auto body = ParseJsonBody(req);
    return LoginRequest{
        .email    = RequireString(body, "email"),
        .password = RequireString(body, "password"),
    };
}

ChangePasswordRequest ParseChangePasswordRequest(const http::HttpRequest& req) {
    const auto body = ParseJsonBody(req);
    return ChangePasswordRequest{
        .current_password = RequireString(body, "current_password"),
        .new_password     = RequireString(body, "new_password"),
    };
}

}  // namespace

RegisterHandler::RegisterHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string RegisterHandler::HandleRequestThrow(
    const http::HttpRequest& request, userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        return JsonResponse(request,
                            ToJson(service_.Register(ParseRegisterRequest(request))),
                            http::HttpStatus::kCreated);
    });
}

LoginHandler::LoginHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string LoginHandler::HandleRequestThrow(
    const http::HttpRequest& request, userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        return JsonResponse(request,
                            ToJson(service_.Login(ParseLoginRequest(request))));
    });
}

ChangePasswordHandler::ChangePasswordHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<IdentityService>()) {}

std::string ChangePasswordHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        const auto auth = tutorflow::common::ParseAuthContext(request);
        service_.ChangePassword(auth.user_id,
                                ParseChangePasswordRequest(request));
        json::ValueBuilder body;
        body["status"] = "ok";
        return JsonResponse(request, body.ExtractValue());
    });
}

}  // namespace tutorflow::identity
