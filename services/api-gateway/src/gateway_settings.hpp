#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/yaml_config/schema.hpp>

namespace tutorflow::gateway {

enum class UpstreamService {
  kIdentity,
  kLesson,
  kAssignment,
  kFinance,
  kFile,
};

class GatewaySettings final
    : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "gateway-settings";

  GatewaySettings(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  static userver::yaml_config::Schema GetStaticConfigSchema();

  const std::string& JwtSecret() const noexcept { return jwt_secret_; }
  std::chrono::milliseconds Timeout() const noexcept { return timeout_; }
  const std::string& BaseUrl(UpstreamService service) const;

 private:
  std::string jwt_secret_;
  std::string identity_base_url_;
  std::string lesson_base_url_;
  std::string assignment_base_url_;
  std::string finance_base_url_;
  std::string file_base_url_;
  std::chrono::milliseconds timeout_;
};

}  // namespace tutorflow::gateway
