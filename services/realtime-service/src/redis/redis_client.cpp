#include "redis/redis_client.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include <unistd.h>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/redis/client.hpp>
#include <userver/storages/redis/component.hpp>
#include <userver/storages/redis/subscribe_client.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "ws/connection_registry.hpp"

namespace tutorflow::realtime {
namespace {

std::string PresenceKey(const std::string& user_id) {
  return "rt:presence:" + user_id;
}

std::string UserChannel(const std::string& user_id) {
  return "rt:user:" + user_id;
}

std::string UnreadKey(const std::string& user_id, const std::string& dialog_id) {
  return "rt:unread:" + user_id + ":" + dialog_id;
}

std::string DialogParticipantsKey(const std::string& dialog_id) {
  return "rt:dialog:" + dialog_id + ":participants";
}

std::string UserPeersKey(const std::string& user_id) {
  return "rt:user_peers:" + user_id;
}

std::string PresenceMessage(const std::string& user_id, bool online) {
  namespace json = userver::formats::json;
  json::ValueBuilder payload;
  payload["user_id"] = user_id;
  payload["online"] = online;
  json::ValueBuilder message;
  message["type"] = "presence";
  message["payload"] = payload.ExtractValue();
  return json::ToString(message.ExtractValue());
}

std::string MakeInstanceId() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::to_string(getpid()) + "-" + std::to_string(now);
}

std::string RedisEnvelope(const std::string& origin,
                          const std::string& payload) {
  namespace json = userver::formats::json;
  json::ValueBuilder envelope;
  envelope["origin"] = origin;
  envelope["payload"] = payload;
  return json::ToString(envelope.ExtractValue());
}

std::pair<std::string, std::string> ParseRedisEnvelope(
    const std::string& raw) {
  try {
    const auto value = userver::formats::json::FromString(raw);
    return {
        value["origin"].As<std::string>(""),
        value["payload"].As<std::string>(raw),
    };
  } catch (const std::exception& ex) {
    LOG_WARNING() << "[realtime] invalid Redis Pub/Sub envelope: "
                  << ex.what();
    return {"", raw};
  }
}

}  // namespace

RedisClient::RedisClient(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      presence_ttl_seconds_(config["presence-ttl-seconds"].As<int>(45)),
      instance_id_(MakeInstanceId()),
      registry_(context.FindComponent<ConnectionRegistry>()) {
  const auto driver_name =
      config["driver-component"].As<std::string>("realtime-redis-driver");
  const auto& redis =
      context.FindComponent<userver::components::Redis>(driver_name);
  client_ = redis.GetClient(
      config["command-client"].As<std::string>("realtime-commands"));
  subscribe_client_ = redis.GetSubscribeClient(
      config["subscribe-client"].As<std::string>("realtime-subscriptions"));
}

void RedisClient::OnAllComponentsLoaded() {
  subscription_ = subscribe_client_->Psubscribe(
      "rt:user:*",
      [this](const std::string&, const std::string& channel,
             const std::string& payload) {
        OnPubSubMessage(channel, payload);
      },
      command_control_);
  subscription_.SetMaxQueueLength(10000);
}

userver::yaml_config::Schema RedisClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: realtime Redis state and fan-out adapter
additionalProperties: false
properties:
    driver-component:
        type: string
        description: userver Redis component name
        defaultDescription: realtime-redis-driver
    command-client:
        type: string
        description: userver Redis command group name
        defaultDescription: realtime-commands
    subscribe-client:
        type: string
        description: userver Redis subscription group name
        defaultDescription: realtime-subscriptions
    presence-ttl-seconds:
        type: integer
        description: presence key TTL in seconds
        defaultDescription: '45'
)");
}

void RedisClient::RefreshPresence(const std::string& user_id) const {
  client_
      ->Setex(PresenceKey(user_id),
              std::chrono::seconds{presence_ttl_seconds_}, "1",
              command_control_)
      .Get();
}

void RedisClient::ClearPresence(const std::string& user_id) const {
  client_->Del(PresenceKey(user_id), command_control_).Get();
}

void RedisClient::PublishPresence(const std::string& user_id,
                                  bool online) const {
  const auto message = PresenceMessage(user_id, online);
  const auto peers =
      client_->Smembers(UserPeersKey(user_id), command_control_).Get();
  for (const auto& peer_id : peers) {
    PublishToUser(peer_id, message);
  }
}

void RedisClient::PublishToUser(const std::string& user_id,
                                const std::string& message) const {
  registry_.SendToUser(user_id, message);
  client_->Publish(UserChannel(user_id), RedisEnvelope(instance_id_, message),
                   command_control_);
}

long long RedisClient::IncrementUnread(const std::string& user_id,
                                       const std::string& dialog_id) const {
  const auto key = UnreadKey(user_id, dialog_id);
  const auto value = client_->Incr(key, command_control_).Get();
  client_->Expire(key, std::chrono::seconds{86400}, command_control_).Get();
  return value;
}

void RedisClient::ResetUnread(const std::string& user_id,
                              const std::string& dialog_id) const {
  client_->Del(UnreadKey(user_id, dialog_id), command_control_).Get();
}

void RedisClient::CacheDialogParticipants(const std::string& dialog_id,
                                          const std::string& teacher_id,
                                          const std::string& student_id) const {
  client_
      ->Setex(DialogParticipantsKey(dialog_id), std::chrono::seconds{86400},
              teacher_id + ":" + student_id, command_control_)
      .Get();
}

std::optional<std::string> RedisClient::GetDialogPeer(
    const std::string& dialog_id, const std::string& user_id) const {
  const auto value =
      client_->Get(DialogParticipantsKey(dialog_id), command_control_).Get();
  if (!value) return std::nullopt;
  const auto colon = value->find(':');
  if (colon == std::string::npos) return std::nullopt;
  const auto first = value->substr(0, colon);
  const auto second = value->substr(colon + 1);
  if (first == user_id) return second;
  if (second == user_id) return first;
  return std::nullopt;
}

void RedisClient::AddPeer(const std::string& user_id,
                          const std::string& peer_id) const {
  const auto key = UserPeersKey(user_id);
  client_->Sadd(key, peer_id, command_control_).Get();
  client_->Expire(key, std::chrono::seconds{86400}, command_control_).Get();
}

void RedisClient::Ping() const {
  client_->Ping(0, command_control_).Get();
}

void RedisClient::OnPubSubMessage(const std::string& channel,
                                  const std::string& payload) const {
  constexpr std::string_view prefix = "rt:user:";
  if (channel.rfind(prefix, 0) != 0) return;

  const auto [origin, message] = ParseRedisEnvelope(payload);
  if (origin == instance_id_) return;
  registry_.SendToUser(channel.substr(prefix.size()), message);
}

}  // namespace tutorflow::realtime
