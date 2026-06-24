#pragma once

#include <string>

#include <userver/formats/json/value.hpp>

namespace tutorflow::notification {

struct Notification {
  std::string id;
  std::string user_id;
  std::string type;
  std::string title;
  std::string body;
  std::string payload_json;
  bool is_read{false};
  std::string created_at;
};

struct CreateNotificationRequest {
  std::string user_id;
  std::string type;
  std::string title;
  std::string body;
  userver::formats::json::Value payload;
  std::string source_event_id;
  std::string source_event_type;
};

}  // namespace tutorflow::notification
