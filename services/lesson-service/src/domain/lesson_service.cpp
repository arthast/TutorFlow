#include "domain/lesson_service.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>

#include "clients/finance_client.hpp"
#include "clients/identity_client.hpp"
#include "repositories/lesson_repository.hpp"

namespace tutorflow::lesson {

LessonService::LessonService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<LessonRepository>()),
      identity_(context.FindComponent<StubIdentityClient>()),
      finance_(context.FindComponent<StubFinanceClient>()) {}

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
  if (!request.price.has_value()) {
    userver::formats::json::ValueBuilder details(
        userver::formats::common::Type::kObject);
    details["contract_gap"] =
        "identity check-access does not expose hourly_rate";
    throw tutorflow::common::ServiceError::BusinessRule(
        "price is required until identity exposes relation hourly_rate",
        details.ExtractValue());
  }
  if (*request.price <= 0.0) {
    throw tutorflow::common::ServiceError::Validation(
        "price must be greater than zero");
  }
  return repository_.CreateLesson(auth.user_id, request);
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

Lesson LessonService::CompleteLesson(const tutorflow::common::AuthContext &auth,
                                     const std::string &lesson_id) const {
  const auto lesson = repository_.CompleteLesson(lesson_id, auth.user_id);
  finance_.CreateCharge(ChargeRequest{
      .teacher_id = lesson.teacher_id,
      .student_id = lesson.student_id,
      .lesson_id = lesson.id,
      .amount = lesson.price,
      .currency = "RUB",
      .comment = std::string{"Lesson charge"},
  });
  return lesson;
}

Lesson LessonService::CancelLesson(const tutorflow::common::AuthContext &auth,
                                   const std::string &lesson_id) const {
  return repository_.CancelLesson(lesson_id, auth.user_id);
}

} // namespace tutorflow::lesson
