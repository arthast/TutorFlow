#include <tutorflow/events/event_consumer.hpp>

#include <utility>

#include <userver/kafka/message.hpp>

namespace tutorflow::events {

EventConsumer::EventConsumer(userver::kafka::ConsumerComponent& component,
                             Handler handler)
    : handler_(std::move(handler)), scope_(component.GetConsumer()) {}

void EventConsumer::Start() {
  scope_.Start([this](userver::kafka::MessageBatchView batch) {
    for (const auto& message : batch) {
      handler_(ParseString(message.GetPayload()), message.GetKey(),
               message.GetTopic());
    }
    scope_.AsyncCommit();
  });
}

}  // namespace tutorflow::events
