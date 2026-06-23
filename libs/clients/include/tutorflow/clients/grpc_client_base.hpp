#pragma once

#include <chrono>
#include <string>

#include <grpcpp/support/status.h>

#include <userver/ugrpc/client/call_options.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/http_client_base.hpp>

namespace tutorflow::clients {

struct GrpcCallContext {
  std::string user_id;
  std::string roles;
  std::string request_id;
  std::string trace_id;

  static GrpcCallContext FromCommon(const tutorflow::common::CallContext& ctx);
};

struct GrpcClientOptions {
  std::chrono::milliseconds timeout{std::chrono::seconds{5}};
  int idempotent_attempts{2};
};

enum class GrpcOperationKind {
  kNonIdempotent,
  kIdempotent,
};

userver::ugrpc::client::CallOptions MakeGrpcCallOptions(
    const GrpcCallContext& ctx,
    const GrpcClientOptions& options = {},
    GrpcOperationKind operation_kind = GrpcOperationKind::kNonIdempotent);

tutorflow::common::ServiceError MapGrpcStatusToServiceError(
    const grpc::Status& status);

}  // namespace tutorflow::clients
