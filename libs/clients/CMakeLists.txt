# Shared typed HTTP clients. Keep service DTOs out of libs/common.

add_library(tutorflow_clients STATIC
    src/identity_client.cpp
)

target_include_directories(tutorflow_clients
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tutorflow_clients
    PUBLIC
        tutorflow_common
        userver::core
)

target_compile_features(tutorflow_clients PUBLIC cxx_std_20)
