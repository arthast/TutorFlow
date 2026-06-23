# libs/common — узкий инфраструктурный каркас (PLAN §7).
# Только инфраструктура, одинаковая для всех сервисов. Без DTO и домена.
# Public-сигнатуры меняем только согласованным шагом (PLAN §16).

find_package(OpenSSL REQUIRED)

add_library(tutorflow_common STATIC
    src/errors.cpp
    src/auth_context.cpp
    src/request_context.cpp
    src/http_client_base.cpp
    src/handler_helpers.cpp
    src/health_handler.cpp
    src/jwt.cpp
)

target_include_directories(tutorflow_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# userver::core даёт json, server/handlers, clients::http и utils — всё, что
# нужно каркасу. Постгрес-обёртку сюда НЕ тянем (PLAN §7).
target_link_libraries(tutorflow_common
    PUBLIC userver::core
    PRIVATE OpenSSL::Crypto
)

target_compile_features(tutorflow_common PUBLIC cxx_std_20)
