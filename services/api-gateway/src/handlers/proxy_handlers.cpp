#include "handlers/proxy_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <userver/clients/http/component.hpp>
#include <userver/clients/http/error.hpp>
#include <userver/clients/http/response.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/http/common_headers.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/error_codes.hpp>
#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/jwt.hpp>
#include <tutorflow/common/request_context.hpp>

#include "cors.hpp"

namespace tutorflow::gateway {
namespace {

namespace http = userver::server::http;
namespace json = userver::formats::json;
using tutorflow::common::ServiceError;

bool StartsWithNoCase(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    const auto a = static_cast<unsigned char>(value[i]);
    const auto b = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

bool EqualsNoCase(std::string_view lhs, std::string_view rhs) {
  return lhs.size() == rhs.size() && StartsWithNoCase(lhs, rhs);
}

std::string JoinRoles(const std::vector<std::string>& roles) {
  std::string result;
  for (const auto& role : roles) {
    if (!result.empty()) result += ',';
    result += role;
  }
  return result;
}

std::string JsonResponse(const http::HttpRequest& req, json::Value body,
                         http::HttpStatus status) {
  req.GetHttpResponse().SetStatus(status);
  req.GetHttpResponse().SetHeader(std::string{"Content-Type"},
                                  std::string{"application/json; charset=utf-8"});
  return json::ToString(body);
}

std::string ErrorResponse(const http::HttpRequest& req, const ServiceError& e) {
  return JsonResponse(req, tutorflow::common::MakeErrorBody(e), e.Status());
}

std::string GatewayUnavailableResponse(const http::HttpRequest& req,
                                       std::string_view message) {
  return JsonResponse(
      req,
      tutorflow::common::MakeErrorBody(
          tutorflow::common::error_code::kInternal, message),
      static_cast<http::HttpStatus>(503));
}

std::string QuerySuffix(const http::HttpRequest& request) {
  const auto& url = request.GetUrl();
  const auto pos = url.find('?');
  if (pos == std::string::npos) return {};
  return url.substr(pos);
}

std::string RequirePathArg(const http::HttpRequest& request,
                           std::string_view name) {
  auto value = request.GetPathArg(name);
  if (value.empty()) {
    throw ServiceError::Validation("missing path parameter: " + std::string{name});
  }
  return value;
}

bool ShouldSkipInboundHeader(std::string_view name) {
  if (StartsWithNoCase(name, "X-User-")) return true;
  return EqualsNoCase(name, "Host") ||
         EqualsNoCase(name, "Content-Length") ||
         EqualsNoCase(name, "Connection") ||
         EqualsNoCase(name, "Transfer-Encoding") ||
         EqualsNoCase(name, "Authorization");
}

bool ShouldSkipOutboundHeader(std::string_view name) {
  return EqualsNoCase(name, "Content-Length") ||
         EqualsNoCase(name, "Connection") ||
         EqualsNoCase(name, "Transfer-Encoding") ||
         StartsWithNoCase(name, "Access-Control-");
}

userver::clients::http::Headers BuildUpstreamHeaders(
    const http::HttpRequest& request, const std::optional<AuthInfo>& auth) {
  userver::clients::http::Headers headers;
  for (const auto& name : request.GetHeaderNames()) {
    if (ShouldSkipInboundHeader(name)) continue;
    headers[name] = request.GetHeader(name);
  }

  const auto request_id = tutorflow::common::GetOrCreateRequestId(request);
  if (!request_id.empty()) headers[tutorflow::common::kHeaderRequestId] = request_id;

  if (auth) {
    headers[tutorflow::common::kHeaderUserId] = auth->user_id;
    headers[tutorflow::common::kHeaderUserRoles] = auth->roles_csv;
  }
  return headers;
}

std::string BuildUrl(const GatewaySettings& settings, UpstreamService service,
                     std::string internal_path) {
  if (internal_path.empty() || internal_path.front() != '/') {
    internal_path.insert(internal_path.begin(), '/');
  }
  return settings.BaseUrl(service) + internal_path;
}

#define TUTORFLOW_GATEWAY_DEFINE_CTOR(ClassName)                             \
  ClassName::ClassName(                                                       \
      const userver::components::ComponentConfig& config,                     \
      const userver::components::ComponentContext& context)                   \
      : ProxyHandlerBase(config, context) {}

}  // namespace

ProxyHandlerBase::ProxyHandlerBase(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      settings_(context.FindComponent<GatewaySettings>()),
      http_client_(
          context.FindComponent<userver::components::HttpClient>().GetHttpClient()) {}

AuthInfo ProxyHandlerBase::Authenticate(const http::HttpRequest& request) const {
  const auto& header = request.GetHeader(userver::http::headers::kAuthorization);
  static constexpr std::string_view kBearer = "Bearer ";
  if (!StartsWithNoCase(header, kBearer)) {
    throw ServiceError::Unauthorized("missing or invalid bearer token");
  }

  const auto token = std::string_view{header}.substr(kBearer.size());
  const auto claims = tutorflow::common::jwt::Verify(token, settings_.JwtSecret());
  if (!claims || claims->sub.empty() || claims->roles.empty()) {
    throw ServiceError::Unauthorized("missing or invalid bearer token");
  }

  const auto roles_csv = JoinRoles(claims->roles);
  if (roles_csv.empty()) {
    throw ServiceError::Unauthorized("missing or invalid bearer token");
  }
  return AuthInfo{.user_id = claims->sub, .roles_csv = roles_csv};
}

std::string ProxyHandlerBase::ProxyToUpstream(
    const http::HttpRequest& request, UpstreamService service,
    std::string internal_path, std::optional<AuthInfo> auth) const {
  internal_path += QuerySuffix(request);
  const auto url = BuildUrl(settings_, service, std::move(internal_path));
  const auto headers = BuildUpstreamHeaders(request, auth);

  auto upstream_request = http_client_.CreateRequest()
                              .headers(headers)
                              .timeout(settings_.Timeout())
                              .DisableReplyDecoding();

  const auto& method = request.GetMethodStr();
  std::shared_ptr<userver::clients::http::Response> upstream_response;
  if (method == "GET") {
    upstream_response = upstream_request.get(url).perform();
  } else if (method == "POST") {
    upstream_response = upstream_request.post(url, request.RequestBody()).perform();
  } else {
    throw ServiceError(static_cast<http::HttpStatus>(405),
                       std::string{tutorflow::common::error_code::kValidation},
                       "method not allowed");
  }

  request.GetHttpResponse().SetStatus(
      static_cast<http::HttpStatus>(static_cast<int>(upstream_response->status_code())));
  for (const auto& [name, value] : upstream_response->headers()) {
    if (ShouldSkipOutboundHeader(name)) continue;
    request.GetHttpResponse().SetHeader(std::string{name}, value);
  }
  return upstream_response->body();
}

std::string ProxyHandlerBase::HandleGatewayErrors(
    const http::HttpRequest& request,
    const std::function<std::string()>& func) const {
  if (IsOptionsRequest(request)) {
    return MakePreflightResponse(request, settings_);
  }

  try {
    auto body = func();
    ApplyCorsHeaders(request, settings_);
    return body;
  } catch (const ServiceError& e) {
    ApplyCorsHeaders(request, settings_);
    return ErrorResponse(request, e);
  } catch (const userver::clients::http::TimeoutException&) {
    ApplyCorsHeaders(request, settings_);
    return GatewayUnavailableResponse(request, "upstream service timeout");
  } catch (const userver::clients::http::BaseException&) {
    ApplyCorsHeaders(request, settings_);
    return GatewayUnavailableResponse(request, "upstream service unavailable");
  } catch (const std::exception& e) {
    ApplyCorsHeaders(request, settings_);
    return ErrorResponse(request, ServiceError::Internal(e.what()));
  }
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AuthRegisterHandler)
std::string AuthRegisterHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    return ProxyToUpstream(request, UpstreamService::kIdentity,
                           "/internal/auth/register", std::nullopt);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AuthLoginHandler)
std::string AuthLoginHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    return ProxyToUpstream(request, UpstreamService::kIdentity,
                           "/internal/auth/login", std::nullopt);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AuthChangePasswordHandler)
std::string AuthChangePasswordHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kIdentity,
                           "/internal/auth/change-password", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(MeHandler)
std::string MeHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kIdentity,
                           "/internal/users/" + auth.user_id, auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(StudentsHandler)
std::string StudentsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    if (request.GetMethodStr() == "GET") {
      return ProxyToUpstream(request, UpstreamService::kIdentity,
                             "/internal/teachers/" + auth.user_id + "/students",
                             auth);
    }
    return ProxyToUpstream(request, UpstreamService::kIdentity,
                           "/internal/students", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(StudentHandler)
std::string StudentHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kIdentity,
                           "/internal/students/" +
                               RequirePathArg(request, "studentId"),
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(StudentBalanceHandler)
std::string StudentBalanceHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kFinance,
                           "/internal/students/" +
                               RequirePathArg(request, "studentId") + "/balance",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(StudentTransactionsHandler)
std::string StudentTransactionsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kFinance,
                           "/internal/students/" +
                               RequirePathArg(request, "studentId") +
                               "/transactions",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AvailabilityHandler)
std::string AvailabilityHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kLesson,
                           "/internal/availability", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(LessonsHandler)
std::string LessonsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kLesson,
                           "/internal/lessons", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(LessonHandler)
std::string LessonHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kLesson,
                           "/internal/lessons/" +
                               RequirePathArg(request, "lessonId"),
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(LessonCompleteHandler)
std::string LessonCompleteHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kLesson,
                           "/internal/lessons/" +
                               RequirePathArg(request, "lessonId") + "/complete",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(LessonCancelHandler)
std::string LessonCancelHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kLesson,
                           "/internal/lessons/" +
                               RequirePathArg(request, "lessonId") + "/cancel",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AssignmentsHandler)
