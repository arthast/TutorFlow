#pragma once

// Thin consumer over a userver Kafka ConsumerScope (Этап 5D). Parses each Kafka
// message payload as an EventEnvelope (JSON) and dispatches it to a handler.
// Idempotency / retry / dead-letter handling is NOT here — that is 5E.

#include <functional>
#include <string>
#include <string_view>

#include <userver/kafka/consumer_component.hpp>
#include <userver/kafka/consumer_scope.hpp>

#include <tutorflow/events/event_envelope.hpp>

namespace tutorflow::events {

class EventConsumer final {
public:
  using Handler = std::function<void(const EventEnvelope& event,
                                     std::string_view key,
                                     const std::string& topic)>;

  // Takes the ConsumerScope from `component` (guaranteed copy elision) and the
  // per-message handler. Construct this as the LAST field of the owning
  // component so polling stops before captured state is destroyed.
  EventConsumer(userver::kafka::ConsumerComponent& component, Handler handler);

  // Subscribes and starts polling. Each polled message is parsed into an
  // EventEnvelope and passed to the handler; the batch is then committed.
  void Start();

private:
  Handler handler_;
  // Subscription must be the last field! Add new fields above this comment.
  userver::kafka::ConsumerScope scope_;
};

}  // namespace tutorflow::events
