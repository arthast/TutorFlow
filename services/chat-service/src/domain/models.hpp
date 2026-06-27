#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tutorflow::chat {

struct Message {
  std::string id;
  std::string dialog_id;
  std::string sender_id;
  std::string text;
  std::vector<std::string> file_ids;
  std::string created_at;
};

struct Dialog {
  std::string id;
  std::string teacher_id;
  std::string student_id;
  std::string created_at;
  std::optional<std::string> last_message_at;
  int unread_count{};
  std::optional<Message> last_message;
};

struct ReadMarker {
  std::string dialog_id;
  std::string user_id;
  std::string last_read_message_id;
  std::string last_read_at;
};

}  // namespace tutorflow::chat
