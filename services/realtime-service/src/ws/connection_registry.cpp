#include "ws/connection_registry.hpp"

#include <utility>

#include <userver/utils/uuid4.hpp>

namespace tutorflow::realtime {

ConnectionRegistry::ConnectionRegistry(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context) {}

std::shared_ptr<ConnectionState> ConnectionRegistry::Add(
    std::string user_id, std::vector<std::string> roles) {
  auto state = std::make_shared<ConnectionState>();
  state->connection_id = userver::utils::generators::GenerateUuid();
  state->user_id = std::move(user_id);
  state->roles = std::move(roles);
  std::lock_guard lock(mutex_);
  by_user_[state->user_id].insert(state);
  return state;
}

void ConnectionRegistry::Remove(
    const std::shared_ptr<ConnectionState>& connection) {
  if (!connection) return;
  connection->outbound.Close();
  std::lock_guard lock(mutex_);
  auto it = by_user_.find(connection->user_id);
  if (it == by_user_.end()) return;
  it->second.erase(connection);
  if (it->second.empty()) by_user_.erase(it);
}

void ConnectionRegistry::SendToUser(std::string_view user_id,
                                    const std::string& message) const {
  std::vector<std::shared_ptr<ConnectionState>> connections;
  {
    std::lock_guard lock(mutex_);
    auto it = by_user_.find(std::string{user_id});
    if (it == by_user_.end()) return;
    connections.assign(it->second.begin(), it->second.end());
  }
  for (const auto& connection : connections) {
    connection->outbound.Push(message);
  }
}

}  // namespace tutorflow::realtime
