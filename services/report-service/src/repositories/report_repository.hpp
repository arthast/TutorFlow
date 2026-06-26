#pragma once

#include <string>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::report {

class ReportRepository final : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "report-repository";

  ReportRepository(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

  bool ApplyLessonEvent(const std::string& event_id,
                        const std::string& event_type,
                        const LessonEvent& event) const;
  bool ApplyAssignmentEvent(const std::string& event_id,
                            const std::string& event_type,
                            const AssignmentEvent& event) const;
  bool ApplyBalanceEvent(const std::string& event_id,
                         const std::string& event_type,
                         const BalanceEvent& event) const;
  bool ApplyReceiptEvent(const std::string& event_id,
                         const std::string& event_type,
                         const ReceiptEvent& event) const;

  TeacherDashboard GetTeacherDashboard(const std::string& teacher_id) const;
  StudentDashboard GetStudentDashboard(const std::string& student_id) const;
  StudentSummary GetStudentSummary(const std::string& teacher_id,
                                   const std::string& student_id) const;

 private:
  userver::storages::postgres::ClusterPtr pg_;
};

}  // namespace tutorflow::report
