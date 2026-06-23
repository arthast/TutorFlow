# TutorFlow

Бэкенд платформы «преподаватель — ученик»: занятия, расписание, домашние задания,
учёт оплат. Микросервисы на **C++20 + userver**, PostgreSQL (отдельная БД на
сервис), Docker Compose. Наружу доступен только `api-gateway`.

> **Документация.** Архитектура и домен — [`docs/PLAN.md`](docs/PLAN.md);
> эндпоинты — [`docs/api-contracts/`](docs/api-contracts/); правила для агентов —
> [`AGENTS.md`](AGENTS.md); обзор — [`docs/architecture.md`](docs/architecture.md).

## Сервисы (PLAN §4)

| Сервис | Порт | Наружу | БД |
|---|---|---|---|
| api-gateway | 8080 | **да** | — |
| identity-service | 8081 | нет | identity_db |
| lesson-service | 8082 | нет | lesson_db |
| assignment-service | 8083 | нет | assignment_db |
| finance-service | 8084 | нет | finance_db |
| file-service | 8085 | нет | file_db |

## Быстрый старт (dev)

```bash
cp .env.example .env          # заполнить JWT_SECRET, пароль БД и т.п.
docker compose up --build     # postgres + 6 сервисов
./scripts/migrate.sh all      # применить миграции (postgres должен быть поднят)
curl http://localhost:8080/health   # -> {"status":"ok"} (через gateway)
```

Миграции по одному сервису: `./scripts/migrate.sh identity`.
Сброс БД (пересоздать тома и per-service базы): `docker compose down -v`.

## Что реализовано в MVP

MVP покрывает регистрацию и логин, создание ученика преподавателем, расписание
и завершение занятий, домашние задания с отправкой решения, review и
комментариями, загрузку файлов, загрузку payment receipt, ручное подтверждение
платежа преподавателем и append-only финансовые операции.

## Smoke test

После запуска сервисов и миграций:

```bash
python3 scripts/smoke_mvp.py
```

`SMOKE OK` означает, что demo flow через внешний `api-gateway` проходит целиком:
teacher регистрируется, создаёт student/lesson/assignment, student отправляет
решение и receipt, teacher review/comment/complete/confirm, finance обновляет
баланс.

## Переменные окружения

Минимальный dev-набор лежит в `.env.example`. Для локального запуска обычно
нужны `POSTGRES_PASSWORD`, per-service `*_DATABASE_URL`, `JWT_SECRET` и URL
внутренних сервисов, которые уже используются `docker-compose.yml`.

## API contract

Публичный контракт gateway: `docs/api-contracts/gateway.openapi.yaml`.
Внутренние контракты сервисов лежат рядом в `docs/api-contracts/`.

## Что не входит в MVP

В MVP намеренно нет Kafka, Redis, чата, уведомлений, frontend, Telegram bot,
Google Calendar и реальных платёжных интеграций.

## Сборка

Каждый сервис собирается своим Dockerfile (контекст — корень репозитория):
нужны `CMakeLists.txt`, `libs/` и `services/<svc>/`.

userver подключается одним из двух способов (выбор автоматический в
`CMakeLists.txt`):

1. **find_package** — по умолчанию. Dockerfile стартует с образа
   `ghcr.io/userver-framework/ubuntu-22.04-userver` (userver предустановлен).
   Образ переопределяется через build-arg / env `USERVER_IMAGE`.
2. **submodule** — если положить userver в `third_party/userver`, сборка пойдёт
   из исходников (`git submodule update --init --recursive`).

Локальная сборка без Docker (нужен установленный userver):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Структура

```text
services/<svc>/        # сервисы (handlers/domain/repositories/clients/dto)
libs/common/           # узкий инфраструктурный каркас (errors/auth/http-client)
migrations/<svc>/      # SQL-миграции, по подпапке на сервис
docker/postgres/initdb # создание per-service БД при первом запуске
scripts/migrate.sh     # применение миграций в dev
docs/                  # PLAN, контракты, архитектура
```