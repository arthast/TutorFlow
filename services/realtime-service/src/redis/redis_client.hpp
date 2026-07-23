#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/redis/client_fwd.hpp>
#include <userver/storages/redis/command_control.hpp>
#include <userver/storages/redis/subscription_token.hpp>
#include <userver/yaml_config/schema.hpp>

namespace tutorflow::realtime {

class ConnectionRegistry;

class RedisClient final : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "realtime-redis";

  RedisClient(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);

  static userver::yaml_config::Schema GetStaticConfigSchema();

  void OnAllComponentsLoaded() override;

  bool RefreshPresence(const std::string& user_id,
                       const std::string& connection_id) const;
  bool ClearPresence(const std::string& user_id,
                     const std::string& connection_id) const;
  void PublishPresence(const std::string& user_id, bool online) const;
  void PublishToUser(const std::string& user_id, const std::string& message) const;
  long long IncrementUnread(const std::string& user_id,
                            const std::string& dialog_id) const;
  void ResetUnread(const std::string& user_id, const std::string& dialog_id) const;
  void CacheDialogParticipants(const std::string& dialog_id,
                               const std::string& teacher_id,
                               const std::string& student_id) const;
  std::optional<std::string> GetDialogPeer(const std::string& dialog_id,
                                           const std::string& user_id) const;
  void AddPeer(const std::string& user_id, const std::string& peer_id) const;
  void Ping() const;

private:
  void OnPubSubMessage(const std::string& channel,
                       const std::string& payload) const;

  int presence_ttl_seconds_{15};
  std::string instance_id_;
  ConnectionRegistry& registry_;
  std::shared_ptr<userver::storages::redis::Client> client_;
  std::shared_ptr<userver::storages::redis::SubscribeClient> subscribe_client_;
  userver::storages::redis::CommandControl command_control_{
      std::chrono::milliseconds{500}, std::chrono::milliseconds{2000}, 4};
  userver::storages::redis::SubscriptionToken subscription_;
};

}  // namespace tutorflow::realtime
