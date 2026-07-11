#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/kafka/producer_component.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/ugrpc/server/component_list.hpp>
#include <userver/ugrpc/server/health/component.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "consumers/domain_event_consumer.hpp"
#include "domain/notification_service.hpp"
#include "grpc/notification_grpc_service.hpp"
#include "handlers/ready_handler.hpp"
#include "outbox/outbox_publisher.hpp"
#include "repositories/notification_repository.hpp"

int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<userver::server::handlers::ServerMonitor>()
          .AppendComponentList(userver::ugrpc::server::MinimalComponentList())
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::Postgres>("notification-db")
          .Append<userver::components::Secdist>()
          .Append<userver::components::DefaultSecdistProvider>()
          .Append<userver::kafka::ConsumerComponent>()
          .Append<userver::kafka::ProducerComponent>()
          .Append<userver::ugrpc::server::HealthComponent>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::notification::ReadyHandler>()
          .Append<tutorflow::notification::NotificationRepository>()
          .Append<tutorflow::notification::NotificationService>()
          .Append<tutorflow::notification::NotificationGrpcService>()
          .Append<tutorflow::notification::DomainEventConsumer>()
          .Append<tutorflow::notification::OutboxPublisher>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
