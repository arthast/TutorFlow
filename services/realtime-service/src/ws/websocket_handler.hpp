#pragma once

#include <string>
#include <string_view>

#include <userver/server/handlers/websocket_handler.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/common/jwt.hpp>

namespace tutorflow::realtime {

class ConnectionRegistry;
class RedisClient;

class RealtimeWebSocketHandler final
    : public userver::server::handlers::WebsocketHandlerBase {
public:
  static constexpr std::string_view kName = "realtime-ws-handler";

  RealtimeWebSocketHandler(
      const userver::components::ComponentConfig& config,
      const userver::components::ComponentContext& context);

  static userver::yaml_config::Schema GetStaticConfigSchema();

  bool HandleHandshake(userver::server::http::HttpRequest& request,
                       userver::server::request::RequestContext& context)
      const override;

  void Handle(userver::websocket::WebSocketConnection& websocket,
              userver::server::request::RequestContext& context) const override;

private:
  std::string jwt_secret_;
  ConnectionRegistry& registry_;
  RedisClient& redis_;
};

}  // namespace tutorflow::realtime
