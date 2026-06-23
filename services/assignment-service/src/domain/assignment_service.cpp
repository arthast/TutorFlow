#include "domain/assignment_service.hpp"

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>
#include <tutorflow/clients/identity_client.hpp>

#include "repositories/assignment_repository.hpp"

namespace tutorflow::assignment {
namespace {

void RequireNonEmpty(std::string_view value, std::string_view field) {
  if (value.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        std::string{field} + " must not be empty");
  }
}

void ValidateReviewStatus(const std::string &status) {
  if (status == "reviewed" || status == "needs_fix" || status == "accepted") {
    return;
  }
  throw tutorflow::common::ServiceError::Validation("invalid review status");
}

void EnsureParticipant(const Assignment &assignment, const std::string &user_id) {
  if (assignment.teacher_id == user_id || assignment.student_id == user_id) {
    return;
  }
  throw tutorflow::common::ServiceError::Forbidden(
      "user is not assignment participant");
}

} // namespace

AssignmentService::AssignmentService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<AssignmentRepository>()),
      identity_(context.FindComponent<tutorflow::clients::HttpIdentityClient>()) {}

Assignment AssignmentService::CreateAssignment(
    const tutorflow::common::AuthContext &auth,
    const CreateAssignmentRequest &request) const {
  tutorflow::common::RequireTeacher(auth);
  RequireNonEmpty(request.title, "title");

  const auto access = identity_.CheckAccess(auth.user_id, request.student_id);
  if (!access.allowed || access.status != "active") {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher-student relation is not active");
  }
  return repository_.CreateAssignment(auth.user_id, request);
}

std::vector<Assignment> AssignmentService::ListAssignments(
    const tutorflow::common::AuthContext &auth) const {
  if (auth.IsTeacher()) {
    return repository_.ListAssignmentsForTeacher(auth.user_id);
  }
  if (auth.IsStudent()) {
    return repository_.ListAssignmentsForStudent(auth.user_id);
  }
  throw tutorflow::common::ServiceError::Forbidden(
      "teacher or student role required");
}

AssignmentDetail
AssignmentService::GetAssignment(const tutorflow::common::AuthContext &auth,
                                 const std::string &assignment_id) const {
  auto detail = repository_.GetAssignmentDetail(assignment_id);
  EnsureParticipant(detail.assignment, auth.user_id);
  return detail;
}

Submission AssignmentService::SubmitAssignment(
    const tutorflow::common::AuthContext &auth,
    const std::string &assignment_id, const SubmitRequest &request) const {
  tutorflow::common::RequireStudent(auth);
  const auto assignment = repository_.FindAssignment(assignment_id);
  if (!assignment.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("assignment not found");
  }
  if (assignment->student_id != auth.user_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "student does not own assignment");
  }
  if (assignment->status == "done" || assignment->status == "expired") {
    throw tutorflow::common::ServiceError::Conflict(
        "assignment cannot be submitted in current status");
  }
  if (!request.text_answer.has_value() && request.file_ids.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "text_answer or file_ids is required");
  }
  return repository_.CreateSubmission(assignment_id, auth.user_id, request);
}

Submission AssignmentService::ReviewAssignment(
    const tutorflow::common::AuthContext &auth,
    const std::string &assignment_id, const ReviewRequest &request) const {
  tutorflow::common::RequireTeacher(auth);
  ValidateReviewStatus(request.status);
  const auto assignment = repository_.FindAssignment(assignment_id);
  if (!assignment.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("assignment not found");
  }
  if (assignment->teacher_id != auth.user_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own assignment");
  }
  if (assignment->status == "expired") {
    throw tutorflow::common::ServiceError::Conflict(
        "expired assignment cannot be reviewed");
  }
  return repository_.ReviewLatestSubmission(assignment_id, request);
}

Comment AssignmentService::CreateComment(
    const tutorflow::common::AuthContext &auth,
    const std::string &assignment_id, const CommentRequest &request) const {
  RequireNonEmpty(request.text, "text");
  const auto assignment = repository_.FindAssignment(assignment_id);
  if (!assignment.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("assignment not found");
  }
  EnsureParticipant(*assignment, auth.user_id);
  return repository_.CreateComment(assignment_id, auth.user_id, request);
}

} // namespace tutorflow::assignment
