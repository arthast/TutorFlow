#include "ws/websocket_handler.hpp"

#include <chrono>
#include <string>
#include <utility>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/engine/deadline.hpp>
#include <userver/engine/task/cancel.hpp>
#include <userver/engine/wait_any.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/request/request_context.hpp>
#include <userver/utils/fast_scope_guard.hpp>
#include <userver/websocket/message.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "redis/redis_client.hpp"
#include "ws/connection_registry.hpp"

namespace tutorflow::realtime {
namespace {
namespace json = userver::formats::json;

constexpr std::string_view kClaimsKey = "realtime.jwt.claims";

std::string JsonMessage(std::string_view type) {
  json::ValueBuilder message;
  message["type"] = std::string{type};
  json::ValueBuilder payload;
  message["payload"] = payload.ExtractValue();
  return json::ToString(message.ExtractValue());
}

bool IsClientPing(const std::string& data) {
  try {
    const auto value = json::FromString(data);
    return value["type"].As<std::string>("") == "ping";
  } catch (...) {
    return false;
  }
}

bool DrainOutbound(ConnectionState& connection,
                   userver::websocket::WebSocketConnection& websocket) {
  std::string message;
  bool drained = false;
  while (connection.outbound.TryPop(message)) {
    websocket.SendText(message);
    drained = true;
  }
  return drained;
}

}  // namespace

RealtimeWebSocketHandler::RealtimeWebSocketHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : WebsocketHandlerBase(config, context),
      jwt_secret_(config["jwt-secret"].As<std::string>()),
      registry_(context.FindComponent<ConnectionRegistry>()),
      redis_(context.FindComponent<RedisClient>()) {}

userver::yaml_config::Schema
RealtimeWebSocketHandler::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::server::handlers::WebsocketHandlerBase>(R"(
type: object
description: realtime WebSocket endpoint
additionalProperties: false
properties:
    jwt-secret:
        type: string
        description: JWT verification secret shared with identity-service and gateway
)");
}

bool RealtimeWebSocketHandler::HandleHandshake(
    userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
  const auto& token = request.GetArg("token");
  if (token.empty()) {
    LOG_WARNING() << "[realtime] websocket rejected: missing token";
    return false;
  }
  auto claims = tutorflow::common::jwt::Verify(token, jwt_secret_);
  if (!claims || claims->sub.empty()) {
    LOG_WARNING() << "[realtime] websocket rejected: invalid token";
    return false;
  }
  context.SetData(std::string{kClaimsKey}, std::move(*claims));
  return true;
}

void RealtimeWebSocketHandler::Handle(
    userver::websocket::WebSocketConnection& websocket,
    userver::server::request::RequestContext& context) const {
  const auto& claims =
      context.GetData<tutorflow::common::jwt::Claims>(kClaimsKey);
  auto connection = registry_.Add(claims.sub, claims.roles);
  auto cleanup = userver::utils::FastScopeGuard([this, &connection]() noexcept {
    registry_.Remove(connection);
    try {
      if (redis_.ClearPresence(connection->user_id,
                               connection->connection_id)) {
        redis_.PublishPresence(connection->user_id, false);
      }
    } catch (const std::exception& ex) {
      LOG_WARNING() << "[realtime] connection cleanup Redis failure user_id="
                    << connection->user_id << " reason=" << ex.what();
    }
  });

  if (redis_.RefreshPresence(connection->user_id,
                             connection->connection_id)) {
    redis_.PublishPresence(connection->user_id, true);
  }

  try {
    auto last_ping_at = std::chrono::steady_clock::now();
    while (true) {
      DrainOutbound(*connection, websocket);

      userver::websocket::Message incoming;
      while (websocket.TryRecv(incoming)) {
        if (incoming.close_status) {
          return;
        }
        if (incoming.is_text && IsClientPing(incoming.data)) {
          if (redis_.RefreshPresence(connection->user_id,
                                     connection->connection_id)) {
            redis_.PublishPresence(connection->user_id, true);
          }
          websocket.SendText(JsonMessage("pong"));
        }
      }

      if (websocket.NotAnsweredSequentialPingsCount() > 4) {
        websocket.Close(userver::websocket::CloseStatus::kGoingAway);
        break;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now - last_ping_at >= std::chrono::seconds{15}) {
        websocket.SendPing();
        last_ping_at = now;
      }

      connection->outbound.ResetSignal();
      if (DrainOutbound(*connection, websocket)) continue;

      auto wait = userver::engine::MakeWaitAny(
          connection->outbound.Signal(), websocket.ReadAwaiter());
      const auto ready = wait.WaitUntil(
          userver::engine::Deadline::FromTimePoint(
              last_ping_at + std::chrono::seconds{15}));
      if (!ready && userver::engine::current_task::ShouldCancel()) return;
    }
  } catch (const std::exception& ex) {
    LOG_INFO() << "[realtime] websocket closed user_id=" << connection->user_id
               << " reason=" << ex.what();
  }
}

}  // namespace tutorflow::realtime
