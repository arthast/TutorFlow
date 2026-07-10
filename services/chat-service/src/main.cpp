#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
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

#include <tutorflow/clients/identity_grpc_client.hpp>
#include <tutorflow/common/health_handler.hpp>

#include "domain/chat_service.hpp"
#include "grpc/chat_grpc_service.hpp"
#include "handlers/ready_handler.hpp"
#include "outbox/outbox_publisher.hpp"
#include "repositories/chat_repository.hpp"

int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .AppendComponentList(userver::ugrpc::client::MinimalComponentList())
          .AppendComponentList(userver::ugrpc::server::MinimalComponentList())
          .Append<userver::ugrpc::client::ClientFactoryComponent>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::components::Postgres>("chat-db-shard0")
          .Append<userver::components::Postgres>("chat-db-shard1")
          .Append<userver::components::Secdist>()
          .Append<userver::components::DefaultSecdistProvider>()
          .Append<userver::kafka::ProducerComponent>()
          .Append<userver::ugrpc::server::HealthComponent>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::chat::ReadyHandler>()
          .Append<tutorflow::chat::ChatRepository>()
          .Append<tutorflow::clients::GrpcIdentityClient>()
          .Append<tutorflow::chat::ChatService>()
          .Append<tutorflow::chat::ChatGrpcService>()
          .Append<tutorflow::chat::OutboxPublisher>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
