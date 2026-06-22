#include "clients/finance_client.hpp"

#include <chrono>

#include <userver/clients/http/component.hpp>
#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::lesson {
namespace {
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;
} // namespace

HttpFinanceClient::HttpFinanceClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      transport_(
          context.FindComponent<userver::components::HttpClient>()
              .GetHttpClient(),
          config["base-url"].As<std::string>(),
          std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)}) {}

void HttpFinanceClient::CreateCharge(const ChargeRequest &request) const {
  json::ValueBuilder body(common_formats::Type::kObject);
  body["teacher_id"] = request.teacher_id;
  body["student_id"] = request.student_id;
  body["lesson_id"] = request.lesson_id;
  body["amount"] = request.amount;
  body["currency"] = request.currency;
  if (request.comment.has_value()) {
    body["comment"] = *request.comment;
  }

  transport_.PostJson("/internal/charges", body.ExtractValue(),
                      tutorflow::common::CallContext{});
  LOG_INFO() << "Finance charge ensured lesson_id=" << request.lesson_id;
}

userver::yaml_config::Schema HttpFinanceClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: finance-service HTTP client
additionalProperties: false
properties:
    base-url:
        type: string
        description: finance-service base URL
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
)");
}

} // namespace tutorflow::lesson
