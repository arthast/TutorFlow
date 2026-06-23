# finance-service (owner: Agent B). Append-only журнал операций + чеки.
add_executable(finance-service
    src/main.cpp
    src/domain/finance_service.cpp
    src/domain/models.cpp
    src/handlers/finance_handlers.cpp
    src/repositories/finance_repository.cpp
)

target_include_directories(finance-service PRIVATE src)

target_link_libraries(finance-service
    PRIVATE
        tutorflow_common
        tutorflow_clients
        userver::core
        userver::postgresql
)
