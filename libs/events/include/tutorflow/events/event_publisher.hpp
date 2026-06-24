#pragma once

// Thin publisher over a userver Kafka producer (Этап 5D). Serializes an
// EventEnvelope to JSON and sends it. No outbox / transactional guarantees here
// (that is 5E) — this is a plain best-effort publish wrapper.

#include <string>
#include <string_view>

#include <userver/kafka/producer.hpp>

#include <tutorflow/events/event_envelope.hpp>

namespace tutorflow::events {

class EventPublisher final {
public:
  explicit EventPublisher(const userver::kafka::Producer& producer);

  // Serializes `event` to JSON and sends it to `topic` under `key`
  // (key is typically the aggregate id for per-aggregate ordering).
  void Publish(const std::string& topic, std::string_view key,
               const EventEnvelope& event) const;

private:
  const userver::kafka::Producer& producer_;
};

}  // namespace tutorflow::events
