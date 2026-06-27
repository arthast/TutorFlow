#include "outbox/outbox_publisher.hpp"

#include <string>

#include <userver/components/component_context.hpp>
#include <userver/kafka/producer_component.hpp>
#include <userver/storages/postgres/component.hpp>

namespace tutorflow::chat {
namespace {

constexpr std::string_view kProducerName = "chat-service";
constexpr std::string_view kTaskName = "chat-outbox-publisher";

}  // namespace

OutboxPublisher::OutboxPublisher(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      publisher_(
          context.FindComponent<userver::components::Postgres>("chat-db")
              .GetCluster(),
          context.FindComponent<userver::kafka::ProducerComponent>()
              .GetProducer(),
          std::string{kTaskName}, std::string{kProducerName}) {}

void OutboxPublisher::OnAllComponentsLoaded() { publisher_.Start(); }

}  // namespace tutorflow::chat
