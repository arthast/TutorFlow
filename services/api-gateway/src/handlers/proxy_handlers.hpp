#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <userver/clients/http/client.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

#include "gateway_settings.hpp"

namespace tutorflow::gateway {

class GrpcIdentityClient;
class GrpcLessonClient;
class GrpcAssignmentClient;
class GrpcFinanceClient;
class GrpcNotificationClient;

struct AuthInfo {
  std::string user_id;
  std::string roles_csv;
};

class ProxyHandlerBase : public userver::server::handlers::HttpHandlerBase {
 public:
  ProxyHandlerBase(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

 protected:
  AuthInfo Authenticate(
      const userver::server::http::HttpRequest& request) const;

  std::string ProxyToUpstream(
      const userver::server::http::HttpRequest& request,
      UpstreamService service, std::string internal_path,
      std::optional<AuthInfo> auth) const;

  std::string HandleGatewayErrors(
      const userver::server::http::HttpRequest& request,
      const std::function<std::string()>& func) const;

  GrpcIdentityClient& Identity() const noexcept { return identity_client_; }
  GrpcLessonClient& Lesson() const noexcept { return lesson_client_; }
  GrpcAssignmentClient& Assignment() const noexcept {
    return assignment_client_;
  }
  GrpcFinanceClient& Finance() const noexcept { return finance_client_; }
  GrpcNotificationClient& Notification() const noexcept {
    return notification_client_;
  }

 private:
  const GatewaySettings& settings_;
  userver::clients::http::Client& http_client_;
  GrpcIdentityClient& identity_client_;
  GrpcLessonClient& lesson_client_;
  GrpcAssignmentClient& assignment_client_;
  GrpcFinanceClient& finance_client_;
  GrpcNotificationClient& notification_client_;
};

#define TUTORFLOW_GATEWAY_DECLARE_HANDLER(ClassName, NameValue)              \
  class ClassName final : public ProxyHandlerBase {                          \
   public:                                                                   \
    static constexpr std::string_view kName = NameValue;                     \
    ClassName(const userver::components::ComponentConfig& config,            \
              const userver::components::ComponentContext& context);         \
    std::string HandleRequestThrow(                                          \
        const userver::server::http::HttpRequest& request,                   \
        userver::server::request::RequestContext&) const override;           \
  }

TUTORFLOW_GATEWAY_DECLARE_HANDLER(AuthRegisterHandler,
                                  "gateway-auth-register-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AuthLoginHandler,
                                  "gateway-auth-login-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AuthChangePasswordHandler,
                                  "gateway-auth-change-password-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(MeHandler, "gateway-me-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(StudentsHandler,
                                  "gateway-students-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(StudentHandler, "gateway-student-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(StudentBalanceHandler,
                                  "gateway-student-balance-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(StudentTransactionsHandler,
                                  "gateway-student-transactions-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AvailabilityHandler,
                                  "gateway-availability-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(LessonsHandler, "gateway-lessons-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(LessonHandler, "gateway-lesson-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(LessonCompleteHandler,
                                  "gateway-lesson-complete-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(LessonRescheduleHandler,
                                  "gateway-lesson-reschedule-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(LessonReactivateHandler,
                                  "gateway-lesson-reactivate-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(LessonCancelHandler,
                                  "gateway-lesson-cancel-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AssignmentsHandler,
                                  "gateway-assignments-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AssignmentHandler,
                                  "gateway-assignment-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AssignmentSubmitHandler,
                                  "gateway-assignment-submit-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AssignmentReviewHandler,
                                  "gateway-assignment-review-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(AssignmentCommentsHandler,
                                  "gateway-assignment-comments-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(PaymentReceiptsHandler,
                                  "gateway-payment-receipts-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(PaymentReceiptConfirmHandler,
                                  "gateway-payment-receipt-confirm-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(PaymentReceiptRejectHandler,
                                  "gateway-payment-receipt-reject-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(NotificationsHandler,
                                  "gateway-notifications-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(NotificationReadHandler,
                                  "gateway-notification-read-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(FilesHandler, "gateway-files-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(FileMetaHandler,
                                  "gateway-file-meta-handler");
TUTORFLOW_GATEWAY_DECLARE_HANDLER(FileDownloadHandler,
                                  "gateway-file-download-handler");

#undef TUTORFLOW_GATEWAY_DECLARE_HANDLER

}  // namespace tutorflow::gateway
