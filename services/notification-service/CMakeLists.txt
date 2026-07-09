# notification-service. In-app notifications built from Kafka domain events.
add_executable(notification-service
    src/main.cpp
    src/consumers/domain_event_consumer.cpp
    src/domain/notification_service.cpp
    src/grpc/notification_grpc_service.cpp
    src/handlers/ready_handler.cpp
    src/outbox/outbox_publisher.cpp
    src/repositories/notification_repository.cpp
)

target_include_directories(notification-service PRIVATE src)

target_link_libraries(notification-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        tutorflow_events
        userver::core
        userver::grpc
        userver::kafka
        userver::postgresql
)
