#include "clients/notification_grpc_client.hpp"

#include <chrono>
#include <string>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::notification::v1;

json::Value PayloadFromJsonString(const std::string& payload_json) {
  if (payload_json.empty()) {
    return json::ValueBuilder(common_formats::Type::kObject).ExtractValue();
  }
  return json::FromString(payload_json);
}

json::Value ToJson(const proto::Notification& notification) {
  json::ValueBuilder body;
  body["id"] = notification.id();
  body["user_id"] = notification.user_id();
  body["type"] = notification.type();
  body["title"] = notification.title();
  body["body"] = notification.body();
  body["payload"] = PayloadFromJsonString(notification.payload_json());
  body["is_read"] = notification.is_read();
  body["created_at"] = notification.created_at();
  return body.ExtractValue();
}

}  // namespace

GrpcNotificationClient::GrpcNotificationClient(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::NotificationServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout =
              std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)},
      } {}

json::Value GrpcNotificationClient::ListNotifications(
    bool unread_only,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::ListNotificationsRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_unread_only(unread_only);
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListNotifications(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto& notification : response.notifications()) {
    array.PushBack(ToJson(notification));
  }
  return array.ExtractValue();
}

json::Value GrpcNotificationClient::MarkAsRead(
    std::string_view notification_id,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::MarkAsReadRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_notification_id(std::string{notification_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.MarkAsRead(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

userver::yaml_config::Schema GrpcNotificationClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway notification gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: notification gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

}  // namespace tutorflow::gateway
