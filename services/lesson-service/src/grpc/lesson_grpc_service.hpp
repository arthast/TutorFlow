#pragma once

#include <tutorflow/lesson_service.usrv.pb.hpp>

#include "domain/lesson_service.hpp"

namespace tutorflow::lesson {

class LessonGrpcService final
    : public tutorflow::lesson::v1::LessonServiceBase::Component {
public:
  static constexpr std::string_view kName = "lesson-grpc-service";

  LessonGrpcService(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  CreateAvailabilityResult
  CreateAvailability(CallContext &context,
                     tutorflow::lesson::v1::CreateSlotRequest &&request) override;
  ListAvailabilityResult ListAvailability(
      CallContext &context,
      tutorflow::lesson::v1::ListAvailabilityRequest &&request) override;
  CreateLessonResult
  CreateLesson(CallContext &context,
               tutorflow::lesson::v1::CreateLessonRequest &&request) override;
  ListLessonsResult
  ListLessons(CallContext &context,
              tutorflow::lesson::v1::ListLessonsRequest &&request) override;
  GetLessonResult
  GetLesson(CallContext &context,
            tutorflow::lesson::v1::GetLessonRequest &&request) override;
  CompleteLessonResult CompleteLesson(
      CallContext &context,
      tutorflow::lesson::v1::CompleteLessonRequest &&request) override;
  RescheduleLessonResult RescheduleLesson(
      CallContext &context,
      tutorflow::lesson::v1::RescheduleLessonRequest &&request) override;
  ReactivateLessonResult ReactivateLesson(
      CallContext &context,
      tutorflow::lesson::v1::ReactivateLessonRequest &&request) override;
  CancelLessonResult
  CancelLesson(CallContext &context,
               tutorflow::lesson::v1::CancelLessonRequest &&request) override;

private:
  LessonService &service_;
};

} // namespace tutorflow::lesson
