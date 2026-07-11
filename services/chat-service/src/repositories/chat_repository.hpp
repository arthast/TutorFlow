#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include "domain/models.hpp"
#include "repositories/shard_router.hpp"

namespace tutorflow::chat {

// Участники диалога (для проверки доступа и определения получателя).
struct DialogParticipants {
  std::string id;
  std::string teacher_id;
  std::string student_id;
};

// Всё для отправки сообщения одной транзакцией (message + attachments +
// dialogs.last_message_at + message.sent в outbox).
struct SendMessageParams {
  std::string dialog_id;
  std::string sender_id;
  std::string recipient_id;
  std::string teacher_id;
  std::string student_id;
  std::string text;
  std::vector<std::string> file_ids;
  std::optional<std::string> preview;
  bool has_attachments{};
};

class ChatRepository final : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "chat-repository";

  ChatRepository(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

  // UUIDv5 find-or-create; id вычисляется до запроса и сразу задаёт шард.
  std::string FindOrCreateDialogId(const std::string& teacher_id,
                                   const std::string& student_id) const;

  std::optional<DialogParticipants> FindDialog(
      const std::string& dialog_id) const;

  std::optional<Dialog> GetDialogForUser(const std::string& dialog_id,
                                         const std::string& user_id) const;

  std::vector<Dialog> ListDialogsForUser(const std::string& user_id) const;

  Message SendMessage(const SendMessageParams& params) const;

  // По возрастанию created_at. before — message_id-курсор (старше него).
  std::vector<Message> ListMessages(const std::string& dialog_id,
                                    const std::optional<std::string>& before,
                                    int limit) const;

  bool MessageInDialog(const std::string& dialog_id,
                       const std::string& message_id) const;

  // Указатель только вперёд; пишет message.read в outbox при реальном сдвиге.
  ReadMarker MarkRead(const std::string& dialog_id, const std::string& user_id,
                      const std::string& up_to_message_id) const;

 private:
  ShardRouter shard_router_;
};

}  // namespace tutorflow::chat
