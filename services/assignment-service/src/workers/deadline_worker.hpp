#pragma once

// Авто-просрочка ДЗ (deadline-worker). PeriodicTask одной транзакцией переводит
// ДЗ со статусом 'assigned'/'needs_fix' и истёкшим due_at в 'expired' и пишет
// факт assignment.deadline_expired в outbox (по строке на переход). Идемпотентно:
// после перехода строка уже не попадает в выборку, событие — одно на переход.
// Один инстанс assignment-service + non-overlapping PeriodicTask => без гонок.

#include <chrono>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/schema.hpp>

namespace tutorflow::assignment {

class DeadlineWorker final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "assignment-deadline-worker";

  DeadlineWorker(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  void ExpireOverdue() const;

  userver::storages::postgres::ClusterPtr pg_;
  std::chrono::milliseconds period_;
  userver::utils::PeriodicTask task_;
};

}  // namespace tutorflow::assignment
