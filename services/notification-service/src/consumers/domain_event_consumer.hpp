#pragma once

#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include <tutorflow/events/event_consumer.hpp>
#include <tutorflow/events/event_envelope.hpp>

namespace tutorflow::notification {

class NotificationRepository;
class NotificationService;

class DomainEventConsumer final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "notification-domain-event-consumer";

  DomainEventConsumer(const userver::components::ComponentConfig& config,
                      const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

private:
  void OnEvent(const tutorflow::events::EventEnvelope& event,
               std::string_view key, const std::string& topic) const;

  const NotificationService& service_;
  const NotificationRepository& repository_;
  // EventConsumer owns the ConsumerScope — keep it last.
  tutorflow::events::EventConsumer consumer_;
};

}  // namespace tutorflow::notification
