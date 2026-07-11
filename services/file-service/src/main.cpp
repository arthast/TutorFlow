#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/component_list.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>
#include <tutorflow/clients/identity_grpc_client.hpp>

#include "domain/file_service.hpp"
#include "handlers/file_handlers.hpp"
#include "handlers/ready_handler.hpp"
#include "repositories/file_repository.hpp"
#include "storages/file_storage.hpp"

int main(int argc, char* argv[]) {
    const auto component_list =
        userver::components::MinimalServerComponentList()
            .Append<userver::server::handlers::ServerMonitor>()
            .AppendComponentList(userver::clients::http::ComponentList())
            .AppendComponentList(userver::ugrpc::client::MinimalComponentList())
            .Append<userver::ugrpc::client::ClientFactoryComponent>()
            .Append<userver::clients::dns::Component>()
            .Append<userver::components::TestsuiteSupport>()
            .Append<userver::components::Postgres>("file-db")
            .Append<userver::components::Secdist>()
            .Append<userver::components::DefaultSecdistProvider>()
            .Append<tutorflow::common::HealthHandler>()
            .Append<tutorflow::file::ReadyHandler>()
            .Append<tutorflow::file::FileRepository>()
            .Append<tutorflow::clients::GrpcIdentityClient>()
            .Append<tutorflow::file::FileStorageComponent>()
            .Append<tutorflow::file::FileService>()
            .Append<tutorflow::file::UploadHandler>()
            .Append<tutorflow::file::GetMetaHandler>()
            .Append<tutorflow::file::DownloadHandler>();
    return userver::utils::DaemonMain(argc, argv, component_list);
}
