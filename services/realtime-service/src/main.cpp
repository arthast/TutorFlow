#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/kafka/consumer_component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "handlers/ready_handler.hpp"
#include "kafka/realtime_event_consumer.hpp"
#include "redis/redis_client.hpp"
#include "ws/connection_registry.hpp"
#include "ws/websocket_handler.hpp"

int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<userver::server::handlers::ServerMonitor>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::Secdist>()
          .Append<userver::components::DefaultSecdistProvider>()
          .Append<userver::kafka::ConsumerComponent>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::realtime::ReadyHandler>()
          .Append<tutorflow::realtime::ConnectionRegistry>()
          .Append<tutorflow::realtime::RedisClient>()
          .Append<tutorflow::realtime::RealtimeEventConsumer>()
          .Append<tutorflow::realtime::RealtimeWebSocketHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
