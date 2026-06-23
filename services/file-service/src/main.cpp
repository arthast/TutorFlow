#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>
#include <tutorflow/clients/identity_client.hpp>

#include "domain/file_service.hpp"
#include "handlers/file_handlers.hpp"
#include "repositories/file_repository.hpp"

int main(int argc, char* argv[]) {
    const auto component_list =
        userver::components::MinimalServerComponentList()
            .AppendComponentList(userver::clients::http::ComponentList())
            .Append<userver::clients::dns::Component>()
            .Append<userver::components::TestsuiteSupport>()
            .Append<userver::components::Postgres>("file-db")
            .Append<tutorflow::common::HealthHandler>()
            .Append<tutorflow::file::FileRepository>()
            .Append<tutorflow::clients::HttpIdentityClient>()
            .Append<tutorflow::file::FileService>()
            .Append<tutorflow::file::UploadHandler>()
            .Append<tutorflow::file::GetMetaHandler>()
            .Append<tutorflow::file::DownloadHandler>();
    return userver::utils::DaemonMain(argc, argv, component_list);
}
