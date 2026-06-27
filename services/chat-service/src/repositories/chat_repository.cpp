#include "repositories/chat_repository.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::chat {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

// file_id-ы агрегируются через string_agg(',') и разбираются здесь — так не
// зависим от array-IO драйвера. file_id — UUID, запятых не содержит.
std::vector<std::string> SplitCsv(const std::string& value) {
  std::vector<std::string> items;
  std::size_t pos = 0;
  while (pos < value.size()) {
    const auto comma = value.find(',', pos);
    const auto end = comma == std::string::npos ? value.size() : comma;
    if (end > pos) items.push_back(value.substr(pos, end - pos));
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return items;
}

// Поля диалога с обогащением (last_message + unread_count) для конкретного
// пользователя $1 (user_id). Используется и в GetDialogForUser, и в списке.
constexpr std::string_view kDialogSelect = R"(
SELECT
  d.id::text AS id,
  d.teacher_id::text AS teacher_id,
  d.student_id::text AS student_id,
  to_char(d.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
  COALESCE(to_char(d.last_message_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'), '') AS last_message_at,
  (SELECT count(*) FROM messages m
     WHERE m.dialog_id = d.id
       AND m.sender_id <> $1::uuid
       AND m.created_at > COALESCE(
            (SELECT rm_msg.created_at
               FROM read_markers rm
               JOIN messages rm_msg ON rm_msg.id = rm.last_read_message_id
              WHERE rm.dialog_id = d.id AND rm.user_id = $1::uuid),
            '-infinity'::timestamptz))::int AS unread_count,
  COALESCE(lm.id::text, '') AS lm_id,
  COALESCE(lm.sender_id::text, '') AS lm_sender_id,
  COALESCE(lm.text, '') AS lm_text,
  COALESCE(to_char(lm.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'), '') AS lm_created_at,
  COALESCE((SELECT string_agg(a.file_id::text, ',' ORDER BY a.created_at, a.id)
            FROM message_attachments a WHERE a.message_id = lm.id), '') AS lm_file_ids
FROM dialogs d
LEFT JOIN LATERAL (
  SELECT id, sender_id, text, created_at
  FROM messages m WHERE m.dialog_id = d.id
  ORDER BY m.created_at DESC, m.id DESC LIMIT 1
) lm ON TRUE
)";

constexpr std::string_view kMessageSelect = R"(
SELECT m.id::text AS id, m.dialog_id::text AS dialog_id,
       m.sender_id::text AS sender_id, m.text,
       to_char(m.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
       COALESCE((SELECT string_agg(a.file_id::text, ',' ORDER BY a.created_at, a.id)
                 FROM message_attachments a WHERE a.message_id = m.id), '') AS file_ids
FROM messages m
)";

Message RowToMessage(const pg::Row& row) {
  return Message{
      .id = row["id"].As<std::string>(),
      .dialog_id = row["dialog_id"].As<std::string>(),
      .sender_id = row["sender_id"].As<std::string>(),
      .text = row["text"].As<std::string>(),
      .file_ids = SplitCsv(row["file_ids"].As<std::string>()),
      .created_at = row["created_at"].As<std::string>(),
  };
}

std::optional<std::string> EmptyToNull(std::string value) {
  if (value.empty()) return std::nullopt;
  return value;
}

Dialog RowToDialog(const pg::Row& row) {
  Dialog dialog{
      .id = row["id"].As<std::string>(),
      .teacher_id = row["teacher_id"].As<std::string>(),
      .student_id = row["student_id"].As<std::string>(),
      .created_at = row["created_at"].As<std::string>(),
      .last_message_at = EmptyToNull(row["last_message_at"].As<std::string>()),
      .unread_count = row["unread_count"].As<int>(),
      .last_message = std::nullopt,
  };
  const auto lm_id = row["lm_id"].As<std::string>();
  if (!lm_id.empty()) {
    dialog.last_message = Message{
        .id = lm_id,
        .dialog_id = dialog.id,
        .sender_id = row["lm_sender_id"].As<std::string>(),
        .text = row["lm_text"].As<std::string>(),
        .file_ids = SplitCsv(row["lm_file_ids"].As<std::string>()),
        .created_at = row["lm_created_at"].As<std::string>(),
    };
  }
  return dialog;
}

}  // namespace

ChatRepository::ChatRepository(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("chat-db")
              .GetCluster()) {}

std::string ChatRepository::FindOrCreateDialogId(
    const std::string& teacher_id, const std::string& student_id) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH ins AS ("
      "  INSERT INTO dialogs (teacher_id, student_id) "
      "  VALUES ($1::uuid, $2::uuid) "
      "  ON CONFLICT (teacher_id, student_id) DO NOTHING "
      "  RETURNING id"
      ") "
      "SELECT id::text AS id FROM ins "
      "UNION ALL "
      "SELECT id::text AS id FROM dialogs "
      "  WHERE teacher_id = $1::uuid AND student_id = $2::uuid "
      "    AND NOT EXISTS (SELECT 1 FROM ins) "
      "LIMIT 1",
      teacher_id, student_id);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Internal("dialog was not created");
  }
  return result[0]["id"].As<std::string>();
}

std::optional<DialogParticipants> ChatRepository::FindDialog(
    const std::string& dialog_id) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT id::text AS id, teacher_id::text AS teacher_id, "
      "       student_id::text AS student_id "
      "FROM dialogs WHERE id = $1::uuid",
      dialog_id);
  if (result.IsEmpty()) return std::nullopt;
  return DialogParticipants{
      .id = result[0]["id"].As<std::string>(),
      .teacher_id = result[0]["teacher_id"].As<std::string>(),
      .student_id = result[0]["student_id"].As<std::string>(),
  };
}

