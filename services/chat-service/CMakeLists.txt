# chat-service. Личная переписка teacher<->student (gRPC) + outbox-producer.
add_executable(chat-service
    src/main.cpp
    src/domain/chat_service.cpp
    src/grpc/chat_grpc_service.cpp
    src/handlers/ready_handler.cpp
    src/repositories/dialog_merge.cpp
    src/repositories/chat_repository.cpp
    src/repositories/shard_router.cpp
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

add_executable(chat-sharding-unit-tests
    tests/shard_router_test.cpp
    tests/dialog_merge_test.cpp
    src/repositories/dialog_merge.cpp
    src/repositories/shard_router.cpp
)

target_include_directories(chat-sharding-unit-tests PRIVATE src)

target_link_libraries(chat-sharding-unit-tests
    PRIVATE
        userver::postgresql
        userver::universal-utest
)

add_google_tests(chat-sharding-unit-tests)
