#pragma once

#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

namespace tutorflow::assignment {

struct AccessCheckResult {
  bool allowed{};
  std::string status;
};

class IdentityClient {
public:
  virtual ~IdentityClient() = default;

  virtual AccessCheckResult CheckAccess(std::string_view teacher_id,
                                        std::string_view student_id) const = 0;
};

class StubIdentityClient final
    : public userver::components::LoggableComponentBase,
      public IdentityClient {
public:
  static constexpr std::string_view kName = "identity-client";

  StubIdentityClient(const userver::components::ComponentConfig &config,
                     const userver::components::ComponentContext &context);

  AccessCheckResult CheckAccess(std::string_view teacher_id,
                                std::string_view student_id) const override;
};

} // namespace tutorflow::assignment
