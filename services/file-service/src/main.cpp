#include <userver/components/minimal_server_component_list.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

// Скелет сервиса (foundation): поднимает HTTP-сервер и отдаёт GET /health.
// Доменные компоненты (postgres-database, handlers, http-client) добавляет
// владелец сервиса при реализации — см. docs/PLAN.md §3, §8.
int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<tutorflow::common::HealthHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
