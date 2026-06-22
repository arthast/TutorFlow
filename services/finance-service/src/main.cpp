#include <userver/clients/http/component_list.hpp>
#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "clients/identity_client.hpp"
#include "domain/finance_service.hpp"
#include "handlers/finance_handlers.hpp"
#include "repositories/finance_repository.hpp"

int main(int argc, char *argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .AppendComponentList(userver::clients::http::ComponentList())
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::components::Postgres>("finance-db")
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::finance::FinanceRepository>()
          .Append<tutorflow::finance::HttpIdentityClient>()
          .Append<tutorflow::finance::FinanceService>()
          .Append<tutorflow::finance::CreateChargeHandler>()
          .Append<tutorflow::finance::GetBalanceHandler>()
          .Append<tutorflow::finance::ListTransactionsHandler>()
          .Append<tutorflow::finance::CreateReceiptHandler>()
          .Append<tutorflow::finance::ListReceiptsHandler>()
          .Append<tutorflow::finance::ConfirmReceiptHandler>()
          .Append<tutorflow::finance::RejectReceiptHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
