# assignment-service (owner: Agent B). ДЗ, решения, комментарии.
add_executable(assignment-service
    src/main.cpp
    src/domain/assignment_service.cpp
    src/grpc/assignment_grpc_service.cpp
    src/repositories/assignment_repository.cpp
)

target_include_directories(assignment-service PRIVATE src)

target_link_libraries(assignment-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        userver::core
        userver::grpc
        userver::postgresql
)
