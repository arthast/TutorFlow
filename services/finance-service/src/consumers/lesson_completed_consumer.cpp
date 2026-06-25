#include "consumers/lesson_completed_consumer.hpp"

#include <userver/components/component_context.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/logging/log.hpp>

#include "domain/finance_service.hpp"
#include "domain/models.hpp"
#include "repositories/finance_repository.hpp"

namespace tutorflow::finance {

LessonCompletedConsumer::LessonCompletedConsumer(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      service_(context.FindComponent<FinanceService>()),
      repository_(context.FindComponent<FinanceRepository>()),
      consumer_(context.FindComponent<userver::kafka::ConsumerComponent>(),
                [this](const tutorflow::events::EventEnvelope& event,
                       std::string_view key, const std::string& topic) {
                  OnLessonEvent(event, key, topic);
                }) {}

void LessonCompletedConsumer::OnAllComponentsLoaded() { consumer_.Start(); }

void LessonCompletedConsumer::OnLessonEvent(
    const tutorflow::events::EventEnvelope& event, std::string_view,
    const std::string&) const {
  if (event.event_type == "lesson.completed") {
    OnLessonCompleted(event);
  } else if (event.event_type == "lesson.cancelled") {
    OnLessonCancelled(event);
  } else if (event.event_type == "lesson.restored") {
    OnLessonRestored(event);
  } else {
    LOG_WARNING() << "[finance] ignoring unexpected event_type="
                  << event.event_type << " event_id=" << event.event_id;
  }
}

void LessonCompletedConsumer::OnLessonCompleted(
    const tutorflow::events::EventEnvelope& event) const {
  if (repository_.IsEventProcessed(event.event_id)) {
    LOG_INFO() << "[lesson.completed] duplicate event_id=" << event.event_id
               << " skipped by inbox";
    return;
  }

  const auto& payload = event.payload;
  const CreateChargeRequest request{
      .teacher_id = payload["teacher_id"].As<std::string>(),
      .student_id = payload["student_id"].As<std::string>(),
      .lesson_id = payload["lesson_id"].As<std::string>(),
      .amount = payload["price"].As<double>(),
      .currency = payload["currency"].As<std::string>("RUB"),
      .comment = std::string{"Lesson charge"},
  };

  // Idempotent by unique(lesson_id): duplicate events return created=false
  // and do not insert a second charge.
  const auto result = service_.CreateCharge(request);
  repository_.MarkEventProcessed(event.event_id, event.event_type);

  LOG_INFO() << "[lesson.completed] consumed event_id=" << event.event_id
             << " lesson_id=" << request.lesson_id
             << " charge_created=" << result.created;
}

void LessonCompletedConsumer::OnLessonCancelled(
    const tutorflow::events::EventEnvelope& event) const {
  const auto& payload = event.payload;
  const auto previous_status =
      payload["previous_status"].As<std::string>(std::string{});
  // Финансовые последствия есть только при отмене ЗАВЕРШЁННОГО занятия:
  // за него был charge -> нужна компенсирующая correction. Отмена scheduled
  // занятия charge не создавала, компенсировать нечего.
  if (previous_status != "completed") {
    LOG_INFO() << "[lesson.cancelled] no compensation (previous_status="
               << previous_status << ") event_id=" << event.event_id;
    return;
  }

  const auto lesson_id = payload["lesson_id"].As<std::string>();
  const double price = payload["price"].As<double>();
  const CreateCorrectionRequest request{
      .teacher_id = payload["teacher_id"].As<std::string>(),
      .student_id = payload["student_id"].As<std::string>(),
      .lesson_id = lesson_id,
      .amount = -price,  // компенсируем charge: append-only откат
      .currency = payload["currency"].As<std::string>("RUB"),
      .comment = std::string{"charge reversed: lesson cancelled"},
  };

  // Идемпотентно атомарным inbox по event_id: реплей события не создаёт вторую
  // compensation correction и не пишет второй balance.changed.
  const auto created =
      service_.CompensateCancelledLesson(request, event.event_id, event.event_type);

  LOG_INFO() << "[lesson.cancelled] consumed event_id=" << event.event_id
             << " lesson_id=" << lesson_id
             << " correction_created=" << created;
}

void LessonCompletedConsumer::OnLessonRestored(
    const tutorflow::events::EventEnvelope& event) const {
  const auto& payload = event.payload;
  const auto lesson_id = payload["lesson_id"].As<std::string>();
  const double price = payload["price"].As<double>();
  const CreateCorrectionRequest request{
      .teacher_id = payload["teacher_id"].As<std::string>(),
      .student_id = payload["student_id"].As<std::string>(),
      .lesson_id = lesson_id,
      .amount = price,  // возвращаем долг после восстановления completed lesson
      .currency = payload["currency"].As<std::string>("RUB"),
      .comment = std::string{"charge restored: lesson reactivated"},
  };

  const auto created =
      service_.RestoreCancelledLesson(request, event.event_id, event.event_type);

  LOG_INFO() << "[lesson.restored] consumed event_id=" << event.event_id
             << " lesson_id=" << lesson_id
             << " correction_created=" << created;
}

}  // namespace tutorflow::finance