std::optional<Dialog> ChatRepository::GetDialogForUser(
    const std::string& dialog_id, const std::string& user_id) const {
  const auto result =
      pg_->Execute(kSlave, std::string{kDialogSelect} + " WHERE d.id = $2::uuid",
                   user_id, dialog_id);
  if (result.IsEmpty()) return std::nullopt;
  return RowToDialog(result[0]);
}

std::vector<Dialog> ChatRepository::ListDialogsForUser(
    const std::string& user_id) const {
  const auto result = pg_->Execute(
      kSlave,
      std::string{kDialogSelect} +
          " WHERE d.teacher_id = $1::uuid OR d.student_id = $1::uuid "
          "ORDER BY d.last_message_at DESC NULLS LAST, d.created_at DESC",
      user_id);
  std::vector<Dialog> dialogs;
  dialogs.reserve(result.Size());
  for (const auto& row : result) dialogs.push_back(RowToDialog(row));
  return dialogs;
}

Message ChatRepository::SendMessage(const SendMessageParams& params) const {
  // Одна транзакция: message + attachments + dialogs.last_message_at +
  // message.sent в outbox. Явная транзакция (а не один CTE), чтобы не зависеть
  // от array-параметров для произвольного числа вложений.
  auto trx = pg_->Begin("chat-send-message",
                        userver::storages::postgres::Transaction::RW);

  const auto inserted = trx.Execute(
      "INSERT INTO messages (dialog_id, sender_id, text) "
      "VALUES ($1::uuid, $2::uuid, $3) "
      "RETURNING id::text AS id, "
      "          to_char(created_at AT TIME ZONE 'UTC', "
      "                  'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at",
      params.dialog_id, params.sender_id, params.text);
  const auto message_id = inserted[0]["id"].As<std::string>();
  const auto created_at = inserted[0]["created_at"].As<std::string>();

  for (const auto& file_id : params.file_ids) {
    trx.Execute(
        "INSERT INTO message_attachments (message_id, file_id) "
        "VALUES ($1::uuid, $2::uuid)",
        message_id, file_id);
  }

  trx.Execute("UPDATE dialogs SET last_message_at = now() WHERE id = $1::uuid",
              params.dialog_id);

  trx.Execute(
      "INSERT INTO outbox_events "
      "  (aggregate_type, aggregate_id, event_type, event_version, payload) "
      "VALUES ('dialog', $1::uuid, 'message.sent', 1, "
      "        jsonb_build_object("
      "          'dialog_id', $1::text, 'message_id', $2::text, "
      "          'sender_id', $3::text, 'recipient_id', $4::text, "
      "          'teacher_id', $5::text, 'student_id', $6::text, "
      "          'has_attachments', $7::boolean, 'preview', $8, "
      "          'sent_at', $9))",
      params.dialog_id, message_id, params.sender_id, params.recipient_id,
      params.teacher_id, params.student_id, params.has_attachments,
      params.preview, created_at);

  trx.Commit();

  return Message{
      .id = message_id,
      .dialog_id = params.dialog_id,
      .sender_id = params.sender_id,
      .text = params.text,
      .file_ids = params.file_ids,
      .created_at = created_at,
  };
}

