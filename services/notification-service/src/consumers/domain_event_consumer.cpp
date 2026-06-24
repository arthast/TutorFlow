#include "consumers/domain_event_consumer.hpp"

#include <optional>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/logging/log.hpp>

#include "domain/models.hpp"
#include "domain/notification_service.hpp"
#include "repositories/notification_repository.hpp"

namespace tutorflow::notification {
namespace {
namespace json = userver::formats::json;

std::string RequiredString(const json::Value& payload, std::string_view field) {
  return payload[std::string{field}].As<std::string>();
}

std::string OptionalString(const json::Value& payload, std::string_view field) {
  return payload[std::string{field}].As<std::string>("");
}

std::string Money(const json::Value& payload) {
  const auto amount = payload["amount"].As<double>(0.0);
  const auto currency = payload["currency"].As<std::string>("RUB");
  return std::to_string(amount) + " " + currency;
}

CreateNotificationRequest NotificationFor(
    const tutorflow::events::EventEnvelope& event, std::string user_id,
    std::string type, std::string title, std::string body) {
  return CreateNotificationRequest{
      .user_id = std::move(user_id),
      .type = std::move(type),
      .title = std::move(title),
      .body = std::move(body),
      .payload = event.payload,
      .source_event_id = event.event_id,
      .source_event_type = event.event_type,
  };
}

std::optional<CreateNotificationRequest> BuildNotification(
    const tutorflow::events::EventEnvelope& event) {
  const auto& payload = event.payload;

  if (event.event_type == "assignment.created") {
    return NotificationFor(
        event, RequiredString(payload, "student_id"), "assignment_created",
        "New assignment",
        "New assignment: " + RequiredString(payload, "title"));
  }

  if (event.event_type == "submission.uploaded") {
    return NotificationFor(
        event, RequiredString(payload, "teacher_id"), "submission_uploaded",
        "Assignment submitted",
        "A student submitted an assignment for review");
  }

  if (event.event_type == "assignment.reviewed") {
    const auto status = RequiredString(payload, "assignment_status");
    return NotificationFor(
        event, RequiredString(payload, "student_id"), "assignment_reviewed",
        "Assignment reviewed", "Assignment review status: " + status);
  }

  if (event.event_type == "payment_receipt.uploaded") {
    return NotificationFor(
        event, RequiredString(payload, "teacher_id"), "payment_receipt_uploaded",
        "Payment receipt uploaded",
        "A student uploaded a payment receipt for " + Money(payload));
  }

  if (event.event_type == "payment.confirmed") {
    return NotificationFor(
        event, RequiredString(payload, "student_id"), "payment_confirmed",
        "Payment confirmed", "Payment confirmed: " + Money(payload));
  }

  if (event.event_type == "payment.rejected") {
    auto body = "Payment receipt rejected";
    const auto comment = OptionalString(payload, "comment");
    return NotificationFor(
        event, RequiredString(payload, "student_id"), "payment_rejected",
        "Payment rejected",
        comment.empty() ? std::string{body} : std::string{body} + ": " + comment);
  }

  if (event.event_type == "lesson.completed") {
    return NotificationFor(
        event, RequiredString(payload, "student_id"), "lesson_completed",
        "Lesson completed",
        "Lesson completed, charge is being processed");
  }

  return std::nullopt;
}

}  // namespace

DomainEventConsumer::DomainEventConsumer(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      service_(context.FindComponent<NotificationService>()),
      repository_(context.FindComponent<NotificationRepository>()),
      consumer_(context.FindComponent<userver::kafka::ConsumerComponent>(),
                [this](const tutorflow::events::EventEnvelope& event,
                       std::string_view key, const std::string& topic) {
                  OnEvent(event, key, topic);
                }) {}

void DomainEventConsumer::OnAllComponentsLoaded() { consumer_.Start(); }

void DomainEventConsumer::OnEvent(const tutorflow::events::EventEnvelope& event,
                                  std::string_view,
                                  const std::string& topic) const {
  if (repository_.IsEventProcessed(event.event_id)) {
    LOG_INFO() << "[notification] duplicate event_id=" << event.event_id
               << " type=" << event.event_type << " skipped";
    return;
  }

  const auto notification = BuildNotification(event);
  if (!notification) {
    LOG_WARNING() << "[notification] unsupported event type=" << event.event_type
                  << " topic=" << topic;
    return;
  }

  service_.CreateFromEvent(*notification);
  LOG_INFO() << "[notification] consumed event_id=" << event.event_id
             << " type=" << event.event_type
             << " user_id=" << notification->user_id;
}

}  // namespace tutorflow::notification
