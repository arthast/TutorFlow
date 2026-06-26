#pragma once

#include <string>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include "domain/models.hpp"

namespace tutorflow::common {
struct AuthContext;
}

namespace tutorflow::report {

class ReportRepository;

class ReportService final : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "report-domain-service";

  ReportService(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);

  TeacherDashboard GetTeacherDashboard(
      const tutorflow::common::AuthContext& auth) const;
  StudentDashboard GetStudentDashboard(
      const tutorflow::common::AuthContext& auth) const;
  StudentSummary GetStudentSummary(const tutorflow::common::AuthContext& auth,
                                   const std::string& student_id) const;

 private:
  ReportRepository& repository_;
};

}  // namespace tutorflow::report
