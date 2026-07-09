# file-service (owner: Agent A). Метаданные файлов + локальный том.
find_package(OpenSSL REQUIRED)

add_executable(file-service
    src/main.cpp
    src/domain/models.cpp
    src/domain/file_service.cpp
    src/storages/file_storage.cpp
    src/repositories/file_repository.cpp
    src/handlers/file_handlers.cpp
    src/handlers/ready_handler.cpp
)

target_include_directories(file-service PRIVATE src)

target_link_libraries(file-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        userver::core
        userver::grpc
        userver::postgresql
        OpenSSL::Crypto
)
