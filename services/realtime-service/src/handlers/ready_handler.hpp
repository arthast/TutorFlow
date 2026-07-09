#pragma once

#include <string_view>

#include <userver/server/handlers/http_handler_base.hpp>

#include "redis/redis_client.hpp"

namespace tutorflow::realtime {

class ReadyHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "ready-handler";

  ReadyHandler(const userver::components::ComponentConfig& config,
               const userver::components::ComponentContext& context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest& request,
      userver::server::request::RequestContext& context) const override;

 private:
  const RedisClient& redis_;
};

}  // namespace tutorflow::realtime
