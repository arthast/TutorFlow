#include <tutorflow/events/event_publisher.hpp>

namespace tutorflow::events {

EventPublisher::EventPublisher(const userver::kafka::Producer& producer)
    : producer_(producer) {}

void EventPublisher::Publish(const std::string& topic, std::string_view key,
                             const EventEnvelope& event) const {
  producer_.Send(topic, key, ToJsonString(event));
}

}  // namespace tutorflow::events
