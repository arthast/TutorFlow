#include "redis/redis_client.hpp"

#include <cstdlib>
#include <chrono>
#include <sstream>
#include <stdexcept>

#include <unistd.h>

#include <hiredis/hiredis.h>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
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
  } catch (...) {
    return {"", raw};
  }
}

redisContext* Connect(const RedisClient::RedisEndpoint& endpoint) {
  timeval timeout{1, 0};
  auto* context =
      redisConnectWithTimeout(endpoint.host.c_str(), endpoint.port, timeout);
  if (!context || context->err) {
    std::string error = context ? context->errstr : "allocation failed";
    if (context) redisFree(context);
    throw std::runtime_error("redis connect failed: " + error);
  }
  redisSetTimeout(context, timeout);
  return context;
}

redisReply* Run(redisContext* context, const std::vector<std::string>& args) {
  std::vector<const char*> argv;
  std::vector<size_t> argvlen;
  argv.reserve(args.size());
  argvlen.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(arg.c_str());
    argvlen.push_back(arg.size());
  }
  return static_cast<redisReply*>(
      redisCommandArgv(context, static_cast<int>(argv.size()), argv.data(),
                       argvlen.data()));
}

}  // namespace

RedisClient::RedisClient(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      endpoint_(ParseUrl(config["url"].As<std::string>("redis://redis:6379"))),
      presence_ttl_seconds_(config["presence-ttl-seconds"].As<int>(45)),
      instance_id_(MakeInstanceId()),
      registry_(context.FindComponent<ConnectionRegistry>()) {}

RedisClient::~RedisClient() {
  running_ = false;
  if (pubsub_thread_.joinable()) pubsub_thread_.join();
}

void RedisClient::OnAllComponentsLoaded() {
  running_ = true;
  pubsub_thread_ = std::thread([this] { PubSubLoop(); });
}

userver::yaml_config::Schema RedisClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: realtime Redis state and fan-out client
additionalProperties: false
properties:
    url:
        type: string
        description: Redis URL, for example redis://redis:6379
        defaultDescription: redis://redis:6379
    presence-ttl-seconds:
        type: integer
        description: presence key TTL in seconds
        defaultDescription: '45'
)");
}

RedisClient::RedisEndpoint RedisClient::ParseUrl(std::string url) {
  constexpr std::string_view prefix = "redis://";
  if (url.rfind(prefix, 0) == 0) url.erase(0, prefix.size());
  const auto slash = url.find('/');
  if (slash != std::string::npos) url.erase(slash);
  RedisEndpoint endpoint;
  const auto colon = url.rfind(':');
  if (colon == std::string::npos) {
    endpoint.host = url.empty() ? "redis" : url;
    return endpoint;
  }
  endpoint.host = url.substr(0, colon);
  endpoint.port = std::stoi(url.substr(colon + 1));
  return endpoint;
}

void RedisClient::Command(std::vector<std::string> args) const {
  auto* context = Connect(endpoint_);
  auto* reply = Run(context, args);
  if (!reply) {
    std::string error = context->errstr;
    redisFree(context);
    throw std::runtime_error("redis command failed: " + error);
  }
  freeReplyObject(reply);
  redisFree(context);
}

long long RedisClient::IntegerCommand(std::vector<std::string> args) const {
  auto* context = Connect(endpoint_);
  auto* reply = Run(context, args);
  if (!reply) {
    std::string error = context->errstr;
    redisFree(context);
    throw std::runtime_error("redis integer command failed: " + error);
  }
  const auto value = reply->type == REDIS_REPLY_INTEGER ? reply->integer : 0;
  freeReplyObject(reply);
  redisFree(context);
  return value;
}

std::optional<std::string> RedisClient::StringCommand(
    std::vector<std::string> args) const {
  auto* context = Connect(endpoint_);
  auto* reply = Run(context, args);
  if (!reply) {
    std::string error = context->errstr;
    redisFree(context);
    throw std::runtime_error("redis string command failed: " + error);
  }
  std::optional<std::string> value;
  if (reply->type == REDIS_REPLY_STRING) {
    value = std::string(reply->str, reply->len);
  }
  freeReplyObject(reply);
  redisFree(context);
  return value;
}

