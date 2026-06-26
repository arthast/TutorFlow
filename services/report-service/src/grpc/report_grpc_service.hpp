#pragma once

#include <tutorflow/report_service.usrv.pb.hpp>

#include "domain/report_service.hpp"

namespace tutorflow::report {

class ReportGrpcService final
    : public tutorflow::report::v1::ReportServiceBase::Component {
 public:
  static constexpr std::string_view kName = "report-grpc-service";

  ReportGrpcService(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

  GetTeacherDashboardResult GetTeacherDashboard(
      CallContext& context,
      tutorflow::report::v1::GetTeacherDashboardRequest&& request) override;
  GetStudentDashboardResult GetStudentDashboard(
      CallContext& context,
      tutorflow::report::v1::GetStudentDashboardRequest&& request) override;
  GetStudentSummaryResult GetStudentSummary(
      CallContext& context,
      tutorflow::report::v1::GetStudentSummaryRequest&& request) override;

 private:
  ReportService& service_;
};

}  // namespace tutorflow::report
