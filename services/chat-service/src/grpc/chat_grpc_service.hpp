#pragma once

#include <tutorflow/chat_service.usrv.pb.hpp>

#include "domain/chat_service.hpp"

namespace tutorflow::chat {

class ChatGrpcService final
    : public tutorflow::chat::v1::ChatServiceBase::Component {
 public:
  static constexpr std::string_view kName = "chat-grpc-service";

  ChatGrpcService(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  CreateDialogResult CreateDialog(
      CallContext& context,
      tutorflow::chat::v1::CreateDialogRequest&& request) override;
  ListDialogsResult ListDialogs(
      CallContext& context,
      tutorflow::chat::v1::ListDialogsRequest&& request) override;
  SendMessageResult SendMessage(
      CallContext& context,
      tutorflow::chat::v1::SendMessageRequest&& request) override;
  ListMessagesResult ListMessages(
      CallContext& context,
      tutorflow::chat::v1::ListMessagesRequest&& request) override;
  MarkReadResult MarkRead(CallContext& context,
                          tutorflow::chat::v1::MarkReadRequest&& request) override;

 private:
  ChatService& service_;
};

}  // namespace tutorflow::chat
