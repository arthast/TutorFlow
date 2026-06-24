# syntax=docker/dockerfile:1
#
# Эталонный Dockerfile сервиса (PLAN §12.4). Остальные сервисы копируют его,
# меняя только дефолт SERVICE и порт EXPOSE.
#
# Контекст сборки — корень репозитория (см. docker-compose.yml): нужны
# CMakeLists.txt + libs/ + services/<svc>/. Root-CMake собирает только этот
# таргет; прочие services/* в контекст не попадают и пропускаются EXISTS-гардом.
#
# По умолчанию userver берётся из официального образа через find_package.
# Переопредели USERVER_IMAGE под свою среду при необходимости.
ARG USERVER_IMAGE=ghcr.io/userver-framework/ubuntu-22.04-userver:latest

FROM ${USERVER_IMAGE}
ARG SERVICE=identity-service
# Число параллельных задач компиляции. Пусто -> nproc (как раньше). На машинах с
# малым объёмом RAM полный nproc (по тяжёлым userver/C++ TU ~1.5-2 ГБ на job)
# приводит к OOM (cc1plus killed) — тогда задаём малое значение (docker-compose
# по умолчанию ставит 2). См. README «Быстрый старт».
ARG BUILD_JOBS=
WORKDIR /src

COPY CMakeLists.txt ./
COPY libs ./libs
COPY services/${SERVICE} ./services/${SERVICE}

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target ${SERVICE} -j "${BUILD_JOBS:-$(nproc)}"

# Бинарь и конфиги под фиксированными путями, чтобы ENTRYPOINT не зависел от ARG.
WORKDIR /app
RUN cp "/src/build/services/${SERVICE}/${SERVICE}" /app/service \
 && cp -r "/src/services/${SERVICE}/configs" /app/configs

EXPOSE 8081
ENTRYPOINT ["/app/service", "--config", "/app/configs/static_config.yaml"]
