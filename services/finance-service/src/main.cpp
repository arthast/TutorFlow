#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/kafka/producer_component.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/component_list.hpp>
#include <userver/ugrpc/server/component_list.hpp>
#include <userver/ugrpc/server/health/component.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>
#include <tutorflow/clients/identity_grpc_client.hpp>

#include "consumers/lesson_completed_consumer.hpp"
#include "domain/finance_service.hpp"
#include "grpc/finance_grpc_service.hpp"
#include "handlers/ready_handler.hpp"
#include "outbox/outbox_publisher.hpp"
#include "repositories/finance_repository.hpp"

int main(int argc, char *argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<userver::server::handlers::ServerMonitor>()
          .AppendComponentList(userver::ugrpc::client::MinimalComponentList())
          .AppendComponentList(userver::ugrpc::server::MinimalComponentList())
          .Append<userver::ugrpc::client::ClientFactoryComponent>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::components::Postgres>("finance-db")
          .Append<userver::components::Secdist>()
          .Append<userver::components::DefaultSecdistProvider>()
          .Append<userver::kafka::ProducerComponent>()
          .Append<userver::kafka::ConsumerComponent>()
          .Append<userver::ugrpc::server::HealthComponent>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::finance::ReadyHandler>()
          .Append<tutorflow::finance::FinanceRepository>()
          .Append<tutorflow::clients::GrpcIdentityClient>()
          .Append<tutorflow::finance::FinanceService>()
          .Append<tutorflow::finance::FinanceGrpcService>()
          .Append<tutorflow::finance::LessonCompletedConsumer>()
          .Append<tutorflow::finance::OutboxPublisher>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
