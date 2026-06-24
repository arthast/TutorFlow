#include "clients/lesson_grpc_client.hpp"

#include "clients/json_helpers.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/handler_helpers.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::lesson::v1;
namespace common_proto = tutorflow::common::v1;

json::Value ToJson(const proto::Slot &slot) {
  json::ValueBuilder body;
  body["id"] = slot.id();
  body["teacher_id"] = slot.teacher_id();
  body["starts_at"] = slot.starts_at();
  body["ends_at"] = slot.ends_at();
  body["status"] = slot.status();
  body["created_at"] = slot.created_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::Lesson &lesson) {
  json::ValueBuilder body;
  body["id"] = lesson.id();
  body["teacher_id"] = lesson.teacher_id();
  body["student_id"] = lesson.student_id();
  body["slot_id"] = NullableString(lesson.has_slot_id(), lesson.slot_id());
  body["starts_at"] = lesson.starts_at();
  body["ends_at"] = lesson.ends_at();
  body["status"] = lesson.status();
  body["topic"] = NullableString(lesson.has_topic(), lesson.topic());
  body["notes"] = NullableString(lesson.has_notes(), lesson.notes());
  body["price"] = lesson.price();
  json::ValueBuilder file_ids(common_formats::Type::kArray);
  for (const auto &file_id : lesson.file_ids()) {
    file_ids.PushBack(file_id);
  }
  body["file_ids"] = file_ids.ExtractValue();
  body["created_at"] = lesson.created_at();
  body["completed_at"] =
      NullableString(lesson.has_completed_at(), lesson.completed_at());
  return body.ExtractValue();
}

} // namespace

GrpcLessonClient::GrpcLessonClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::LessonServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout =
              std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)},
      } {}

json::Value GrpcLessonClient::ListAvailability(
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ListAvailabilityRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListAvailability(request,
                                    tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &slot : response.slots()) {
    array.PushBack(ToJson(slot));
  }
  return array.ExtractValue();
}

json::Value GrpcLessonClient::CreateAvailability(
    const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::CreateSlotRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_starts_at(tutorflow::common::RequireString(body, "starts_at"));
  request.set_ends_at(tutorflow::common::RequireString(body, "ends_at"));
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CreateAvailability(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcLessonClient::ListLessons(
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ListLessonsRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListLessons(request,
                               tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &lesson : response.lessons()) {
    array.PushBack(ToJson(lesson));
  }
  return array.ExtractValue();
}

json::Value GrpcLessonClient::CreateLesson(
    const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::CreateLessonRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_student_id(tutorflow::common::RequireString(body, "student_id"));
  if (const auto slot_id = tutorflow::common::OptionalString(body, "slot_id")) {
    request.set_slot_id(*slot_id);
  }
  request.set_starts_at(tutorflow::common::RequireString(body, "starts_at"));
  request.set_ends_at(tutorflow::common::RequireString(body, "ends_at"));
  if (const auto topic = tutorflow::common::OptionalString(body, "topic")) {
    request.set_topic(*topic);
  }
  if (const auto notes = tutorflow::common::OptionalString(body, "notes")) {
    request.set_notes(*notes);
  }
  if (const auto price = tutorflow::common::OptionalDouble(body, "price")) {
    request.set_price(*price);
  }
  for (auto &file_id : RequireStringArray(body, "file_ids")) {
    request.add_file_ids(std::move(file_id));
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CreateLesson(request,
                                tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcLessonClient::GetLesson(
    std::string_view lesson_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::GetLessonRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_lesson_id(std::string{lesson_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.GetLesson(request, tutorflow::clients::IdempotentCall(call_context, options_));
  }));
}

json::Value GrpcLessonClient::CompleteLesson(
    std::string_view lesson_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::CompleteLessonRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_lesson_id(std::string{lesson_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CompleteLesson(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcLessonClient::CancelLesson(
    std::string_view lesson_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::CancelLessonRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_lesson_id(std::string{lesson_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CancelLesson(request,
                                tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

userver::yaml_config::Schema GrpcLessonClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway lesson gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: lesson gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

} // namespace tutorflow::gateway
