#pragma once

#include <chrono>
#include <string>
#include <utility>

#include <grpcpp/support/status.h>

#include <userver/ugrpc/client/call_options.hpp>
#include <userver/ugrpc/client/exceptions.hpp>

#include <tutorflow/common.pb.h>

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

// Convenience wrappers around MakeGrpcCallOptions for the two operation kinds.
userver::ugrpc::client::CallOptions IdempotentCall(
    const GrpcCallContext& ctx, const GrpcClientOptions& options = {});
userver::ugrpc::client::CallOptions NonIdempotentCall(
    const GrpcCallContext& ctx, const GrpcClientOptions& options = {});

// Fills a proto UserContext from the call context (user id + roles CSV).
void FillUserContext(tutorflow::common::v1::UserContext& user,
                     const GrpcCallContext& ctx);

// Runs a gRPC client call and converts ugrpc client errors into ServiceError so
// callers see the same domain error type regardless of transport.
template <typename Func>
auto InvokeUnary(Func&& func) {
  try {
    return std::forward<Func>(func)();
  } catch (const userver::ugrpc::client::ErrorWithStatus& e) {
    throw MapGrpcStatusToServiceError(e.GetStatus());
  } catch (const userver::ugrpc::client::BaseError& e) {
    throw tutorflow::common::ServiceError::Internal(e.what());
  }
}

}  // namespace tutorflow::clients
