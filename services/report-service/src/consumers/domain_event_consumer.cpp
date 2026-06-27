#include "consumers/domain_event_consumer.hpp"

#include <optional>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/logging/log.hpp>

#include "domain/models.hpp"
#include "repositories/report_repository.hpp"

namespace tutorflow::report {
namespace {
namespace json = userver::formats::json;

std::string RequiredString(const json::Value& payload, std::string_view field) {
  return payload[std::string{field}].As<std::string>();
}

std::string OptionalString(const json::Value& payload, std::string_view field) {
  return payload[std::string{field}].As<std::string>("");
}

double RequiredDouble(const json::Value& payload, std::string_view field) {
  return payload[std::string{field}].As<double>();
}

LessonEvent BuildLessonEvent(const tutorflow::events::EventEnvelope& event) {
  const auto& payload = event.payload;
  LessonEvent model;
  model.lesson_id = RequiredString(payload, "lesson_id");
  model.teacher_id = RequiredString(payload, "teacher_id");
  model.student_id = RequiredString(payload, "student_id");

  if (event.event_type == "lesson.scheduled") {
    model.status = "scheduled";
    model.starts_at = RequiredString(payload, "starts_at");
    model.ends_at = RequiredString(payload, "ends_at");
    model.event_at = RequiredString(payload, "scheduled_at");
  } else if (event.event_type == "lesson.completed") {
    model.status = "completed";
    model.event_at = RequiredString(payload, "completed_at");
  } else if (event.event_type == "lesson.cancelled") {
    model.status = "cancelled";
    model.event_at = RequiredString(payload, "cancelled_at");
  } else if (event.event_type == "lesson.rescheduled") {
    model.status = "scheduled";
    model.starts_at = RequiredString(payload, "new_starts_at");
    model.ends_at = RequiredString(payload, "new_ends_at");
    model.event_at = RequiredString(payload, "rescheduled_at");
  } else if (event.event_type == "lesson.restored") {
    model.status = "completed";
    model.event_at = RequiredString(payload, "restored_at");
  }
  return model;
}

AssignmentEvent BuildAssignmentEvent(
    const tutorflow::events::EventEnvelope& event) {
  const auto& payload = event.payload;
  AssignmentEvent model;
  model.assignment_id = RequiredString(payload, "assignment_id");
  model.teacher_id = RequiredString(payload, "teacher_id");
  model.student_id = RequiredString(payload, "student_id");
  if (event.event_type == "assignment.created") {
    model.status = "assigned";
    model.event_at = RequiredString(payload, "created_at");
  } else if (event.event_type == "submission.uploaded") {
    model.status = RequiredString(payload, "status");
    model.event_at = RequiredString(payload, "submitted_at");
  } else if (event.event_type == "assignment.reviewed") {
    model.status = RequiredString(payload, "assignment_status");
    model.event_at = RequiredString(payload, "reviewed_at");
  } else if (event.event_type == "assignment.deadline_expired") {
    model.status = "expired";
    model.event_at = RequiredString(payload, "expired_at");
  }
  return model;
}

std::optional<BalanceEvent> BuildBalanceEvent(
    const tutorflow::events::EventEnvelope& event) {
  const auto& payload = event.payload;
  if (!payload.HasMember("balance_amount") || payload["balance_amount"].IsNull()) {
    LOG_WARNING() << "[report] legacy balance.changed without balance_amount "
                  << "event_id=" << event.event_id << " skipped";
    return std::nullopt;
  }
  return BalanceEvent{
      .teacher_id = RequiredString(payload, "teacher_id"),
      .student_id = RequiredString(payload, "student_id"),
      .balance_amount = RequiredDouble(payload, "balance_amount"),
      .currency = payload["currency"].As<std::string>("RUB"),
      .changed_at = RequiredString(payload, "changed_at"),
  };
}

ReceiptEvent BuildReceiptEvent(const tutorflow::events::EventEnvelope& event) {
  const auto& payload = event.payload;
  ReceiptEvent model;
  model.receipt_id = RequiredString(payload, "receipt_id");
  model.teacher_id = RequiredString(payload, "teacher_id");
  model.student_id = RequiredString(payload, "student_id");
  model.amount = RequiredDouble(payload, "amount");
  model.currency = payload["currency"].As<std::string>("RUB");
  if (event.event_type == "payment_receipt.uploaded") {
    model.status = OptionalString(payload, "status");
    if (model.status.empty()) model.status = "pending_review";
    model.event_at = RequiredString(payload, "submitted_at");
  } else if (event.event_type == "payment.confirmed") {
    model.status = "confirmed";
    model.event_at = RequiredString(payload, "confirmed_at");
  } else if (event.event_type == "payment.rejected") {
    model.status = "rejected";
    model.event_at = RequiredString(payload, "rejected_at");
  }
  return model;
}

bool IsLessonEvent(std::string_view event_type) {
  return event_type == "lesson.scheduled" ||
         event_type == "lesson.completed" ||
         event_type == "lesson.cancelled" ||
         event_type == "lesson.rescheduled" ||
         event_type == "lesson.restored";
}

bool IsAssignmentEvent(std::string_view event_type) {
  return event_type == "assignment.created" ||
         event_type == "submission.uploaded" ||
         event_type == "assignment.reviewed" ||
         event_type == "assignment.deadline_expired";
}

bool IsReceiptEvent(std::string_view event_type) {
  return event_type == "payment_receipt.uploaded" ||
         event_type == "payment.confirmed" ||
         event_type == "payment.rejected";
}

}  // namespace

DomainEventConsumer::DomainEventConsumer(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<ReportRepository>()),
      consumer_(context.FindComponent<userver::kafka::ConsumerComponent>(),
                [this](const tutorflow::events::EventEnvelope& event,
                       std::string_view key, const std::string& topic) {
                  OnEvent(event, key, topic);
                }) {}

void DomainEventConsumer::OnAllComponentsLoaded() { consumer_.Start(); }

void DomainEventConsumer::OnEvent(const tutorflow::events::EventEnvelope& event,
                                  std::string_view,
                                  const std::string& topic) const {
  bool accepted = false;
  if (IsLessonEvent(event.event_type)) {
    accepted = repository_.ApplyLessonEvent(event.event_id, event.event_type,
                                            BuildLessonEvent(event));
  } else if (IsAssignmentEvent(event.event_type)) {
    accepted = repository_.ApplyAssignmentEvent(
        event.event_id, event.event_type, BuildAssignmentEvent(event));
  } else if (event.event_type == "balance.changed") {
    const auto balance_event = BuildBalanceEvent(event);
    if (!balance_event) {
      return;
    }
    accepted = repository_.ApplyBalanceEvent(event.event_id, event.event_type,
                                             *balance_event);
  } else if (IsReceiptEvent(event.event_type)) {
    accepted = repository_.ApplyReceiptEvent(event.event_id, event.event_type,
                                             BuildReceiptEvent(event));
  } else {
    LOG_WARNING() << "[report] unsupported event type=" << event.event_type
                  << " topic=" << topic;
    return;
  }

  LOG_INFO() << "[report] "
             << (accepted ? "consumed" : "duplicate")
             << " event_id=" << event.event_id
             << " type=" << event.event_type;
}

}  // namespace tutorflow::report
