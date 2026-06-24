# lesson-service (owner: Agent B). Расписание + занятия.
add_executable(lesson-service
    src/main.cpp
    src/domain/lesson_service.cpp
    src/grpc/lesson_grpc_service.cpp
    src/outbox/outbox_publisher.cpp
    src/repositories/lesson_repository.cpp
)

target_include_directories(lesson-service PRIVATE src)

target_link_libraries(lesson-service
    PRIVATE
        tutorflow_common
        tutorflow_grpc_clients
        tutorflow_events
        userver::core
        userver::grpc
        userver::kafka
        userver::postgresql
)
