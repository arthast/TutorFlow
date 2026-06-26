#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/ugrpc/server/component_list.hpp>
#include <userver/ugrpc/server/health/component.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "consumers/domain_event_consumer.hpp"
#include "domain/report_service.hpp"
#include "grpc/report_grpc_service.hpp"
#include "repositories/report_repository.hpp"

int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .AppendComponentList(userver::ugrpc::server::MinimalComponentList())
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::Postgres>("report-db")
          .Append<userver::components::Secdist>()
          .Append<userver::components::DefaultSecdistProvider>()
          .Append<userver::kafka::ConsumerComponent>()
          .Append<userver::ugrpc::server::HealthComponent>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::report::ReportRepository>()
          .Append<tutorflow::report::ReportService>()
          .Append<tutorflow::report::ReportGrpcService>()
          .Append<tutorflow::report::DomainEventConsumer>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
