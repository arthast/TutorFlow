#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <userver/clients/http/client.hpp>
#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/common/http_client_base.hpp>

namespace tutorflow::lesson {

struct ChargeRequest {
  std::string teacher_id;
  std::string student_id;
  std::string lesson_id;
  double amount{};
  std::string currency{"RUB"};
  std::optional<std::string> comment;
};

class FinanceClient {
public:
  virtual ~FinanceClient() = default;

  virtual void CreateCharge(const ChargeRequest &request) const = 0;
};

class HttpFinanceClient final
    : public userver::components::LoggableComponentBase,
      public FinanceClient {
public:
  static constexpr std::string_view kName = "finance-client";

  HttpFinanceClient(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  void CreateCharge(const ChargeRequest &request) const override;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::common::HttpClientBase transport_;
};

} // namespace tutorflow::lesson
