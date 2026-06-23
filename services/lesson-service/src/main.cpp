#include <userver/clients/http/component_list.hpp>
#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/component_list.hpp>
#include <userver/ugrpc/server/component_list.hpp>
#include <userver/ugrpc/server/health/component.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>
#include <tutorflow/clients/identity_grpc_client.hpp>

#include "clients/finance_client.hpp"
#include "domain/lesson_service.hpp"
#include "handlers/lesson_handlers.hpp"
#include "repositories/lesson_repository.hpp"

int main(int argc, char *argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .AppendComponentList(userver::clients::http::ComponentList())
          .AppendComponentList(userver::ugrpc::client::MinimalComponentList())
          .AppendComponentList(userver::ugrpc::server::MinimalComponentList())
          .Append<userver::ugrpc::client::ClientFactoryComponent>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::components::Postgres>("lesson-db")
          .Append<userver::ugrpc::server::HealthComponent>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::lesson::LessonRepository>()
          .Append<tutorflow::clients::GrpcIdentityClient>()
          .Append<tutorflow::lesson::HttpFinanceClient>()
          .Append<tutorflow::lesson::LessonService>()
          .Append<tutorflow::lesson::CreateAvailabilityHandler>()
          .Append<tutorflow::lesson::ListAvailabilityHandler>()
          .Append<tutorflow::lesson::CreateLessonHandler>()
          .Append<tutorflow::lesson::ListLessonsHandler>()
          .Append<tutorflow::lesson::GetLessonHandler>()
          .Append<tutorflow::lesson::CompleteLessonHandler>()
          .Append<tutorflow::lesson::CancelLessonHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
