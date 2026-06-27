#include "kafka/realtime_event_consumer.hpp"

#include <optional>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/logging/log.hpp>

#include "redis/redis_client.hpp"

namespace tutorflow::realtime {
namespace {
namespace json = userver::formats::json;

std::string RequiredString(const json::Value& payload, std::string_view field) {
  return payload[std::string{field}].As<std::string>();
}

std::string Wrap(std::string_view type, const json::Value& payload) {
  json::ValueBuilder message;
  message["type"] = std::string{type};
  message["payload"] = payload;
  return json::ToString(message.ExtractValue());
}

}  // namespace

RealtimeEventConsumer::RealtimeEventConsumer(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      redis_(context.FindComponent<RedisClient>()),
      consumer_(context.FindComponent<userver::kafka::ConsumerComponent>(),
                [this](const tutorflow::events::EventEnvelope& event,
                       std::string_view, const std::string&) {
                  OnEvent(event);
                }) {}

void RealtimeEventConsumer::OnAllComponentsLoaded() { consumer_.Start(); }

void RealtimeEventConsumer::OnEvent(
    const tutorflow::events::EventEnvelope& event) const {
  const auto& payload = event.payload;

  try {
    if (event.event_type == "message.sent") {
      const auto dialog_id = RequiredString(payload, "dialog_id");
      const auto recipient_id = RequiredString(payload, "recipient_id");
      const auto sender_id = RequiredString(payload, "sender_id");
      const auto teacher_id = RequiredString(payload, "teacher_id");
      const auto student_id = RequiredString(payload, "student_id");

      redis_.CacheDialogParticipants(dialog_id, teacher_id, student_id);
      redis_.AddPeer(sender_id, recipient_id);
      redis_.AddPeer(recipient_id, sender_id);
      const auto unread_count =
          redis_.IncrementUnread(recipient_id, dialog_id);

      json::ValueBuilder push(payload);
      push["unread_count"] = unread_count;
      redis_.PublishToUser(recipient_id,
                           Wrap("chat.message", push.ExtractValue()));
      return;
    }

    if (event.event_type == "message.read") {
      const auto dialog_id = RequiredString(payload, "dialog_id");
      const auto reader_id = RequiredString(payload, "reader_id");
      redis_.ResetUnread(reader_id, dialog_id);
      const auto peer_id = redis_.GetDialogPeer(dialog_id, reader_id);
      if (!peer_id) {
        LOG_WARNING() << "[realtime] no cached dialog participants for read "
                      << "dialog_id=" << dialog_id;
        return;
      }
      redis_.PublishToUser(*peer_id, Wrap("chat.read", payload));
      return;
    }

    if (event.event_type == "notification.created") {
      redis_.PublishToUser(RequiredString(payload, "user_id"),
                           Wrap("notification", payload));
      return;
    }

    LOG_WARNING() << "[realtime] unsupported event type=" << event.event_type;
  } catch (const std::exception& ex) {
    LOG_ERROR() << "[realtime] failed to process event_id=" << event.event_id
                << " type=" << event.event_type << ": " << ex.what();
    throw;
  }
}

}  // namespace tutorflow::realtime
