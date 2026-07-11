#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/ugrpc/server/component_list.hpp>
#include <userver/ugrpc/server/health/component.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "domain/identity_service.hpp"
#include "grpc/identity_grpc_service.hpp"
#include "handlers/ready_handler.hpp"
#include "repositories/identity_repository.hpp"

int main(int argc, char* argv[]) {
    const auto component_list =
        userver::components::MinimalServerComponentList()
            .Append<userver::server::handlers::ServerMonitor>()
            .AppendComponentList(userver::clients::http::ComponentList())
            .AppendComponentList(userver::ugrpc::server::MinimalComponentList())
            .Append<userver::clients::dns::Component>()
            .Append<userver::components::TestsuiteSupport>()
            .Append<userver::components::Postgres>("identity-db")
            .Append<userver::ugrpc::server::HealthComponent>()
            .Append<tutorflow::common::HealthHandler>()
            .Append<tutorflow::identity::ReadyHandler>()
            .Append<tutorflow::identity::IdentityRepository>()
            .Append<tutorflow::identity::IdentityService>()
            .Append<tutorflow::identity::IdentityGrpcService>();
    return userver::utils::DaemonMain(argc, argv, component_list);
}
