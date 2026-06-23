#include "gateway_settings.hpp"

#include <userver/components/component_config.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::gateway {

namespace {

std::string TrimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}

}  // namespace

GatewaySettings::GatewaySettings(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      jwt_secret_(config["jwt-secret"].As<std::string>()),
      cors_origin_(config["cors-origin"].As<std::string>("http://localhost:5173")),
      identity_base_url_(
          TrimTrailingSlash(config["identity-base-url"].As<std::string>())),
      lesson_base_url_(
          TrimTrailingSlash(config["lesson-base-url"].As<std::string>())),
      assignment_base_url_(
          TrimTrailingSlash(config["assignment-base-url"].As<std::string>())),
      finance_base_url_(
          TrimTrailingSlash(config["finance-base-url"].As<std::string>())),
      file_base_url_(
          TrimTrailingSlash(config["file-base-url"].As<std::string>())),
      timeout_(std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)}) {}

userver::yaml_config::Schema GatewaySettings::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: api-gateway routing and auth settings
additionalProperties: false
properties:
    jwt-secret:
        type: string
        description: JWT verification secret shared with identity-service
    cors-origin:
        type: string
        description: Browser origin allowed by api-gateway CORS
        defaultDescription: http://localhost:5173
    identity-base-url:
        type: string
        description: identity-service base URL
    lesson-base-url:
        type: string
        description: lesson-service base URL
    assignment-base-url:
        type: string
        description: assignment-service base URL
    finance-base-url:
        type: string
        description: finance-service base URL
    file-base-url:
        type: string
        description: file-service base URL
    timeout-ms:
        type: integer
        description: upstream request timeout in milliseconds
        defaultDescription: '5000'
)");
}

const std::string& GatewaySettings::BaseUrl(UpstreamService service) const {
  switch (service) {
    case UpstreamService::kIdentity:
      return identity_base_url_;
    case UpstreamService::kLesson:
      return lesson_base_url_;
    case UpstreamService::kAssignment:
      return assignment_base_url_;
    case UpstreamService::kFinance:
      return finance_base_url_;
    case UpstreamService::kFile:
      return file_base_url_;
  }
  return identity_base_url_;
}

}  // namespace tutorflow::gateway
