#include "domain/lesson_service.hpp"

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>
#include <tutorflow/clients/identity_grpc_client.hpp>

#include "repositories/lesson_repository.hpp"

namespace tutorflow::lesson {

LessonService::LessonService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<LessonRepository>()),
      identity_(context.FindComponent<tutorflow::clients::GrpcIdentityClient>()) {}

Slot LessonService::CreateSlot(const tutorflow::common::AuthContext &auth,
                               const CreateSlotRequest &request) const {
  tutorflow::common::RequireTeacher(auth);
  return repository_.CreateSlot(auth.user_id, request);
}

std::vector<Slot>
LessonService::ListSlots(const tutorflow::common::AuthContext &auth) const {
  return repository_.ListSlots(auth.user_id);
}

Lesson LessonService::CreateLesson(const tutorflow::common::AuthContext &auth,
                                   const CreateLessonRequest &request) const {
  tutorflow::common::RequireTeacher(auth);
  const auto access = identity_.CheckAccess(auth.user_id, request.student_id);
  if (!access.allowed || access.status != "active") {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher-student relation is not active");
  }

  auto create_request = request;
  if (!create_request.price.has_value()) {
    create_request.price = access.hourly_rate;
  }
  if (!create_request.price.has_value()) {
    throw tutorflow::common::ServiceError::BusinessRule(
        "price is required when teacher-student relation has no hourly_rate");
  }
  if (*create_request.price <= 0.0) {
    throw tutorflow::common::ServiceError::Validation(
        "price must be greater than zero");
  }
  return repository_.CreateLesson(auth.user_id, create_request);
}

std::vector<Lesson>
LessonService::ListLessons(const tutorflow::common::AuthContext &auth) const {
  if (auth.IsTeacher())
    return repository_.ListLessonsForTeacher(auth.user_id);
  if (auth.IsStudent())
    return repository_.ListLessonsForStudent(auth.user_id);
  throw tutorflow::common::ServiceError::Forbidden(
      "teacher or student role required");
}

Lesson LessonService::GetLesson(const std::string &lesson_id) const {
  auto lesson = repository_.FindLesson(lesson_id);
  if (!lesson.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  return *lesson;
}

CompleteLessonOutcome
LessonService::CompleteLesson(const tutorflow::common::AuthContext &auth,
                              const std::string &lesson_id) const {
  const auto lesson = repository_.CompleteLesson(lesson_id, auth.user_id);
  return CompleteLessonOutcome{
      .lesson = lesson,
      .charge_status = "pending",
  };
}

Lesson LessonService::RescheduleLesson(
    const tutorflow::common::AuthContext &auth,
    const RescheduleLessonRequest &request) const {
  tutorflow::common::RequireTeacher(auth);
  const auto current = repository_.FindLesson(request.lesson_id);
  if (!current.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  if (current->teacher_id != auth.user_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own lesson");
  }
  if (current->status != "scheduled") {
    throw tutorflow::common::ServiceError::Conflict(
        "only scheduled lesson can be rescheduled");
  }
  const auto access = identity_.CheckAccess(auth.user_id, current->student_id);
  if (!access.allowed || access.status != "active") {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher-student relation is not active");
  }
  return repository_.RescheduleLesson(auth.user_id, request);
}

Lesson LessonService::ReactivateLesson(
    const tutorflow::common::AuthContext &auth,
    const std::string &lesson_id) const {
  tutorflow::common::RequireTeacher(auth);
  const auto current = repository_.FindLesson(lesson_id);
  if (!current.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("lesson not found");
  }
  if (current->teacher_id != auth.user_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own lesson");
  }
  if (current->status != "cancelled" && current->status != "scheduled" &&
      current->status != "completed") {
    throw tutorflow::common::ServiceError::Conflict(
        "only cancelled lesson can be reactivated");
  }
  const auto access = identity_.CheckAccess(auth.user_id, current->student_id);
  if (!access.allowed || access.status != "active") {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher-student relation is not active");
  }
  return repository_.ReactivateLesson(lesson_id, auth.user_id);
}

Lesson LessonService::CancelLesson(const tutorflow::common::AuthContext &auth,
                                   const std::string &lesson_id) const {
  return repository_.CancelLesson(lesson_id, auth.user_id);
}

} // namespace tutorflow::lesson
