#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include <tutorflow/events/outbox_publisher.hpp>

namespace tutorflow::notification {

class OutboxPublisher final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "notification-outbox-publisher";

  OutboxPublisher(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

private:
  tutorflow::events::PostgresOutboxPublisher publisher_;
};

}  // namespace tutorflow::notification
