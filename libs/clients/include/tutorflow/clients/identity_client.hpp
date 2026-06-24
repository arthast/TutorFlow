#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tutorflow::clients {

struct AccessCheckResult {
  bool allowed{};
  std::string status;
  std::optional<double> hourly_rate;
};

// Teacher<->student access check used by lesson/assignment/finance/file.
// Implemented over gRPC by GrpcIdentityClient (identity_grpc_client.hpp).
class IdentityClient {
public:
  virtual ~IdentityClient() = default;

  virtual AccessCheckResult CheckAccess(std::string_view teacher_id,
                                        std::string_view student_id) const = 0;
};

}  // namespace tutorflow::clients
