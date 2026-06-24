#pragma once

// Shared PostgreSQL transactional outbox publisher (Этап 5F-0). Services own
// their outbox table and transaction boundaries; this helper only polls pending
// rows, publishes EventEnvelope JSON to Kafka, and marks rows as published.

#include <chrono>
#include <string>
#include <string_view>

#include <userver/kafka/producer.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/utils/periodic_task.hpp>

#include <tutorflow/events/event_publisher.hpp>

namespace tutorflow::events {

class PostgresOutboxPublisher final {
public:
  PostgresOutboxPublisher(userver::storages::postgres::ClusterPtr pg,
                          const userver::kafka::Producer& producer,
                          std::string task_name, std::string producer_name,
                          std::chrono::milliseconds period =
                              std::chrono::seconds{1});

  void Start();

private:
  void PublishPending() const;

  userver::storages::postgres::ClusterPtr pg_;
  EventPublisher publisher_;
  std::string task_name_;
  std::string producer_name_;
  std::chrono::milliseconds period_;
  userver::utils::PeriodicTask task_;
};

std::string TopicForEventType(std::string_view event_type);

}  // namespace tutorflow::events
