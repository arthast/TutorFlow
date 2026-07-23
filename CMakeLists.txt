cmake_minimum_required(VERSION 3.20)
project(tutorflow CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- userver -----------------------------------------------------------------
# Два способа подключить фреймворк (выбирается автоматически):
#   1) submodule  third_party/userver  -> add_subdirectory (полностью
#      воспроизводимая сборка из исходников);
#   2) установленный userver (напр. в официальном Docker-образе) -> find_package.
# Dockerfile сервиса по умолчанию идёт по пути (2). См. README "Сборка".
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/userver/CMakeLists.txt")
    message(STATUS "tutorflow: userver from submodule (third_party/userver)")
    add_subdirectory(third_party/userver)
else()
    message(STATUS "tutorflow: userver via find_package")
    find_package(userver REQUIRED COMPONENTS core grpc postgresql kafka redis s3api)
endif()

# --- общий каркас ------------------------------------------------------------
add_subdirectory(libs/proto)
add_subdirectory(libs/common)
add_subdirectory(libs/clients)
add_subdirectory(libs/events)

# --- сервисы -----------------------------------------------------------------
# EXISTS-гард: Dockerfile сервиса копирует в контекст только свою папку, поэтому
# при сборке одного образа остальные services/* отсутствуют — и это нормально.
set(TUTORFLOW_SERVICES
    api-gateway
    identity-service
    file-service
    lesson-service
    assignment-service
    finance-service
    notification-service
    report-service
    chat-service
    realtime-service
)
foreach(svc ${TUTORFLOW_SERVICES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/services/${svc}/CMakeLists.txt")
        add_subdirectory(services/${svc})
    endif()
endforeach()
