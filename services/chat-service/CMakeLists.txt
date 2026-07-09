# chat-service. Личная переписка teacher<->student (gRPC) + outbox-producer.
add_executable(chat-service
    src/main.cpp
    src/domain/chat_service.cpp
    src/grpc/chat_grpc_service.cpp
    src/handlers/ready_handler.cpp
    src/repositories/chat_repository.cpp
    src/outbox/outbox_publisher.cpp
)

target_include_directories(chat-service PRIVATE src)

target_link_libraries(chat-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        tutorflow_events
        userver::core
        userver::grpc
        userver::kafka
        userver::postgresql
)
