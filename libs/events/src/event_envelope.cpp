#include <tutorflow/events/event_envelope.hpp>

#include <utility>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/utils/datetime.hpp>
#include <userver/utils/uuid4.hpp>

namespace tutorflow::events {
namespace {
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;

constexpr std::string_view kIsoUtcFormat = "%Y-%m-%dT%H:%M:%SZ";

}  // namespace

EventEnvelope MakeEvent(std::string event_type, int event_version,
                        std::string producer, json::Value payload,
                        std::string trace_id) {
  return EventEnvelope{
      .event_id = userver::utils::generators::GenerateUuid(),
      .event_type = std::move(event_type),
      .event_version = event_version,
      .occurred_at = userver::utils::datetime::Timestring(
          userver::utils::datetime::Now(), "UTC", std::string{kIsoUtcFormat}),
      .producer = std::move(producer),
      .trace_id = std::move(trace_id),
      .payload = std::move(payload),
  };
}

json::Value ToJson(const EventEnvelope& event) {
  json::ValueBuilder builder;
  builder["event_id"] = event.event_id;
  builder["event_type"] = event.event_type;
  builder["event_version"] = event.event_version;
  builder["occurred_at"] = event.occurred_at;
  builder["producer"] = event.producer;
  builder["trace_id"] = event.trace_id;
  builder["payload"] =
      event.payload.IsNull()
          ? json::ValueBuilder(common_formats::Type::kObject).ExtractValue()
          : event.payload;
  return builder.ExtractValue();
}

std::string ToJsonString(const EventEnvelope& event) {
  return json::ToString(ToJson(event));
}

EventEnvelope Parse(const json::Value& value) {
  return EventEnvelope{
      .event_id = value["event_id"].As<std::string>(""),
      .event_type = value["event_type"].As<std::string>(""),
      .event_version = value["event_version"].As<int>(1),
      .occurred_at = value["occurred_at"].As<std::string>(""),
      .producer = value["producer"].As<std::string>(""),
      .trace_id = value["trace_id"].As<std::string>(""),
      .payload = value["payload"],
  };
}

EventEnvelope ParseString(std::string_view json_text) {
  return Parse(json::FromString(json_text));
}

}  // namespace tutorflow::events
