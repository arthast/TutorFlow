#include "clients/chat_grpc_client.hpp"

#include "clients/json_helpers.hpp"

#include <chrono>
#include <optional>
#include <string>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/handler_helpers.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::chat::v1;

json::Value ToJson(const proto::Message& message) {
  json::ValueBuilder body;
  body["id"] = message.id();
  body["dialog_id"] = message.dialog_id();
  body["sender_id"] = message.sender_id();
  body["text"] = message.text();
  body["file_ids"] = StringArray(message.file_ids());
  body["created_at"] = message.created_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::Dialog& dialog) {
  json::ValueBuilder body;
  body["id"] = dialog.id();
  body["teacher_id"] = dialog.teacher_id();
  body["student_id"] = dialog.student_id();
  body["created_at"] = dialog.created_at();
  // last_message_at — нестрого optional в proto: пусто => null в REST.
  body["last_message_at"] =
      NullableString(!dialog.last_message_at().empty(), dialog.last_message_at());
  body["unread_count"] = dialog.unread_count();
  if (dialog.has_last_message()) {
    body["last_message"] = ToJson(dialog.last_message());
  } else {
    body["last_message"] = json::ValueBuilder(nullptr).ExtractValue();
  }
  return body.ExtractValue();
}

json::Value ToJson(const proto::ReadMarker& marker) {
  json::ValueBuilder body;
  body["dialog_id"] = marker.dialog_id();
  body["user_id"] = marker.user_id();
  body["last_read_message_id"] = marker.last_read_message_id();
  body["last_read_at"] = marker.last_read_at();
  return body.ExtractValue();
}

}  // namespace

GrpcChatClient::GrpcChatClient(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::ChatServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout =
              std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)},
      } {}

json::Value GrpcChatClient::CreateDialog(
    const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::CreateDialogRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_other_user_id(
      tutorflow::common::RequireString(body, "other_user_id"));
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CreateDialog(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcChatClient::ListDialogs(
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::ListDialogsRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListDialogs(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto& dialog : response.dialogs()) {
    array.PushBack(ToJson(dialog));
  }
  return array.ExtractValue();
}

json::Value GrpcChatClient::ListMessages(
    std::string_view dialog_id, const std::optional<std::string>& before,
    const std::optional<std::string>& limit,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::ListMessagesRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_dialog_id(std::string{dialog_id});
  if (before && !before->empty()) {
    request.set_before(*before);
  }
  if (limit && !limit->empty()) {
    try {
      request.set_limit(std::stoi(*limit));
    } catch (const std::exception&) {
      throw tutorflow::common::ServiceError::Validation("invalid limit value");
    }
  }
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListMessages(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto& message : response.messages()) {
    array.PushBack(ToJson(message));
  }
  return array.ExtractValue();
}

json::Value GrpcChatClient::SendMessage(
    std::string_view dialog_id, const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::SendMessageRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_dialog_id(std::string{dialog_id});
  request.set_text(
      tutorflow::common::OptionalString(body, "text").value_or(""));
  for (const auto& file_id : RequireStringArray(body, "file_ids")) {
    request.add_file_ids(file_id);
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.SendMessage(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcChatClient::MarkRead(
    std::string_view dialog_id, const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::MarkReadRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_dialog_id(std::string{dialog_id});
  request.set_up_to_message_id(
      tutorflow::common::RequireString(body, "up_to_message_id"));
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.MarkRead(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

userver::yaml_config::Schema GrpcChatClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway chat gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: chat gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

}  // namespace tutorflow::gateway
