# identity-service (owner: Agent A). Auth + профили + связь teacher-student.
find_package(OpenSSL REQUIRED)

add_executable(identity-service
    src/main.cpp
    src/domain/models.cpp
    src/domain/identity_service.cpp
    src/handlers/ready_handler.cpp
    src/repositories/identity_repository.cpp
    src/grpc/identity_grpc_service.cpp
)

target_include_directories(identity-service PRIVATE src)

target_link_libraries(identity-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        userver::core
        userver::grpc
        userver::postgresql
        OpenSSL::Crypto
)
