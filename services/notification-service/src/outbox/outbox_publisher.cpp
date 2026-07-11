#include "outbox/outbox_publisher.hpp"

#include <string>

#include <userver/components/component_context.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/kafka/producer_component.hpp>
#include <userver/storages/postgres/component.hpp>

namespace tutorflow::notification {
namespace {

constexpr std::string_view kProducerName = "notification-service";
constexpr std::string_view kTaskName = "notification-outbox-publisher";

}  // namespace

OutboxPublisher::OutboxPublisher(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      publisher_(context.FindComponent<userver::components::Postgres>(
                     "notification-db")
                     .GetCluster(),
                 context.FindComponent<userver::kafka::ProducerComponent>()
                     .GetProducer(),
                 std::string{kTaskName}, std::string{kProducerName},
                 context.FindComponent<
                     userver::components::StatisticsStorage>()) {}

void OutboxPublisher::OnAllComponentsLoaded() { publisher_.Start(); }

}  // namespace tutorflow::notification
