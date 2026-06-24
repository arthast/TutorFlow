#pragma once

// Shared gRPC server-side helpers reused by every TutorFlow gRPC service:
// auth-context resolution from metadata, ServiceError -> grpc::Status mapping,
// and a unary handler wrapper that turns domain exceptions into status codes.

#include <exception>
#include <utility>

#include <grpcpp/support/status.h>

#include <userver/ugrpc/server/call_context.hpp>
#include <userver/ugrpc/server/result.hpp>

#include <tutorflow/common.pb.h>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>

namespace tutorflow::clients {

// Resolves the auth context from gRPC metadata (x-user-id / x-user-roles),
// falling back to the request's embedded UserContext when metadata is absent.
tutorflow::common::AuthContext ResolveServerAuthContext(
    userver::ugrpc::server::CallContext& context,
    const tutorflow::common::v1::UserContext& user);

// Maps a domain ServiceError to a gRPC status (same mapping as 5B). The stable
// "email_taken" marker is propagated via status details so the gateway can keep
// the external REST envelope code.
grpc::Status ServiceErrorToGrpcStatus(
    const tutorflow::common::ServiceError& error);

// Runs a domain handler and converts exceptions into gRPC status codes.
template <typename Response, typename Func>
userver::ugrpc::server::Result<Response> InvokeServerUnary(Func&& func) {
  try {
    return std::forward<Func>(func)();
  } catch (const tutorflow::common::ServiceError& e) {
    return ServiceErrorToGrpcStatus(e);
  } catch (const std::exception& e) {
    return grpc::Status{grpc::StatusCode::INTERNAL, e.what()};
  }
}

}  // namespace tutorflow::clients
