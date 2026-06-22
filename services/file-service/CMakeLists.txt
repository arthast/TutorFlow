# file-service (owner: Agent A). Метаданные файлов + локальный том.
add_executable(file-service
    src/main.cpp
    src/domain/models.cpp
    src/domain/file_service.cpp
    src/repositories/file_repository.cpp
    src/clients/identity_client.cpp
    src/handlers/file_handlers.cpp
)

target_include_directories(file-service PRIVATE src)

target_link_libraries(file-service
    PRIVATE
        tutorflow_common
        userver::core
        userver::postgresql
)
