#include "consumers/lesson_completed_consumer.hpp"

#include <userver/components/component_context.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/logging/log.hpp>

#include "domain/finance_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::finance {

LessonCompletedConsumer::LessonCompletedConsumer(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      service_(context.FindComponent<FinanceService>()),
      consumer_(context.FindComponent<userver::kafka::ConsumerComponent>(),
                [this](const tutorflow::events::EventEnvelope& event,
                       std::string_view key, const std::string& topic) {
                  OnLessonCompleted(event, key, topic);
                }) {}

void LessonCompletedConsumer::OnAllComponentsLoaded() { consumer_.Start(); }

void LessonCompletedConsumer::OnLessonCompleted(
    const tutorflow::events::EventEnvelope& event, std::string_view,
    const std::string&) const {
  const auto& payload = event.payload;
  const CreateChargeRequest request{
      .teacher_id = payload["teacher_id"].As<std::string>(),
      .student_id = payload["student_id"].As<std::string>(),
      .lesson_id = payload["lesson_id"].As<std::string>(),
      .amount = payload["price"].As<double>(),
      .currency = payload["currency"].As<std::string>("RUB"),
      .comment = std::string{"Lesson charge"},
  };

  // Idempotent by unique(lesson_id): duplicate event / parallel direct call
  // returns created=false and does not insert a second charge.
  const auto result = service_.CreateCharge(request);

  LOG_INFO() << "[lesson.completed] consumed event_id=" << event.event_id
             << " lesson_id=" << request.lesson_id
             << " charge_created=" << result.created;
}

}  // namespace tutorflow::finance
