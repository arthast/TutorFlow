#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

namespace tutorflow::realtime {

struct ConnectionState {
  std::string connection_id;
  std::string user_id;
  std::vector<std::string> roles;
  std::mutex mutex;
  std::deque<std::string> outbound;
  bool closed{false};
};

class ConnectionRegistry final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "realtime-connections";

  ConnectionRegistry(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  std::shared_ptr<ConnectionState> Add(std::string user_id,
                                       std::vector<std::string> roles);
  void Remove(const std::shared_ptr<ConnectionState>& connection);
  void SendToUser(std::string_view user_id, std::string message) const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unordered_set<std::shared_ptr<ConnectionState>>> by_user_;
};

}  // namespace tutorflow::realtime
