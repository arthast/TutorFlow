#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "clients/finance_client.hpp"
#include "clients/identity_client.hpp"
#include "domain/lesson_service.hpp"
#include "handlers/lesson_handlers.hpp"
#include "repositories/lesson_repository.hpp"

int main(int argc, char *argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::components::Postgres>("lesson-db")
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::lesson::LessonRepository>()
          .Append<tutorflow::lesson::StubIdentityClient>()
          .Append<tutorflow::lesson::StubFinanceClient>()
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
