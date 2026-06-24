#pragma once

// Kafka consumer of tutorflow.lesson.completed (Этап 5E). Creates a charge
// IDEMPOTENTLY by lesson_id (existing unique on financial_transactions). A
// duplicate event is a no-op. Idempotency lives in the domain/DB, not here.

#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include <tutorflow/events/event_consumer.hpp>
#include <tutorflow/events/event_envelope.hpp>

namespace tutorflow::finance {

class FinanceService;
class FinanceRepository;

class LessonCompletedConsumer final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "finance-lesson-completed-consumer";

  LessonCompletedConsumer(const userver::components::ComponentConfig& config,
                          const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

private:
  void OnLessonCompleted(const tutorflow::events::EventEnvelope& event,
                         std::string_view key, const std::string& topic) const;

  const FinanceService& service_;
  const FinanceRepository& repository_;
  // EventConsumer owns the ConsumerScope — keep it last.
  tutorflow::events::EventConsumer consumer_;
};

}  // namespace tutorflow::finance
