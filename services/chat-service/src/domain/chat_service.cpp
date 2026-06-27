#include "domain/chat_service.hpp"

#include <algorithm>

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/identity_grpc_client.hpp>
#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>

#include "repositories/chat_repository.hpp"

namespace tutorflow::chat {
namespace {

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 100;
constexpr std::size_t kPreviewMaxLen = 140;

std::string Trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::optional<std::string> MakePreview(const std::string& text) {
  if (text.empty()) return std::nullopt;
  if (text.size() <= kPreviewMaxLen) return text;
  return text.substr(0, kPreviewMaxLen);
}

// Определяет участников по роли звонящего: teacher -> caller=teacher,
// student -> caller=student. Собеседник — other_user_id.
void ResolvePair(const tutorflow::common::AuthContext& auth,
                 const std::string& other_user_id, std::string& teacher_id,
                 std::string& student_id) {
  if (auth.IsTeacher()) {
    teacher_id = auth.user_id;
    student_id = other_user_id;
  } else if (auth.IsStudent()) {
    student_id = auth.user_id;
    teacher_id = other_user_id;
  } else {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher or student role required");
  }
}

void RequireUser(const tutorflow::common::AuthContext& auth) {
  if (auth.user_id.empty()) {
    throw tutorflow::common::ServiceError::Unauthorized("missing user context");
  }
}

// Проверяет, что звонящий — участник диалога; возвращает участников.
DialogParticipants EnsureParticipant(
    const ChatRepository& repository,
    const tutorflow::common::AuthContext& auth, const std::string& dialog_id) {
  RequireUser(auth);
  if (dialog_id.empty()) {
    throw tutorflow::common::ServiceError::Validation("dialog_id is required");
  }
  auto dialog = repository.FindDialog(dialog_id);
  if (!dialog) {
    throw tutorflow::common::ServiceError::NotFound("dialog not found");
  }
  if (auth.user_id != dialog->teacher_id && auth.user_id != dialog->student_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "not a participant of this dialog");
  }
  return *dialog;
}

}  // namespace

ChatService::ChatService(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<ChatRepository>()),
      identity_(
          context.FindComponent<tutorflow::clients::GrpcIdentityClient>()) {}

Dialog ChatService::CreateDialog(const tutorflow::common::AuthContext& auth,
                                 const std::string& other_user_id) const {
  RequireUser(auth);
  if (other_user_id.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "other_user_id is required");
  }
  if (other_user_id == auth.user_id) {
    throw tutorflow::common::ServiceError::Validation(
        "cannot open a dialog with yourself");
  }

  std::string teacher_id;
  std::string student_id;
  ResolvePair(auth, other_user_id, teacher_id, student_id);

  if (!identity_.CheckAccess(teacher_id, student_id).allowed) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher and student are not linked");
  }

  const auto dialog_id = repository_.FindOrCreateDialogId(teacher_id, student_id);
  auto dialog = repository_.GetDialogForUser(dialog_id, auth.user_id);
  if (!dialog) {
    throw tutorflow::common::ServiceError::Internal("dialog not found after create");
  }
  return *dialog;
}

std::vector<Dialog> ChatService::ListDialogs(
    const tutorflow::common::AuthContext& auth) const {
  RequireUser(auth);
  return repository_.ListDialogsForUser(auth.user_id);
}

Message ChatService::SendMessage(
    const tutorflow::common::AuthContext& auth, const std::string& dialog_id,
    const std::string& text, const std::vector<std::string>& file_ids) const {
  const auto dialog = EnsureParticipant(repository_, auth, dialog_id);
  const auto trimmed = Trim(text);
  if (trimmed.empty() && file_ids.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "message must contain text or attachments");
  }
  const auto& recipient_id = auth.user_id == dialog.teacher_id
                                 ? dialog.student_id
                                 : dialog.teacher_id;
  return repository_.SendMessage(SendMessageParams{
      .dialog_id = dialog_id,
      .sender_id = auth.user_id,
      .recipient_id = recipient_id,
      .teacher_id = dialog.teacher_id,
      .student_id = dialog.student_id,
      .text = trimmed,
      .file_ids = file_ids,
      .preview = MakePreview(trimmed),
      .has_attachments = !file_ids.empty(),
  });
}

std::vector<Message> ChatService::ListMessages(
    const tutorflow::common::AuthContext& auth, const std::string& dialog_id,
    const std::optional<std::string>& before, int limit) const {
  EnsureParticipant(repository_, auth, dialog_id);
  int effective = limit <= 0 ? kDefaultLimit : std::min(limit, kMaxLimit);
  return repository_.ListMessages(dialog_id, before, effective);
}

ReadMarker ChatService::MarkRead(const tutorflow::common::AuthContext& auth,
                                 const std::string& dialog_id,
                                 const std::string& up_to_message_id) const {
  EnsureParticipant(repository_, auth, dialog_id);
  if (up_to_message_id.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "up_to_message_id is required");
  }
  if (!repository_.MessageInDialog(dialog_id, up_to_message_id)) {
    throw tutorflow::common::ServiceError::NotFound("message not found in dialog");
  }
  return repository_.MarkRead(dialog_id, auth.user_id, up_to_message_id);
}

}  // namespace tutorflow::chat
