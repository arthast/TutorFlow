#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <tutorflow/common/health_handler.hpp>

#include "gateway_settings.hpp"
#include "handlers/proxy_handlers.hpp"

int main(int argc, char* argv[]) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .AppendComponentList(userver::clients::http::ComponentList())
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::TestsuiteSupport>()
          .Append<tutorflow::common::HealthHandler>()
          .Append<tutorflow::gateway::GatewaySettings>()
          .Append<tutorflow::gateway::AuthRegisterHandler>()
          .Append<tutorflow::gateway::AuthLoginHandler>()
          .Append<tutorflow::gateway::AuthChangePasswordHandler>()
          .Append<tutorflow::gateway::MeHandler>()
          .Append<tutorflow::gateway::StudentsHandler>()
          .Append<tutorflow::gateway::StudentHandler>()
          .Append<tutorflow::gateway::StudentBalanceHandler>()
          .Append<tutorflow::gateway::StudentTransactionsHandler>()
          .Append<tutorflow::gateway::AvailabilityHandler>()
          .Append<tutorflow::gateway::LessonsHandler>()
          .Append<tutorflow::gateway::LessonHandler>()
          .Append<tutorflow::gateway::LessonCompleteHandler>()
          .Append<tutorflow::gateway::LessonCancelHandler>()
          .Append<tutorflow::gateway::AssignmentsHandler>()
          .Append<tutorflow::gateway::AssignmentHandler>()
          .Append<tutorflow::gateway::AssignmentSubmitHandler>()
          .Append<tutorflow::gateway::AssignmentReviewHandler>()
          .Append<tutorflow::gateway::AssignmentCommentsHandler>()
          .Append<tutorflow::gateway::PaymentReceiptsHandler>()
          .Append<tutorflow::gateway::PaymentReceiptConfirmHandler>()
          .Append<tutorflow::gateway::PaymentReceiptRejectHandler>()
          .Append<tutorflow::gateway::FilesHandler>()
          .Append<tutorflow::gateway::FileMetaHandler>()
          .Append<tutorflow::gateway::FileDownloadHandler>();
  return userver::utils::DaemonMain(argc, argv, component_list);
}
