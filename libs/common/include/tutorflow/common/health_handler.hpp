#pragma once

#include <string>

#include <userver/server/handlers/http_handler_base.hpp>

// Общий /health -> {"status":"ok"} (PLAN §15.1). Инфраструктура, одинаковая для
// всех сервисов, поэтому живёт в libs/common. Регистрируется компонентом
// "health-handler" в static_config.yaml каждого сервиса.
namespace tutorflow::common {

class HealthHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "health-handler";

  using HttpHandlerBase::HttpHandlerBase;

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest& request,
      userver::server::request::RequestContext& context) const override;
};

}  // namespace tutorflow::common
