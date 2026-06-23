#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "domain/identity_service.hpp"
#include "handlers/auth_handlers.hpp"
#include "handlers/identity_handlers.hpp"
#include "repositories/identity_repository.hpp"

int main(int argc, char* argv[]) {
    const auto component_list =
        userver::components::MinimalServerComponentList()
            .AppendComponentList(userver::clients::http::ComponentList())
            .Append<userver::clients::dns::Component>()
            .Append<userver::components::TestsuiteSupport>()
            .Append<userver::components::Postgres>("identity-db")
            .Append<tutorflow::common::HealthHandler>()
            .Append<tutorflow::identity::IdentityRepository>()
            .Append<tutorflow::identity::IdentityService>()
            .Append<tutorflow::identity::RegisterHandler>()
            .Append<tutorflow::identity::LoginHandler>()
            .Append<tutorflow::identity::ChangePasswordHandler>()
            .Append<tutorflow::identity::GetUserHandler>()
            .Append<tutorflow::identity::CheckAccessHandler>()
            .Append<tutorflow::identity::CreateStudentHandler>()
            .Append<tutorflow::identity::GetStudentLinkHandler>()
            .Append<tutorflow::identity::ListStudentsHandler>();
    return userver::utils::DaemonMain(argc, argv, component_list);
}