std::vector<std::string> RedisClient::ArrayCommand(
    std::vector<std::string> args) const {
  auto* context = Connect(endpoint_);
  auto* reply = Run(context, args);
  if (!reply) {
    std::string error = context->errstr;
    redisFree(context);
    throw std::runtime_error("redis array command failed: " + error);
  }
  std::vector<std::string> values;
  if (reply->type == REDIS_REPLY_ARRAY) {
    values.reserve(reply->elements);
    for (std::size_t i = 0; i < reply->elements; ++i) {
      const auto* item = reply->element[i];
      if (item && item->type == REDIS_REPLY_STRING) {
        values.emplace_back(item->str, item->len);
      }
    }
  }
  freeReplyObject(reply);
  redisFree(context);
  return values;
}

void RedisClient::RefreshPresence(const std::string& user_id) const {
  Command({"SETEX", PresenceKey(user_id), std::to_string(presence_ttl_seconds_),
           "1"});
}

void RedisClient::ClearPresence(const std::string& user_id) const {
  Command({"DEL", PresenceKey(user_id)});
}

void RedisClient::PublishPresence(const std::string& user_id,
                                  bool online) const {
  const auto message = PresenceMessage(user_id, online);
  for (const auto& peer_id : ArrayCommand({"SMEMBERS", UserPeersKey(user_id)})) {
    PublishToUser(peer_id, message);
  }
}

void RedisClient::PublishToUser(const std::string& user_id,
                                const std::string& message) const {
  registry_.SendToUser(user_id, message);
  Command({"PUBLISH", UserChannel(user_id),
           RedisEnvelope(instance_id_, message)});
}

long long RedisClient::IncrementUnread(const std::string& user_id,
                                       const std::string& dialog_id) const {
  const auto key = UnreadKey(user_id, dialog_id);
  const auto value = IntegerCommand({"INCR", key});
  Command({"EXPIRE", key, "86400"});
  return value;
}

void RedisClient::ResetUnread(const std::string& user_id,
                              const std::string& dialog_id) const {
  Command({"DEL", UnreadKey(user_id, dialog_id)});
}

void RedisClient::CacheDialogParticipants(const std::string& dialog_id,
                                          const std::string& teacher_id,
                                          const std::string& student_id) const {
  Command({"SETEX", DialogParticipantsKey(dialog_id), "86400",
           teacher_id + ":" + student_id});
}

std::optional<std::string> RedisClient::GetDialogPeer(
    const std::string& dialog_id, const std::string& user_id) const {
  const auto value = StringCommand({"GET", DialogParticipantsKey(dialog_id)});
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
  Command({"SADD", UserPeersKey(user_id), peer_id});
  Command({"EXPIRE", UserPeersKey(user_id), "86400"});
}

void RedisClient::PubSubLoop() {
  while (running_) {
    try {
      auto* context = Connect(endpoint_);
      auto* reply = static_cast<redisReply*>(
          redisCommand(context, "PSUBSCRIBE rt:user:*"));
      if (reply) freeReplyObject(reply);

      while (running_) {
        void* raw_reply = nullptr;
        const auto status = redisGetReply(context, &raw_reply);
        if (status != REDIS_OK) break;
        auto* message = static_cast<redisReply*>(raw_reply);
        if (!message) continue;
        if (message->type == REDIS_REPLY_ARRAY && message->elements == 4) {
          const auto* channel = message->element[2];
          const auto* payload = message->element[3];
          if (channel && payload && channel->type == REDIS_REPLY_STRING &&
              payload->type == REDIS_REPLY_STRING) {
            const std::string channel_value(channel->str, channel->len);
            const std::string prefix = "rt:user:";
            if (channel_value.rfind(prefix, 0) == 0) {
              const auto [origin, message] =
                  ParseRedisEnvelope(std::string(payload->str, payload->len));
              if (origin != instance_id_) {
                registry_.SendToUser(channel_value.substr(prefix.size()),
                                     message);
              }
            }
          }
        }
        freeReplyObject(message);
      }
      redisFree(context);
    } catch (const std::exception& ex) {
      LOG_WARNING() << "[realtime] redis pubsub reconnect after error: "
                    << ex.what();
    }
    std::this_thread::sleep_for(std::chrono::seconds{1});
  }
}

}  // namespace tutorflow::realtime
