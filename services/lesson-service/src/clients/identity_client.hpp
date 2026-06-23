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

struct AccessCheckResult {
  bool allowed{};
  std::string status;
  std::optional<double> hourly_rate;
};

class IdentityClient {
public:
  virtual ~IdentityClient() = default;

  virtual AccessCheckResult CheckAccess(std::string_view teacher_id,
                                        std::string_view student_id) const = 0;
};

class HttpIdentityClient final
    : public userver::components::LoggableComponentBase,
      public IdentityClient {
public:
  static constexpr std::string_view kName = "identity-client";

  HttpIdentityClient(const userver::components::ComponentConfig &config,
                     const userver::components::ComponentContext &context);

  AccessCheckResult CheckAccess(std::string_view teacher_id,
                                std::string_view student_id) const override;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::common::HttpClientBase transport_;
};

} // namespace tutorflow::lesson
