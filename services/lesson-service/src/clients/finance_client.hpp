#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

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

class StubFinanceClient final
    : public userver::components::LoggableComponentBase,
      public FinanceClient {
public:
  static constexpr std::string_view kName = "finance-client";

  StubFinanceClient(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  void CreateCharge(const ChargeRequest &request) const override;
};

} // namespace tutorflow::lesson
