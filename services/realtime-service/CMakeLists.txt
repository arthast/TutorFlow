# realtime-service. Public WebSocket push channel for chat/notifications.
add_executable(realtime-service
    src/main.cpp
    src/handlers/ready_handler.cpp
    src/kafka/realtime_event_consumer.cpp
    src/redis/redis_client.cpp
    src/ws/outbound_queue.cpp
    src/ws/connection_registry.cpp
    src/ws/websocket_handler.cpp
)

target_include_directories(realtime-service PRIVATE src)

target_link_libraries(realtime-service
    PRIVATE
        tutorflow_common
        tutorflow_events
        userver::core
        userver::kafka
        userver::redis
)

add_executable(realtime-connection-queue-unit-tests
    tests/outbound_queue_test.cpp
    src/ws/outbound_queue.cpp
)

target_include_directories(realtime-connection-queue-unit-tests PRIVATE src)

target_link_libraries(realtime-connection-queue-unit-tests
    PRIVATE
        userver::core
        userver::utest
)

add_google_tests(realtime-connection-queue-unit-tests)
