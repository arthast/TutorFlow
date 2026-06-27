#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include <tutorflow/events/event_consumer.hpp>

namespace tutorflow::realtime {

class RedisClient;

class RealtimeEventConsumer final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "realtime-event-consumer";

  RealtimeEventConsumer(const userver::components::ComponentConfig& config,
                        const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

private:
  void OnEvent(const tutorflow::events::EventEnvelope& event) const;

  RedisClient& redis_;
  tutorflow::events::EventConsumer consumer_;
};

}  // namespace tutorflow::realtime
