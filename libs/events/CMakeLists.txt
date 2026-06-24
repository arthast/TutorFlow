# libs/events — Kafka event foundation (Этап 5D).
# Versioned JSON event envelope + thin publisher/consumer wrappers over the
# native userver Kafka client. No outbox / business events here (that is 5E).

add_library(tutorflow_events STATIC
    src/event_envelope.cpp
    src/event_publisher.cpp
    src/event_consumer.cpp
)

target_include_directories(tutorflow_events
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tutorflow_events
    PUBLIC
        userver::core
        userver::kafka
)

target_compile_features(tutorflow_events PUBLIC cxx_std_20)
