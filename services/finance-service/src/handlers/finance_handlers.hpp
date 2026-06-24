#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::finance {

class FinanceService;

// Only internal REST endpoint kept after 5C: lesson-service pushes lesson
// charges here (POST /internal/charges). Moves to Kafka on 5E.
class CreateChargeHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-create-charge-handler";

  CreateChargeHandler(const userver::components::ComponentConfig &config,
                      const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

} // namespace tutorflow::finance
