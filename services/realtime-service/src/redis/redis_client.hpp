#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/yaml_config/schema.hpp>

namespace tutorflow::realtime {

class ConnectionRegistry;

class RedisClient final : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "realtime-redis";

  struct RedisEndpoint {
    std::string host;
    int port{6379};
  };

  RedisClient(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);
  ~RedisClient() override;

  static userver::yaml_config::Schema GetStaticConfigSchema();

  void OnAllComponentsLoaded() override;

  void RefreshPresence(const std::string& user_id) const;
  void ClearPresence(const std::string& user_id) const;
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
  static RedisEndpoint ParseUrl(std::string url);
  void PubSubLoop();
  void Command(std::vector<std::string> args) const;
  long long IntegerCommand(std::vector<std::string> args) const;
  std::optional<std::string> StringCommand(std::vector<std::string> args) const;
  std::vector<std::string> ArrayCommand(std::vector<std::string> args) const;

  RedisEndpoint endpoint_;
  int presence_ttl_seconds_{45};
  std::string instance_id_;
  ConnectionRegistry& registry_;
  std::atomic<bool> running_{false};
  std::thread pubsub_thread_;
};

}  // namespace tutorflow::realtime
