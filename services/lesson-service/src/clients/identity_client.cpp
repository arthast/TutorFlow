#include "clients/identity_client.hpp"

#include <userver/logging/log.hpp>

namespace tutorflow::lesson {

StubIdentityClient::StubIdentityClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context) {}

AccessCheckResult
StubIdentityClient::CheckAccess(std::string_view teacher_id,
                                std::string_view student_id) const {
  LOG_INFO() << "Stub identity check-access allowed teacher_id=" << teacher_id
             << " student_id=" << student_id;
  return {.allowed = true, .status = "active"};
}

} // namespace tutorflow::lesson
