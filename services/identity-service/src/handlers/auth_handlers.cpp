#include "handlers/auth_handlers.hpp"

#include <optional>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/handler_helpers.hpp>

#include "domain/identity_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::identity {
namespace {
namespace http = userver::server::http;
namespace json = userver::formats::json;
using tutorflow::common::HandleEnvelope;
using tutorflow::common::JsonResponse;
using tutorflow::common::OptionalString;
using tutorflow::common::ParseJsonBody;
using tutorflow::common::RequireString;

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
