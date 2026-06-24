#include "domain/notification_service.hpp"

#include <userver/components/component_context.hpp>
#include <userver/components/component_config.hpp>

#include <tutorflow/common/errors.hpp>

#include "repositories/notification_repository.hpp"

namespace tutorflow::notification {
namespace {

void RequireUser(const tutorflow::common::AuthContext& auth) {
  if (auth.user_id.empty()) {
    throw tutorflow::common::ServiceError::Unauthorized(
        "missing user context");
  }
}

}  // namespace

NotificationService::NotificationService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<NotificationRepository>()) {}

std::vector<Notification> NotificationService::ListNotifications(
    const tutorflow::common::AuthContext& auth, bool unread_only) const {
  RequireUser(auth);
  return repository_.ListForUser(auth.user_id, unread_only);
}

Notification NotificationService::MarkAsRead(
    const tutorflow::common::AuthContext& auth,
    const std::string& notification_id) const {
  RequireUser(auth);
  if (notification_id.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "notification_id is required");
  }
  return repository_.MarkAsRead(auth.user_id, notification_id);
}

void NotificationService::CreateFromEvent(
    const CreateNotificationRequest& request) const {
  if (request.user_id.empty() || request.source_event_id.empty() ||
      request.source_event_type.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "invalid notification event payload");
  }
  repository_.CreateFromEvent(request);
}

}  // namespace tutorflow::notification
