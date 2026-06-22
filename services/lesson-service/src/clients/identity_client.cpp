#include "clients/identity_client.hpp"

#include <chrono>

#include <userver/clients/http/component.hpp>
#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::lesson {
namespace {
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;
} // namespace

HttpIdentityClient::HttpIdentityClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      transport_(
          context.FindComponent<userver::components::HttpClient>()
              .GetHttpClient(),
          config["base-url"].As<std::string>(),
          std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)}) {}

AccessCheckResult
HttpIdentityClient::CheckAccess(std::string_view teacher_id,
                                std::string_view student_id) const {
  json::ValueBuilder body(common_formats::Type::kObject);
  body["teacher_id"] = std::string{teacher_id};
  body["student_id"] = std::string{student_id};
  const auto response =
      transport_.PostJson("/internal/relations/check-access",
                          body.ExtractValue(), tutorflow::common::CallContext{});
  return AccessCheckResult{
      .allowed = response["allowed"].As<bool>(false),
      .status = response["status"].As<std::string>(""),
  };
}

userver::yaml_config::Schema HttpIdentityClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: identity-service HTTP client
additionalProperties: false
properties:
    base-url:
        type: string
        description: identity-service base URL
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
)");
}

} // namespace tutorflow::lesson
