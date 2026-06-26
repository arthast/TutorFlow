#include "domain/report_service.hpp"

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>

#include "repositories/report_repository.hpp"

namespace tutorflow::report {

ReportService::ReportService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<ReportRepository>()) {}

TeacherDashboard ReportService::GetTeacherDashboard(
    const tutorflow::common::AuthContext& auth) const {
  tutorflow::common::RequireTeacher(auth);
  return repository_.GetTeacherDashboard(auth.user_id);
}

StudentDashboard ReportService::GetStudentDashboard(
    const tutorflow::common::AuthContext& auth) const {
  tutorflow::common::RequireStudent(auth);
  return repository_.GetStudentDashboard(auth.user_id);
}

StudentSummary ReportService::GetStudentSummary(
    const tutorflow::common::AuthContext& auth,
    const std::string& student_id) const {
  tutorflow::common::RequireTeacher(auth);
  if (student_id.empty()) {
    throw tutorflow::common::ServiceError::Validation("student_id is required");
  }
  return repository_.GetStudentSummary(auth.user_id, student_id);
}

}  // namespace tutorflow::report
