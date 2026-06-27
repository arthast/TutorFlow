#include "repositories/notification_repository.hpp"

#include <userver/components/component_context.hpp>
#include <userver/components/component_config.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::notification {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

constexpr std::string_view kNotificationFields = R"(
    id::text AS id,
    user_id::text AS user_id,
    type,
    title,
    body,
    payload::text AS payload_json,
    is_read,
    to_char(created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)";

Notification RowToNotification(const pg::Row& row) {
  return Notification{
      .id = row["id"].As<std::string>(),
      .user_id = row["user_id"].As<std::string>(),
      .type = row["type"].As<std::string>(),
      .title = row["title"].As<std::string>(),
      .body = row["body"].As<std::string>(),
      .payload_json = row["payload_json"].As<std::string>(),
      .is_read = row["is_read"].As<bool>(),
      .created_at = row["created_at"].As<std::string>(),
  };
}

}  // namespace

NotificationRepository::NotificationRepository(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("notification-db")
              .GetCluster()) {}

std::vector<Notification> NotificationRepository::ListForUser(
    const std::string& user_id, bool unread_only) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT " + std::string{kNotificationFields} +
          " FROM notifications "
          "WHERE user_id = $1::uuid AND ($2::bool = FALSE OR is_read = FALSE) "
          "ORDER BY created_at DESC, id DESC LIMIT 100",
      user_id, unread_only);

  std::vector<Notification> notifications;
  notifications.reserve(result.Size());
  for (const auto& row : result) {
    notifications.push_back(RowToNotification(row));
  }
  return notifications;
}

Notification NotificationRepository::MarkAsRead(
    const std::string& user_id, const std::string& notification_id) const {
  const auto result = pg_->Execute(
      kMaster,
      "UPDATE notifications SET is_read = TRUE "
      "WHERE id = $1::uuid AND user_id = $2::uuid "
      "RETURNING " +
          std::string{kNotificationFields},
      notification_id, user_id);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::NotFound("notification not found");
  }
  return RowToNotification(result[0]);
}

bool NotificationRepository::IsEventProcessed(const std::string& event_id) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT 1 FROM processed_events WHERE event_id = $1::uuid LIMIT 1",
      event_id);
  return !result.IsEmpty();
}

void NotificationRepository::CreateFromEvent(
    const CreateNotificationRequest& request) const {
  pg_->Execute(
      kMaster,
      R"(
      WITH inserted_notification AS (
        INSERT INTO notifications (
          user_id, type, title, body, payload,
          source_event_id, source_event_type
        )
        VALUES (
          $1::uuid, $2, $3, $4, $5::jsonb,
          $6::uuid, $7
        )
        ON CONFLICT (user_id, source_event_id) DO NOTHING
        RETURNING id, user_id, type, title, body, created_at
      ), inserted_event AS (
        INSERT INTO processed_events (event_id, event_type)
        VALUES ($6::uuid, $7)
        ON CONFLICT (event_id) DO NOTHING
      ), inserted_outbox AS (
        INSERT INTO outbox_events (
          aggregate_type, aggregate_id, event_type, event_version, payload
        )
        SELECT
          'notification',
          inserted_notification.user_id,
          'notification.created',
          1,
          jsonb_build_object(
            'user_id', inserted_notification.user_id::text,
            'notification_id', inserted_notification.id::text,
            'type', inserted_notification.type,
            'title', inserted_notification.title,
            'body', inserted_notification.body,
            'created_at', to_char(
              inserted_notification.created_at AT TIME ZONE 'UTC',
              'YYYY-MM-DD"T"HH24:MI:SS"Z"'
            )
          )
        FROM inserted_notification
      )
      SELECT 1
      )",
      request.user_id, request.type, request.title, request.body,
      userver::formats::json::ToString(request.payload),
      request.source_event_id, request.source_event_type);
}

}  // namespace tutorflow::notification
