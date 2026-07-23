# file-service (owner: Agent A). Метаданные файлов + local/S3 storage.

add_executable(file-service
    src/main.cpp
    src/domain/models.cpp
    src/domain/file_service.cpp
    src/storages/file_storage.cpp
    src/storages/s3_file_storage.cpp
    src/storages/s3_sigv4_authenticator.cpp
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
        userver::s3api
)

add_executable(file-s3-unit-tests
    tests/s3_sigv4_authenticator_test.cpp
    tests/s3_file_storage_test.cpp
    src/storages/s3_sigv4_authenticator.cpp
    src/storages/s3_file_storage.cpp
)

target_include_directories(file-s3-unit-tests PRIVATE src)

target_link_libraries(file-s3-unit-tests
    PRIVATE
        tutorflow_common
        userver::s3api
        userver::s3api-utest
        userver::utest
)

add_google_tests(file-s3-unit-tests)
