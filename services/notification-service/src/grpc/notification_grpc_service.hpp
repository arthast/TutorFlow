#pragma once

#include <tutorflow/notification_service.usrv.pb.hpp>

#include "domain/notification_service.hpp"

namespace tutorflow::notification {

class NotificationGrpcService final
    : public tutorflow::notification::v1::NotificationServiceBase::Component {
public:
  static constexpr std::string_view kName = "notification-grpc-service";

  NotificationGrpcService(
      const userver::components::ComponentConfig& config,
      const userver::components::ComponentContext& context);

  ListNotificationsResult ListNotifications(
      CallContext& context,
      tutorflow::notification::v1::ListNotificationsRequest&& request) override;

  MarkAsReadResult MarkAsRead(
      CallContext& context,
      tutorflow::notification::v1::MarkAsReadRequest&& request) override;

private:
  NotificationService& service_;
};

}  // namespace tutorflow::notification
