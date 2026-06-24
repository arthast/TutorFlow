# Versioned internal gRPC contracts. 5A only generates code; services continue
# using the existing REST/HTTP paths until the 5B/5C migration.

set(TUTORFLOW_PROTO_FILES
    tutorflow/common.proto
    tutorflow/identity.proto
    tutorflow/lesson.proto
    tutorflow/assignment.proto
    tutorflow/finance.proto
    tutorflow/notification.proto
)

userver_add_grpc_library(tutorflow_proto
    SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}"
    PROTOS ${TUTORFLOW_PROTO_FILES}
)

add_library(tutorflow::proto ALIAS tutorflow_proto)
target_compile_features(tutorflow_proto PUBLIC cxx_std_20)
