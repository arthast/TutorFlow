#pragma once

#include <string>
#include <string_view>

#include <tutorflow/lesson_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcLessonClient final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "gateway-lesson-grpc-client";

  GrpcLessonClient(const userver::components::ComponentConfig &config,
                   const userver::components::ComponentContext &context);

  userver::formats::json::Value ListAvailability(
      const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value CreateAvailability(
      const userver::formats::json::Value &body,
      const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  ListLessons(const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  CreateLesson(const userver::formats::json::Value &body,
               const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  GetLesson(std::string_view lesson_id,
            const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  CompleteLesson(std::string_view lesson_id,
                 const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  CancelLesson(std::string_view lesson_id,
               const tutorflow::clients::GrpcCallContext &call_context) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::lesson::v1::LessonServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

} // namespace tutorflow::gateway
