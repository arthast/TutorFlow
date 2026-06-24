# finance-service (owner: Agent B). Append-only журнал операций + чеки.
add_executable(finance-service
    src/main.cpp
    src/consumers/lesson_completed_consumer.cpp
    src/domain/finance_service.cpp
    src/domain/models.cpp
    src/grpc/finance_grpc_service.cpp
    src/outbox/outbox_publisher.cpp
    src/repositories/finance_repository.cpp
)

target_include_directories(finance-service PRIVATE src)

target_link_libraries(finance-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        tutorflow_events
        userver::core
        userver::grpc
        userver::kafka
        userver::postgresql
)
