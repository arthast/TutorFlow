#include "outbox/outbox_publisher.hpp"

#include <chrono>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/kafka/producer_component.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/events/event_envelope.hpp>

namespace tutorflow::lesson {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr std::string_view kTopic = "tutorflow.lesson.completed";
constexpr std::string_view kProducerName = "lesson-service";

}  // namespace

OutboxPublisher::OutboxPublisher(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("lesson-db")
              .GetCluster()),
      publisher_(
          context.FindComponent<userver::kafka::ProducerComponent>()
              .GetProducer()) {}

void OutboxPublisher::OnAllComponentsLoaded() {
  task_.Start("lesson-outbox-publisher",
              userver::utils::PeriodicTask::Settings{std::chrono::seconds{1}},
              [this] { PublishPending(); });
}

void OutboxPublisher::PublishPending() const {
  const auto rows = pg_->Execute(
      kMaster,
      R"(SELECT id::text AS id, event_type, event_version,
                aggregate_id::text AS aggregate_id, payload::text AS payload,
                to_char(created_at AT TIME ZONE 'UTC',
                        'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS occurred_at
         FROM outbox_events
         WHERE status = 'pending'
         ORDER BY created_at, id
         LIMIT 100)");

  for (const auto& row : rows) {
    const auto id = row["id"].As<std::string>();
    const tutorflow::events::EventEnvelope event{
        .event_id = id,
        .event_type = row["event_type"].As<std::string>(),
        .event_version = row["event_version"].As<int>(),
        .occurred_at = row["occurred_at"].As<std::string>(),
        .producer = std::string{kProducerName},
        .trace_id = {},
        .payload = userver::formats::json::FromString(
            row["payload"].As<std::string>()),
    };

    // Publish-then-mark: at-least-once. If Send throws, the row stays pending
    // and is retried on the next tick.
    publisher_.Publish(std::string{kTopic},
                       row["aggregate_id"].As<std::string>(), event);

    pg_->Execute(kMaster,
                 "UPDATE outbox_events SET status = 'published', "
                 "published_at = now() WHERE id = $1::uuid AND status = 'pending'",
                 id);

    LOG_INFO() << "[outbox] published event_id=" << id
               << " type=" << event.event_type << " topic=" << kTopic;
  }
}

}  // namespace tutorflow::lesson
