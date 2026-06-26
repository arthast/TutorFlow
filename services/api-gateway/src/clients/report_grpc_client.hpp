#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include <tutorflow/report_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcReportClient final : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "gateway-report-grpc-client";

  GrpcReportClient(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

  userver::formats::json::Value GetTeacherDashboard(
      const tutorflow::clients::GrpcCallContext& call_context,
      const std::unordered_map<std::string, std::string>& student_names) const;
  userver::formats::json::Value GetStudentDashboard(
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value GetStudentSummary(
      std::string_view student_id,
      const tutorflow::clients::GrpcCallContext& call_context,
      std::string_view student_name) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

 private:
  tutorflow::report::v1::ReportServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

}  // namespace tutorflow::gateway
