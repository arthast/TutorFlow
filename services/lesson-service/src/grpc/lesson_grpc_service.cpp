#include "grpc/lesson_grpc_service.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>

#include "domain/models.hpp"

namespace tutorflow::lesson {
namespace {
namespace proto = tutorflow::lesson::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

proto::Slot ToProto(const Slot &slot) {
  proto::Slot response;
  response.set_id(slot.id);
  response.set_teacher_id(slot.teacher_id);
  response.set_starts_at(slot.starts_at);
  response.set_ends_at(slot.ends_at);
  response.set_status(slot.status);
  response.set_created_at(slot.created_at);
  return response;
}

proto::Lesson ToProto(const Lesson &lesson) {
  proto::Lesson response;
  response.set_id(lesson.id);
  response.set_teacher_id(lesson.teacher_id);
  response.set_student_id(lesson.student_id);
  if (lesson.slot_id) {
    response.set_slot_id(*lesson.slot_id);
  }
  response.set_starts_at(lesson.starts_at);
  response.set_ends_at(lesson.ends_at);
  response.set_status(lesson.status);
  if (lesson.topic) {
    response.set_topic(*lesson.topic);
  }
  if (lesson.notes) {
    response.set_notes(*lesson.notes);
  }
  response.set_price(lesson.price);
  for (const auto &file_id : lesson.file_ids) {
    response.add_file_ids(file_id);
  }
  response.set_created_at(lesson.created_at);
  if (lesson.completed_at) {
    response.set_completed_at(*lesson.completed_at);
  }
  return response;
}

CreateSlotRequest FromProto(const proto::CreateSlotRequest &request) {
  return CreateSlotRequest{
      .starts_at = request.starts_at(),
      .ends_at = request.ends_at(),
  };
}

CreateLessonRequest FromProto(const proto::CreateLessonRequest &request) {
  CreateLessonRequest result{
      .student_id = request.student_id(),
      .slot_id = request.has_slot_id()
                     ? std::optional<std::string>{request.slot_id()}
                     : std::nullopt,
      .starts_at = request.starts_at(),
      .ends_at = request.ends_at(),
      .topic = request.has_topic()
                   ? std::optional<std::string>{request.topic()}
                   : std::nullopt,
      .notes = request.has_notes()
                   ? std::optional<std::string>{request.notes()}
                   : std::nullopt,
      .price = request.has_price() ? std::optional<double>{request.price()}
                                   : std::nullopt,
      .file_ids = {},
  };
  for (const auto &file_id : request.file_ids()) {
    result.file_ids.push_back(file_id);
  }
  return result;
}

} // namespace

LessonGrpcService::LessonGrpcService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : proto::LessonServiceBase::Component(config, context),
      service_(context.FindComponent<LessonService>()) {}

LessonGrpcService::CreateAvailabilityResult
LessonGrpcService::CreateAvailability(CallContext &context,
                                      proto::CreateSlotRequest &&request) {
  return InvokeServerUnary<proto::Slot>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CreateSlot(auth, FromProto(request)));
  });
}

LessonGrpcService::ListAvailabilityResult
LessonGrpcService::ListAvailability(CallContext &context,
                                    proto::ListAvailabilityRequest &&request) {
  return InvokeServerUnary<proto::ListAvailabilityResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    proto::ListAvailabilityResponse response;
    for (const auto &slot : service_.ListSlots(auth)) {
      *response.add_slots() = ToProto(slot);
    }
    return response;
  });
}

LessonGrpcService::CreateLessonResult
LessonGrpcService::CreateLesson(CallContext &context,
                                proto::CreateLessonRequest &&request) {
  return InvokeServerUnary<proto::Lesson>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CreateLesson(auth, FromProto(request)));
  });
}

LessonGrpcService::ListLessonsResult
LessonGrpcService::ListLessons(CallContext &context,
                               proto::ListLessonsRequest &&request) {
  return InvokeServerUnary<proto::ListLessonsResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    proto::ListLessonsResponse response;
    for (const auto &lesson : service_.ListLessons(auth)) {
      *response.add_lessons() = ToProto(lesson);
    }
    return response;
  });
}

LessonGrpcService::GetLessonResult
LessonGrpcService::GetLesson(CallContext &, proto::GetLessonRequest &&request) {
  return InvokeServerUnary<proto::Lesson>(
      [&] { return ToProto(service_.GetLesson(request.lesson_id())); });
}

LessonGrpcService::CompleteLessonResult
LessonGrpcService::CompleteLesson(CallContext &context,
                                  proto::CompleteLessonRequest &&request) {
  return InvokeServerUnary<proto::Lesson>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CompleteLesson(auth, request.lesson_id()));
  });
}

LessonGrpcService::CancelLessonResult
LessonGrpcService::CancelLesson(CallContext &context,
                                proto::CancelLessonRequest &&request) {
  return InvokeServerUnary<proto::Lesson>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CancelLesson(auth, request.lesson_id()));
  });
}

} // namespace tutorflow::lesson