std::string AssignmentsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kAssignment,
                           "/internal/assignments", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AssignmentHandler)
std::string AssignmentHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kAssignment,
                           "/internal/assignments/" +
                               RequirePathArg(request, "assignmentId"),
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AssignmentSubmitHandler)
std::string AssignmentSubmitHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kAssignment,
                           "/internal/assignments/" +
                               RequirePathArg(request, "assignmentId") + "/submit",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AssignmentReviewHandler)
std::string AssignmentReviewHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kAssignment,
                           "/internal/assignments/" +
                               RequirePathArg(request, "assignmentId") + "/review",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(AssignmentCommentsHandler)
std::string AssignmentCommentsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kAssignment,
                           "/internal/assignments/" +
                               RequirePathArg(request, "assignmentId") +
                               "/comments",
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(PaymentReceiptsHandler)
std::string PaymentReceiptsHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    // GET (список чеков) и POST (загрузка чека) идут на один internal-путь;
    // ProxyToUpstream маршрутизирует по HTTP-методу, query (?status=) прокидывает.
    return ProxyToUpstream(request, UpstreamService::kFinance,
                           "/internal/payment-receipts", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(PaymentReceiptConfirmHandler)
std::string PaymentReceiptConfirmHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(
        request, UpstreamService::kFinance,
        "/internal/payment-receipts/" + RequirePathArg(request, "receiptId") +
            "/confirm",
        auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(PaymentReceiptRejectHandler)
std::string PaymentReceiptRejectHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(
        request, UpstreamService::kFinance,
        "/internal/payment-receipts/" + RequirePathArg(request, "receiptId") +
            "/reject",
        auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(FilesHandler)
std::string FilesHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kFile,
                           "/internal/files", auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(FileMetaHandler)
std::string FileMetaHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kFile,
                           "/internal/files/" + RequirePathArg(request, "fileId"),
                           auth);
  });
}

TUTORFLOW_GATEWAY_DEFINE_CTOR(FileDownloadHandler)
std::string FileDownloadHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  return HandleGatewayErrors(request, [&] {
    const auto auth = Authenticate(request);
    return ProxyToUpstream(request, UpstreamService::kFile,
                           "/internal/files/" + RequirePathArg(request, "fileId") +
                               "/download",
                           auth);
  });
}

#undef TUTORFLOW_GATEWAY_DEFINE_CTOR

}  // namespace tutorflow::gateway