std::vector<Message> ChatRepository::ListMessages(
    const std::string& dialog_id, const std::optional<std::string>& before,
    int limit) const {
  const auto result = pg_->Execute(
      kSlave,
      std::string{kMessageSelect} +
          " WHERE m.dialog_id = $1::uuid "
          "   AND ($2 = '' OR m.created_at < "
          "        (SELECT created_at FROM messages WHERE id = $2::uuid)) "
          "ORDER BY m.created_at DESC, m.id DESC "
          "LIMIT $3",
      dialog_id, before.value_or(""), limit);
  std::vector<Message> messages;
  messages.reserve(result.Size());
  for (const auto& row : result) messages.push_back(RowToMessage(row));
  // Запрос отдаёт новейшие первыми (keyset назад) — возвращаем по возрастанию.
  std::reverse(messages.begin(), messages.end());
  return messages;
}

bool ChatRepository::MessageInDialog(const std::string& dialog_id,
                                     const std::string& message_id) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT 1 FROM messages WHERE id = $1::uuid AND dialog_id = $2::uuid",
      message_id, dialog_id);
  return !result.IsEmpty();
}

ReadMarker ChatRepository::MarkRead(const std::string& dialog_id,
                                    const std::string& user_id,
                                    const std::string& up_to_message_id) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH target AS ("
      "  SELECT id, created_at FROM messages "
      "  WHERE id = $3::uuid AND dialog_id = $1::uuid"
      "), moved AS ("
      "  INSERT INTO read_markers "
      "    (dialog_id, user_id, last_read_message_id, last_read_at, updated_at) "
      "  SELECT $1::uuid, $2::uuid, target.id, now(), now() FROM target "
      "  ON CONFLICT (dialog_id, user_id) DO UPDATE "
      "    SET last_read_message_id = EXCLUDED.last_read_message_id, "
      "        last_read_at = now(), updated_at = now() "
      "  WHERE (SELECT created_at FROM messages "
      "           WHERE id = read_markers.last_read_message_id) "
      "        < (SELECT created_at FROM messages "
      "             WHERE id = EXCLUDED.last_read_message_id) "
      "  RETURNING dialog_id, user_id, last_read_message_id, last_read_at"
      "), outbox AS ("
      "  INSERT INTO outbox_events "
      "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
      "  SELECT 'dialog', $1::uuid, 'message.read', 1, "
      "         jsonb_build_object("
      "           'dialog_id', $1::text, "
      "           'reader_id', $2::text, "
      "           'up_to_message_id', moved.last_read_message_id::text, "
      "           'read_at', to_char(moved.last_read_at AT TIME ZONE 'UTC', "
      "                              'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')) "
      "  FROM moved"
      ") "
      // В одном statement обычный SELECT из read_markers не видит строку,
      // вставленную CTE moved (снэпшот). Поэтому берём результат из moved
      // (RETURNING), а на no-op (указатель не сдвинулся) — из снэпшота таблицы.
      "SELECT result.dialog_id::text AS dialog_id, "
      "       result.user_id::text AS user_id, "
      "       result.last_read_message_id::text AS last_read_message_id, "
      "       to_char(result.last_read_at AT TIME ZONE 'UTC', "
      "               'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS last_read_at "
      "FROM ("
      "  SELECT dialog_id, user_id, last_read_message_id, last_read_at "
      "  FROM moved "
      "  UNION ALL "
      "  SELECT dialog_id, user_id, last_read_message_id, last_read_at "
      "  FROM read_markers "
      "  WHERE dialog_id = $1::uuid AND user_id = $2::uuid "
      "    AND NOT EXISTS (SELECT 1 FROM moved)"
      ") result "
      "LIMIT 1",
      dialog_id, user_id, up_to_message_id);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Internal(
        "read marker result is missing");
  }
  return ReadMarker{
      .dialog_id = result[0]["dialog_id"].As<std::string>(),
      .user_id = result[0]["user_id"].As<std::string>(),
      .last_read_message_id =
          result[0]["last_read_message_id"].As<std::string>(),
      .last_read_at = result[0]["last_read_at"].As<std::string>(),
  };
}

}  // namespace tutorflow::chat
