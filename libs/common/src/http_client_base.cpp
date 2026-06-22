#include <tutorflow/common/http_client_base.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/error_codes.hpp>
#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/request_context.hpp>

#include <userver/clients/http/request.hpp>
#include <userver/clients/http/response.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/http/common_headers.hpp>

namespace tutorflow::common {
namespace {
namespace http = userver::server::http;
}  // namespace

HttpClientBase::HttpClientBase(userver::clients::http::Client& http_client,
                               std::string base_url,
                               std::chrono::milliseconds timeout)
    : http_client_(http_client),
      base_url_(std::move(base_url)),
      timeout_(timeout) {
  while (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
}

std::string HttpClientBase::Url(std::string_view path) const {
  std::string p{path};
  if (p.empty() || p.front() != '/') p.insert(p.begin(), '/');
  return base_url_ + p;
}

userver::clients::http::Headers HttpClientBase::BuildHeaders(
    const CallContext& ctx, bool json_body) const {
  // HeaderMap::operator[] запрещает строковые литералы (нужен PredefinedHeader),
  // но принимает std::string_view — наши kHeader* как раз string_view.
  userver::clients::http::Headers headers;
  if (json_body) {
    headers[userver::http::headers::kContentType] = "application/json";
  }
  if (!ctx.user_id.empty()) headers[kHeaderUserId] = ctx.user_id;
  if (!ctx.roles.empty()) headers[kHeaderUserRoles] = ctx.roles;
  if (!ctx.request_id.empty()) headers[kHeaderRequestId] = ctx.request_id;
  return headers;
}

userver::formats::json::Value HttpClientBase::ParseResponse(
    int status_code, const std::string& body) const {
  userver::formats::json::Value json;
  if (!body.empty()) {
    try {
      json = userver::formats::json::FromString(body);
    } catch (const std::exception&) {
      json = {};
    }
  }

  if (status_code >= 200 && status_code < 300) return json;

  std::string code{error_code::kInternal};
  std::string message = "upstream service error";
  userver::formats::json::Value details;
  if (!json.IsNull() && json.HasMember("error")) {
    const auto error = json["error"];
    if (error.HasMember("code")) code = error["code"].As<std::string>(code);
    if (error.HasMember("message")) {
      message = error["message"].As<std::string>(message);
    }
    if (error.HasMember("details")) details = error["details"];
  }
  throw ServiceError(static_cast<http::HttpStatus>(status_code), std::move(code),
                     std::move(message), std::move(details));
}

userver::formats::json::Value HttpClientBase::GetJson(
    std::string_view path, const CallContext& ctx) const {
  auto response = http_client_.CreateRequest()
                      .get(Url(path))
                      .headers(BuildHeaders(ctx, /*json_body=*/false))
                      .timeout(timeout_)
                      .perform();
  return ParseResponse(static_cast<int>(response->status_code()),
                       response->body());
}

userver::formats::json::Value HttpClientBase::PostJson(
    std::string_view path, const userver::formats::json::Value& body,
    const CallContext& ctx) const {
  auto response = http_client_.CreateRequest()
                      .post(Url(path), userver::formats::json::ToString(body))
                      .headers(BuildHeaders(ctx, /*json_body=*/true))
                      .timeout(timeout_)
                      .perform();
  return ParseResponse(static_cast<int>(response->status_code()),
                       response->body());
}

}  // namespace tutorflow::common
