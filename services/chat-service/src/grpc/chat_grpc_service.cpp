#include "grpc/chat_grpc_service.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>

#include "domain/models.hpp"

namespace tutorflow::chat {
namespace {
namespace proto = tutorflow::chat::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

proto::Message ToProto(const Message& message) {
  proto::Message response;
  response.set_id(message.id);
  response.set_dialog_id(message.dialog_id);
  response.set_sender_id(message.sender_id);
  response.set_text(message.text);
  for (const auto& file_id : message.file_ids) {
    response.add_file_ids(file_id);
  }
  response.set_created_at(message.created_at);
  return response;
}

proto::Dialog ToProto(const Dialog& dialog) {
  proto::Dialog response;
  response.set_id(dialog.id);
  response.set_teacher_id(dialog.teacher_id);
  response.set_student_id(dialog.student_id);
  response.set_created_at(dialog.created_at);
  if (dialog.last_message_at) {
    response.set_last_message_at(*dialog.last_message_at);
  }
  response.set_unread_count(dialog.unread_count);
  if (dialog.last_message) {
    *response.mutable_last_message() = ToProto(*dialog.last_message);
  }
  return response;
}

proto::ReadMarker ToProto(const ReadMarker& marker) {
  proto::ReadMarker response;
  response.set_dialog_id(marker.dialog_id);
  response.set_user_id(marker.user_id);
  response.set_last_read_message_id(marker.last_read_message_id);
  response.set_last_read_at(marker.last_read_at);
  return response;
}

}  // namespace

ChatGrpcService::ChatGrpcService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : proto::ChatServiceBase::Component(config, context),
      service_(context.FindComponent<ChatService>()) {}

ChatGrpcService::CreateDialogResult ChatGrpcService::CreateDialog(
    CallContext& context, proto::CreateDialogRequest&& request) {
  return InvokeServerUnary<proto::Dialog>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CreateDialog(auth, request.other_user_id()));
  });
}

ChatGrpcService::ListDialogsResult ChatGrpcService::ListDialogs(
    CallContext& context, proto::ListDialogsRequest&& request) {
  return InvokeServerUnary<proto::ListDialogsResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    proto::ListDialogsResponse response;
    for (const auto& dialog : service_.ListDialogs(auth)) {
      *response.add_dialogs() = ToProto(dialog);
    }
    return response;
  });
}

ChatGrpcService::SendMessageResult ChatGrpcService::SendMessage(
    CallContext& context, proto::SendMessageRequest&& request) {
  return InvokeServerUnary<proto::Message>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    std::vector<std::string> file_ids(request.file_ids().begin(),
                                      request.file_ids().end());
    return ToProto(service_.SendMessage(auth, request.dialog_id(),
                                        request.text(), file_ids));
  });
}

ChatGrpcService::ListMessagesResult ChatGrpcService::ListMessages(
    CallContext& context, proto::ListMessagesRequest&& request) {
  return InvokeServerUnary<proto::ListMessagesResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    const auto before = request.has_before()
                            ? std::optional<std::string>{request.before()}
                            : std::nullopt;
    proto::ListMessagesResponse response;
    for (const auto& message :
         service_.ListMessages(auth, request.dialog_id(), before,
                               request.limit())) {
      *response.add_messages() = ToProto(message);
    }
    return response;
  });
}

ChatGrpcService::MarkReadResult ChatGrpcService::MarkRead(
    CallContext& context, proto::MarkReadRequest&& request) {
  return InvokeServerUnary<proto::ReadMarker>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(
        service_.MarkRead(auth, request.dialog_id(), request.up_to_message_id()));
  });
}

}  // namespace tutorflow::chat
