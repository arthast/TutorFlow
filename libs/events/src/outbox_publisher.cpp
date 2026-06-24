#include <tutorflow/events/outbox_publisher.hpp>

#include <utility>

#include <userver/formats/json/serialize.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/result_set.hpp>

namespace tutorflow::events {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr std::string_view kTopicPrefix = "tutorflow.";

}  // namespace

std::string TopicForEventType(std::string_view event_type) {
  return std::string{kTopicPrefix} + std::string{event_type};
}

PostgresOutboxPublisher::PostgresOutboxPublisher(
    userver::storages::postgres::ClusterPtr pg,
    const userver::kafka::Producer& producer, std::string task_name,
    std::string producer_name, std::chrono::milliseconds period)
    : pg_(std::move(pg)),
      publisher_(producer),
      task_name_(std::move(task_name)),
      producer_name_(std::move(producer_name)),
      period_(period) {}

void PostgresOutboxPublisher::Start() {
  task_.Start(task_name_,
              userver::utils::PeriodicTask::Settings{period_},
              [this] { PublishPending(); });
}

void PostgresOutboxPublisher::PublishPending() const {
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
    const auto event_type = row["event_type"].As<std::string>();
    const auto topic = TopicForEventType(event_type);
    const auto aggregate_id = row["aggregate_id"].As<std::string>();

    const EventEnvelope event{
        .event_id = id,
        .event_type = event_type,
        .event_version = row["event_version"].As<int>(),
        .occurred_at = row["occurred_at"].As<std::string>(),
        .producer = producer_name_,
        .trace_id = {},
        .payload = userver::formats::json::FromString(
            row["payload"].As<std::string>()),
    };

    // Publish-then-mark: at-least-once. If Send throws, the row stays pending
    // and will be retried on the next tick. Consumers must be idempotent.
    publisher_.Publish(topic, aggregate_id, event);

    pg_->Execute(kMaster,
                 "UPDATE outbox_events SET status = 'published', "
                 "published_at = now() WHERE id = $1::uuid AND status = 'pending'",
                 id);

    LOG_INFO() << "[outbox] published event_id=" << id
               << " type=" << event.event_type << " topic=" << topic;
  }
}

}  // namespace tutorflow::events
