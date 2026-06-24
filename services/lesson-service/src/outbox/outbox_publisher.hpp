#pragma once

// Transactional outbox publisher (Этап 5E-1). Periodically reads `pending`
// outbox_events and publishes them to Kafka via libs/events, then marks them
// `published`. Publish-then-mark = at-least-once (a crash between the two re-
// publishes the row on the next tick; consumers dedup). Single lesson-service
// instance + non-overlapping PeriodicTask => no concurrent publishing.

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/utils/periodic_task.hpp>

#include <tutorflow/events/event_publisher.hpp>

namespace tutorflow::lesson {

class OutboxPublisher final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "lesson-outbox-publisher";

  OutboxPublisher(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

private:
  void PublishPending() const;

  userver::storages::postgres::ClusterPtr pg_;
  tutorflow::events::EventPublisher publisher_;
  userver::utils::PeriodicTask task_;
};

}  // namespace tutorflow::lesson
