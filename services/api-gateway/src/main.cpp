#include <userver/components/minimal_server_component_list.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

// Скелет сервиса (foundation): поднимает HTTP-сервер и отдаёт GET /health.
// Gateway — тонкий: auth (валидация JWT), срез/проставление X-User-*, роутинг
// во внутренние сервисы. Без бизнес-логики (PLAN §10). Добавляет владелец.
int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<tutorflow::common::HealthHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
