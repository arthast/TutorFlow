#pragma once

// Kafka consumer финансовых последствий жизненного цикла занятия:
//   tutorflow.lesson.completed  -> charge (5E), идемпотентно по lesson_id;
//   tutorflow.lesson.cancelled  -> компенсирующая correction при
//     previous_status='completed' (5L.4), идемпотентно по lesson_id.
// Роутинг по event_type. Идемпотентность живёт в domain/DB (unique + inbox),
// не здесь. Дубликат события — no-op.

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
  // Диспетчер по event_type.
  void OnLessonEvent(const tutorflow::events::EventEnvelope& event,
                     std::string_view key, const std::string& topic) const;
  void OnLessonCompleted(const tutorflow::events::EventEnvelope& event) const;
  void OnLessonCancelled(const tutorflow::events::EventEnvelope& event) const;

  const FinanceService& service_;
  const FinanceRepository& repository_;
  // EventConsumer owns the ConsumerScope — keep it last.
  tutorflow::events::EventConsumer consumer_;
};

}  // namespace tutorflow::finance
