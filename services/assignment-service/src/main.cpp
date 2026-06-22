#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "clients/identity_client.hpp"
#include "domain/assignment_service.hpp"
#include "handlers/assignment_handlers.hpp"
#include "repositories/assignment_repository.hpp"

int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::components::Postgres>("assignment-db")
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::assignment::AssignmentRepository>()
          .Append<tutorflow::assignment::StubIdentityClient>()
          .Append<tutorflow::assignment::AssignmentService>()
          .Append<tutorflow::assignment::CreateAssignmentHandler>()
          .Append<tutorflow::assignment::ListAssignmentsHandler>()
          .Append<tutorflow::assignment::GetAssignmentHandler>()
          .Append<tutorflow::assignment::SubmitAssignmentHandler>()
          .Append<tutorflow::assignment::ReviewAssignmentHandler>()
          .Append<tutorflow::assignment::CreateCommentHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
