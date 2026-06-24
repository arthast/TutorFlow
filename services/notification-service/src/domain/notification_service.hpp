#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include <tutorflow/common/auth_context.hpp>

#include "domain/models.hpp"

namespace tutorflow::notification {

class NotificationRepository;

class NotificationService final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "notification-domain-service";

  NotificationService(const userver::components::ComponentConfig& config,
                      const userver::components::ComponentContext& context);

  std::vector<Notification> ListNotifications(
      const tutorflow::common::AuthContext& auth, bool unread_only) const;

  Notification MarkAsRead(const tutorflow::common::AuthContext& auth,
                          const std::string& notification_id) const;

  void CreateFromEvent(const CreateNotificationRequest& request) const;

private:
  const NotificationRepository& repository_;
};

}  // namespace tutorflow::notification
