# Shared gRPC plumbing + typed domain clients. Keep service DTOs out of
# libs/common (rule §7). All internal sync calls use gRPC; the old HTTP identity
# client was removed after the 5C cleanup.
#
# Contents:
#   grpc_client_base   - client call options, status<->ServiceError mapping,
#                        UserContext fill, unary-call wrapper.
#   grpc_server_utils  - server-side auth-context resolution, ServiceError ->
#                        grpc::Status mapping, unary-handler wrapper.
#   identity_client.hpp - IdentityClient interface + AccessCheckResult.
#   identity_grpc_client - GrpcIdentityClient implementing IdentityClient.

add_library(tutorflow_grpc_clients STATIC
    src/grpc_client_base.cpp
    src/grpc_server_utils.cpp
    src/identity_grpc_client.cpp
)

target_include_directories(tutorflow_grpc_clients
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tutorflow_grpc_clients
    PUBLIC
        tutorflow_common
        tutorflow::proto
        userver::grpc
)

target_compile_features(tutorflow_grpc_clients PUBLIC cxx_std_20)
