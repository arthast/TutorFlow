#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::identity {

class IdentityService;

class RegisterHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "identity-register-handler";

    RegisterHandler(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext&) const override;

private:
    const IdentityService& service_;
};

class LoginHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "identity-login-handler";

    LoginHandler(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext&) const override;

private:
    const IdentityService& service_;
};

class ChangePasswordHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "identity-change-password-handler";

    ChangePasswordHandler(
        const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext&) const override;

private:
    const IdentityService& service_;
};

}  // namespace tutorflow::identity
