#include <tutorflow/events/outbox_publisher.hpp>

#include <stdexcept>
#include <utility>

#include <userver/formats/json/serialize.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/result_set.hpp>
#include <userver/storages/postgres/transaction.hpp>

namespace tutorflow::events {
namespace {
namespace pg = userver::storages::postgres;

constexpr std::string_view kLessonTopic = "tutorflow.lesson.events";
constexpr std::string_view kAssignmentTopic = "tutorflow.assignment.events";
constexpr std::string_view kFinanceTopic = "tutorflow.finance.events";
constexpr std::string_view kChatTopic = "tutorflow.chat.events";
constexpr std::string_view kNotificationTopic = "tutorflow.notification.events";

bool HasPrefix(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

// Ключ advisory-лока лидера outbox-паблишера. Лок берётся в БД сервиса,
// поэтому разные сервисы (разные БД) друг с другом не конкурируют.
constexpr std::string_view kLeaderLockKey = "tutorflow.outbox_publisher";

}  // namespace

std::string TopicForEventType(std::string_view event_type) {
  if (HasPrefix(event_type, "lesson.")) {
    return std::string{kLessonTopic};
  }

  if (HasPrefix(event_type, "assignment.") ||
      event_type == "submission.uploaded") {
    return std::string{kAssignmentTopic};
  }

  if (HasPrefix(event_type, "payment.") ||
      event_type == "payment_receipt.uploaded" ||
      event_type == "charge.created" || event_type == "balance.changed") {
    return std::string{kFinanceTopic};
  }

  if (HasPrefix(event_type, "message.")) {
    return std::string{kChatTopic};
  }

  if (event_type == "notification.created") {
    return std::string{kNotificationTopic};
  }

  throw std::runtime_error{"unknown Kafka event type for topic routing: " +
                           std::string{event_type}};
}

PostgresOutboxPublisher::PostgresOutboxPublisher(
    userver::storages::postgres::ClusterPtr pg,
    const userver::kafka::Producer& producer, std::string task_name,
    std::string producer_name, std::chrono::milliseconds period)
    : pg_(std::move(pg)),
      publisher_(producer),
      task_name_(std::move(task_name)),
      producer_name_(std::move(producer_name)),
      period_(period) {}

void PostgresOutboxPublisher::Start() {
  task_.Start(task_name_,
              userver::utils::PeriodicTask::Settings{period_},
              [this] { PublishPending(); });
}

void PostgresOutboxPublisher::PublishPending() const {
  // Leader-lock: при нескольких репликах сервиса батч публикует ровно одна —
  // остальные молча пропускают tick. Без этого реплики публиковали бы дубли
  // и, что хуже, могли бы нарушить порядок событий одного агрегата
  // (FOR UPDATE SKIP LOCKED дал бы параллелизм, но не сохранил бы порядок —
  // обоснование в docs/adr/0003-service-replicas-and-kafka-scaling.md).
  // Лок транзакционный (pg_try_advisory_xact_lock): упавший лидер отпускает
  // его автоматически вместе с rollback'ом — «зависший» лок невозможен.
  auto trx = pg_->Begin("outbox-publish", pg::Transaction::RW);

  const auto lock = trx.Execute(
      "SELECT pg_try_advisory_xact_lock(hashtext($1)) AS acquired",
      std::string{kLeaderLockKey});
  if (!lock[0]["acquired"].As<bool>()) {
    trx.Rollback();  // лидер уже есть — тихо пропускаем tick
    return;
  }

  const auto rows = trx.Execute(
      R"(SELECT id::text AS id, event_type, event_version,
                aggregate_id::text AS aggregate_id, payload::text AS payload,
                to_char(created_at AT TIME ZONE 'UTC',
                        'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS occurred_at
         FROM outbox_events
         WHERE status = 'pending'
         ORDER BY created_at, id
         LIMIT 100)");

  for (const auto& row : rows) {
    const auto id = row["id"].As<std::string>();
    const auto event_type = row["event_type"].As<std::string>();
    const auto topic = TopicForEventType(event_type);
    const auto aggregate_id = row["aggregate_id"].As<std::string>();

    const EventEnvelope event{
        .event_id = id,
        .event_type = event_type,
        .event_version = row["event_version"].As<int>(),
        .occurred_at = row["occurred_at"].As<std::string>(),
        .producer = producer_name_,
        .trace_id = {},
        .payload = userver::formats::json::FromString(
            row["payload"].As<std::string>()),
    };

    // Publish-then-mark: at-least-once. Если Send бросит исключение,
    // транзакция откатится и весь батч останется pending — уже отправленные
    // события уйдут повторно на следующем tick'е (consumers идемпотентны).
    publisher_.Publish(topic, aggregate_id, event);

    trx.Execute(
        "UPDATE outbox_events SET status = 'published', "
        "published_at = now() WHERE id = $1::uuid AND status = 'pending'",
        id);

    LOG_INFO() << "[outbox] published event_id=" << id
               << " type=" << event.event_type << " topic=" << topic;
  }

  trx.Commit();
}

}  // namespace tutorflow::events
