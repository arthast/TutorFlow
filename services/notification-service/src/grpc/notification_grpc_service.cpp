#include "grpc/notification_grpc_service.hpp"

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>

#include "domain/models.hpp"

namespace tutorflow::notification {
namespace {
namespace proto = tutorflow::notification::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

proto::Notification ToProto(const Notification& notification) {
  proto::Notification response;
  response.set_id(notification.id);
  response.set_user_id(notification.user_id);
  response.set_type(notification.type);
  response.set_title(notification.title);
  response.set_body(notification.body);
  response.set_payload_json(notification.payload_json);
  response.set_is_read(notification.is_read);
  response.set_created_at(notification.created_at);
  return response;
}

}  // namespace

NotificationGrpcService::NotificationGrpcService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : proto::NotificationServiceBase::Component(config, context),
      service_(context.FindComponent<NotificationService>()) {}

NotificationGrpcService::ListNotificationsResult
NotificationGrpcService::ListNotifications(
    CallContext& context, proto::ListNotificationsRequest&& request) {
  return InvokeServerUnary<proto::ListNotificationsResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    proto::ListNotificationsResponse response;
    for (const auto& notification :
         service_.ListNotifications(auth, request.unread_only())) {
      *response.add_notifications() = ToProto(notification);
    }
    return response;
  });
}

NotificationGrpcService::MarkAsReadResult NotificationGrpcService::MarkAsRead(
    CallContext& context, proto::MarkAsReadRequest&& request) {
  return InvokeServerUnary<proto::Notification>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.MarkAsRead(auth, request.notification_id()));
  });
}

}  // namespace tutorflow::notification
