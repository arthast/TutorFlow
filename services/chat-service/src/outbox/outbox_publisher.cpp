#include "outbox/outbox_publisher.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/kafka/producer_component.hpp>
#include <userver/storages/postgres/component.hpp>

namespace tutorflow::chat {
namespace {

constexpr std::string_view kProducerName = "chat-service";
constexpr std::array<std::string_view, 2> kDatabaseComponents{
    "chat-db-shard0", "chat-db-shard1"};
constexpr std::array<std::string_view, 2> kTaskNames{
    "chat-outbox-publisher-shard0", "chat-outbox-publisher-shard1"};

}  // namespace

OutboxPublisher::OutboxPublisher(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context) {
  const auto& producer =
      context.FindComponent<userver::kafka::ProducerComponent>().GetProducer();
  auto& statistics_storage =
      context.FindComponent<userver::components::StatisticsStorage>();
  publishers_.reserve(kDatabaseComponents.size());
  for (std::size_t i = 0; i < kDatabaseComponents.size(); ++i) {
    publishers_.push_back(
        std::make_unique<tutorflow::events::PostgresOutboxPublisher>(
            context
                .FindComponent<userver::components::Postgres>(
                    kDatabaseComponents[i])
                .GetCluster(),
            producer, std::string{kTaskNames[i]}, std::string{kProducerName},
            statistics_storage));
  }
}

void OutboxPublisher::OnAllComponentsLoaded() {
  for (auto& publisher : publishers_) publisher->Start();
}

}  // namespace tutorflow::chat
