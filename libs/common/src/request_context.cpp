#include <tutorflow/common/request_context.hpp>

#include <userver/utils/uuid4.hpp>

namespace tutorflow::common {

std::string GenerateRequestId() {
  return userver::utils::generators::GenerateUuid();
}

std::string GetOrCreateRequestId(
    const userver::server::http::HttpRequest& request) {
  auto existing = request.GetHeader(std::string{kHeaderRequestId});
  if (!existing.empty()) return existing;
  return GenerateRequestId();
}

}  // namespace tutorflow::common
