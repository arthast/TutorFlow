#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::notification {

class NotificationRepository final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "notification-repository";

  NotificationRepository(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context);

  std::vector<Notification> ListForUser(const std::string& user_id,
                                        bool unread_only) const;

  Notification MarkAsRead(const std::string& user_id,
                          const std::string& notification_id) const;

  bool IsEventProcessed(const std::string& event_id) const;

  void CreateFromEvent(const CreateNotificationRequest& request) const;

private:
  userver::storages::postgres::ClusterPtr pg_;
};

}  // namespace tutorflow::notification
