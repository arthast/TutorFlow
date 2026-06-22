# identity-service (owner: Agent A). Auth + профили + связь teacher-student.
find_package(OpenSSL REQUIRED)

add_executable(identity-service
    src/main.cpp
    src/domain/models.cpp
    src/domain/identity_service.cpp
    src/repositories/identity_repository.cpp
    src/handlers/auth_handlers.cpp
    src/handlers/identity_handlers.cpp
)

target_include_directories(identity-service PRIVATE src)

target_link_libraries(identity-service
    PRIVATE
        tutorflow_common
        userver::core
        userver::postgresql
        OpenSSL::Crypto
)
