#pragma once

#include <string_view>

#include <tutorflow/notification_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcNotificationClient final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "gateway-notification-grpc-client";

  GrpcNotificationClient(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context);

  userver::formats::json::Value ListNotifications(
      bool unread_only,
      const tutorflow::clients::GrpcCallContext& call_context) const;

  userver::formats::json::Value MarkAsRead(
      std::string_view notification_id,
      const tutorflow::clients::GrpcCallContext& call_context) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::notification::v1::NotificationServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

}  // namespace tutorflow::gateway
