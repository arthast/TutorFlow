#pragma once

// Versioned event envelope shared by all TutorFlow domain events (Этап 5D).
// Events are JSON (not Protobuf) by roadmap decision. The envelope is transport-
// agnostic: it only depends on userver formats::json, not on Kafka.

#include <string>
#include <string_view>

#include <userver/formats/json/value.hpp>

namespace tutorflow::events {

// Base envelope. `payload` is event-type specific and must be self-contained
// (consumers must not call back into the producer to enrich it).
struct EventEnvelope {
  std::string event_id;       // unique id (uuid v4), for idempotency on 5E
  std::string event_type;     // e.g. "lesson.completed"
  int event_version{1};       // schema version of the payload
  std::string occurred_at;    // ISO-8601 UTC, e.g. 2026-06-24T10:00:00Z
  std::string producer;       // producing service, e.g. "lesson-service"
  std::string trace_id;       // request/trace correlation id (may be empty)
  userver::formats::json::Value payload;
};

// Builds an envelope, generating `event_id` (uuid) and `occurred_at` (now, UTC).
EventEnvelope MakeEvent(std::string event_type, int event_version,
                        std::string producer,
                        userver::formats::json::Value payload,
                        std::string trace_id = {});

userver::formats::json::Value ToJson(const EventEnvelope& event);
std::string ToJsonString(const EventEnvelope& event);

EventEnvelope Parse(const userver::formats::json::Value& value);
EventEnvelope ParseString(std::string_view json);

}  // namespace tutorflow::events
