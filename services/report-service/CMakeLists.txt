# report-service. Dashboard read-models built from Kafka domain events.
add_executable(report-service
    src/main.cpp
    src/consumers/domain_event_consumer.cpp
    src/domain/report_service.cpp
    src/grpc/report_grpc_service.cpp
    src/repositories/report_repository.cpp
)

target_include_directories(report-service PRIVATE src)

target_link_libraries(report-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        tutorflow_events
        userver::core
        userver::grpc
        userver::kafka
        userver::postgresql
)
