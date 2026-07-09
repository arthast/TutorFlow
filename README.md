# TutorFlow

TutorFlow — учебная платформа для связки «преподаватель — ученик». MVP покрывает
регистрацию и вход, учеников преподавателя, расписание и занятия, домашние задания,
файлы, чеки об оплате с ручным подтверждением, финансовый журнал, dashboards,
in-app уведомления, личный чат и realtime push.

Проект сделан как **microservices** на **C++20 + userver**.
Это один управляемый backend/frontend проект
с чёткими границами сервисов, отдельными БД и контрактами.

Production demo:

- App: `https://netwatch-arsen-demo.ru`

## Статус

Реализовано:

- Публичный REST API через `api-gateway`.
- Публичный WebSocket push через `realtime-service`.
- Внутренний gRPC между gateway и доменными сервисами.
- Kafka events через transactional outbox + consumer inbox.
- PostgreSQL с отдельной БД на каждый stateful service.
- Redis для realtime presence/pub-sub.
- MinIO/S3-compatible storage для файлов, с fallback на локальный storage.
- CI: frontend build, prod compose config, pytest collection.
- CD: сборка образов, публикация в GHCR, deploy на сервер.

## Архитектура

```text
Browser / Frontend
  | REST/JSON, multipart upload
  v
api-gateway                         realtime-service
public HTTPS REST                   public WSS /ws push only
  |                                  ^
  | gRPC                             | Kafka + Redis pub/sub
  v                                  |
identity-service      notification-service
lesson-service        report-service
assignment-service    chat-service
finance-service
  |
  | HTTP multipart
  v
file-service

PostgreSQL: separate database per service
Kafka: domain events and side effects
Redis: realtime fan-out/presence
MinIO: object storage for uploaded files
```

Главные правила:

- Внешние доменные команды идут только через `api-gateway`.
- `realtime-service` наружу открыт только для WebSocket push; он не является
  source of truth и не выполняет доменные команды.
- У каждого сервиса своя БД; прямое чтение чужих БД запрещено.
- Синхронные внутренние запросы идут по gRPC.
- Файлы идут через `file-service`; остальные сервисы хранят только `file_id`.
- Асинхронные факты идут через Kafka domain topics:
  `tutorflow.lesson.events`, `tutorflow.assignment.events`,
  `tutorflow.finance.events`, `tutorflow.chat.events`,
  `tutorflow.notification.events`; конкретный факт хранится в `event_type`.
- Финансы append-only: операции не редактируются, исправления делаются
  корректирующими транзакциями.

## Транспорт

| Транспорт | Где используется | Зачем |
|---|---|---|
| REST/JSON | frontend -> `api-gateway` | публичный API для UI |
| HTTP multipart | `api-gateway` -> `file-service` | upload/download файлов без gRPC-обвязки |
| gRPC | gateway -> доменные сервисы, сервисы -> identity | синхронные внутренние команды и чтения |
| Kafka | outbox/inbox между сервисами | факты, side effects, read-models, уведомления |
| WebSocket | frontend -> `realtime-service` | push чата, read markers, presence, notifications |
| Redis pub/sub | внутри `realtime-service` | fan-out между соединениями/инстансами |

Правило выбора простое: если вызывающему нужен ответ прямо сейчас — gRPC. Если
это факт «уже произошло» и на него должны реагировать другие сервисы — Kafka.

## Сервисы

| Сервис | Назначение | State |
|---|---|---|
| `api-gateway` | публичный REST facade: auth, routing, mapping, CORS, единый error envelope | stateless |
| `identity-service` | пользователи, JWT, роли, teacher/student profiles, связи teacher-student | `identity_db` |
| `lesson-service` | расписание, занятия, жизненный цикл lesson, материалы занятия | `lesson_db` |
| `assignment-service` | ДЗ, submissions, review, comments, deadline worker | `assignment_db` |
| `finance-service` | charges, receipts, payments, balance, corrections, finance events | `finance_db` |
| `file-service` | metadata файлов, upload/download, local/S3 storage abstraction | `file_db` + local/MinIO |
| `notification-service` | in-app уведомления из Kafka events, mark-as-read | `notification_db` |
| `report-service` | read-models и dashboards для teacher/student | `report_db` |
| `chat-service` | диалоги teacher-student, сообщения, вложения, read markers | `chat_db` |
| `realtime-service` | WebSocket push для сообщений/уведомлений/presence | Redis + memory |
| `frontend` | пользовательский UI teacher/student | stateless static SPA |

### api-gateway

`api-gateway` — единственная публичная REST-точка для доменных операций. Он
валидирует JWT локально, вычищает входящие `X-User-*`, выставляет доверенные
`X-User-Id` / `X-User-Roles` и дальше вызывает внутренние сервисы.

Внутри:

- `handlers/` — health и proxy handlers для публичных endpoint'ов;
- `clients/` — gRPC clients к identity/lesson/assignment/finance/notification/report/chat;
- `cors.*` — CORS policy для frontend;
- `gateway_settings.*` — runtime settings;
- file upload/download проксируется в `file-service` по HTTP multipart.

Gateway не содержит бизнес-логики: он не создаёт charge, не меняет статусы lessons
и не читает чужие БД.

### identity-service

`identity-service` отвечает за identity/auth домен:

- регистрация и login;
- password hashing;
- выпуск JWT;
- роли `teacher` / `student`;
- профили преподавателей и учеников;
- связь teacher-student;
- `CheckTeacherStudentAccess` для остальных сервисов.

Внутри:

- `domain/` — правила регистрации, логина, смены пароля, связей;
- `repositories/` — доступ только к `identity_db`;
- `grpc/` — внутренний gRPC API для gateway и access checks.

Это source of truth для пользователей и доступа teacher к student.

### lesson-service

`lesson-service` управляет расписанием и занятиями:

- создание lessons;
- завершение занятия;
- отмена, перенос, восстановление;
- хранение `file_id` материалов занятия;
- outbox событий `lesson.scheduled`, `lesson.completed`, `lesson.cancelled`,
  `lesson.restored`, `lesson.rescheduled`.

Внутри:

- `domain/` — state machine занятия и доменные проверки;
- `repositories/` — `lesson_db`, lessons, slots, materials, outbox;
- `grpc/` — команды и чтения для gateway;
- `outbox/` — публикация lesson events в Kafka.

Завершение занятия не создаёт charge напрямую. Сервис фиксирует lesson status и
пишет событие в outbox, а `finance-service` создаёт charge асинхронно.

### assignment-service

`assignment-service` отвечает за домашние задания:

- создание assignment преподавателем;
- просмотр заданий учеником;
- submission с текстом и/или `file_id`;
- review преподавателем;
- comments;
- deadline expiration.

Внутри:

- `domain/` — статусы assignment/submission и проверки прав;
- `repositories/` — `assignment_db`;
- `grpc/` — внутренний API для gateway;
- `outbox/` — события `assignment.created`, `submission.uploaded`,
  `assignment.reviewed`, `assignment.deadline_expired`;
- `workers/` — deadline worker.

Сами файлы решений не хранятся в assignment DB, только ссылки `file_id`.

### finance-service

`finance-service` ведёт финансовую модель MVP:

- charge за проведённое занятие;
- загрузка payment receipt;
- подтверждение/отклонение чека преподавателем;
- append-only ledger;
- ручные и автоматические corrections;
- balance calculation;
- finance events.

Внутри:

- `domain/` — правила charge/payment/correction/refund;
- `repositories/` — `finance_db`, ledger, receipts, inbox/outbox;
- `grpc/` — команды и чтения для gateway;
- `consumers/` — Kafka consumers lesson events;
- `outbox/` — публикация `charge.created`, `payment_receipt.uploaded`,
  `payment.confirmed`, `payment.rejected`, `balance.changed`.

Ключевое правило: загруженный чек сам по себе не меняет баланс. Баланс меняется
только после подтверждения teacher.

### file-service

`file-service` отделяет metadata файлов от физического storage:

- upload/download через HTTP multipart;
- metadata в `file_db`;
- проверка доступа через identity;
- storage backend `local` или `s3`;
- единый внешний API независимо от backend'а.

Внутри:

- `handlers/` — HTTP upload/download endpoints для gateway;
- `domain/` — правила ownership/access;
- `repositories/` — metadata в `file_db`;
- `storages/` — `IFileStorage`, local storage и S3/MinIO storage.

Другие сервисы не получают файл напрямую и не хранят blob'ы у себя.

### notification-service

`notification-service` строит in-app уведомления из доменных событий:

- слушает Kafka events lesson/assignment/finance/chat;
- создаёт уведомления в `notification_db`;
- отдаёт список уведомлений через gRPC;
- поддерживает mark-as-read;
- публикует `notification.created` для realtime push.

Внутри:

- `consumers/` — Kafka event consumer;
- `domain/` — правила генерации уведомлений;
- `repositories/` — notification storage;
- `grpc/` — чтение и mark-as-read;
- `outbox/` — notification events.

Это read/notification projection, а не source of truth для домена.

### report-service

`report-service` строит read-models для dashboards:

- teacher dashboard;
- student dashboard;
- summary по student;
- агрегаты по lessons/assignments/finance.

Внутри:

- `consumers/` — Kafka consumers lesson/assignment/finance events;
- `domain/` — обновление read-models;
- `repositories/` — `report_db`;
- `grpc/` — dashboard API для gateway.

Если read-model расходится с доменными сервисами, истина остаётся в source-of-truth
сервисах. Report можно пересобрать/replay'нуть из событий.

### chat-service

`chat-service` отвечает за личную переписку teacher-student:

- диалоги;
- сообщения;
- attachments через `file_id`;
- read markers;
- события `message.sent` и `message.read`.

Внутри:

- `domain/` — доступ к диалогу, отправка, чтение;
- `repositories/` — `chat_db`;
- `grpc/` — команды/чтения для gateway;
- `outbox/` — Kafka events для уведомлений и realtime.

Отправка сообщения идёт через REST -> gateway -> gRPC -> chat-service. WebSocket
только доставляет push о событии, но не является каналом записи.

### realtime-service

`realtime-service` держит публичные WebSocket-соединения:

- endpoint `/ws?token=...`;
- JWT validation;
- connection registry;
- presence;
- Kafka consumer для `message.*` и `notification.created`;
- Redis pub/sub fan-out;
- отправка push-событий в открытые клиентские соединения.

Внутри:

- `ws/` — WebSocket handler и registry соединений;
- `kafka/` — consumer событий;
- `redis/` — pub/sub client;
- память процесса — активные соединения.

Он намеренно stateless относительно домена: после reconnect клиент должен уметь
добрать состояние через REST/gateway.

### frontend

Frontend — React + TypeScript + Vite SPA. Он работает только с публичными входами:

- REST API: `VITE_API_URL`;
- WebSocket: `VITE_REALTIME_URL`.

Внутри:

- `src/api.ts` — REST client;
- `src/auth.tsx` — auth context;
- `src/realtime.tsx` — WebSocket lifecycle;
- `src/chat.tsx` — chat state helpers;
- `src/pages/` — teacher/student pages;
- `src/ui.tsx` — reusable UI primitives.

## Данные И Границы БД

В dev/prod используется один PostgreSQL контейнер, но разные базы:

```text
identity_db
lesson_db
assignment_db
finance_db
file_db
notification_db
report_db
chat_db
```

Сервис подключается только к своей БД. Межсервисные связи хранятся как stable ids
(`user_id`, `student_id`, `lesson_id`, `file_id`) и проверяются через service API,
а не JOIN'ами между БД.

Миграции лежат в `migrations/<service>/`. В compose их применяет one-shot
`migrator`; локально можно запустить `./scripts/migrate.sh all`.

## Событийная Модель

Сервисы публикуют события через outbox:

1. Доменная операция и запись в `outbox_events` происходят в одной DB transaction.
2. Publisher читает outbox и отправляет event в Kafka.
3. Consumer принимает event, пишет `consumer_inbox`/idempotency marker и обновляет
   своё состояние.
4. Повторная доставка Kafka не должна создавать дубли.

Примеры:

```text
lesson.completed -> finance-service -> charge.created -> report/notification
payment.confirmed -> balance.changed -> report
message.sent -> notification-service + realtime-service
notification.created -> realtime-service -> WebSocket client
```

Контракты событий: `docs/event-contracts/`.

## Auth И Ошибки

JWT выпускает `identity-service`, а валидирует на внешнем периметре `api-gateway`.
Внутренние сервисы получают доверенные headers от gateway и делают доменные проверки.

Единый формат ошибок:

```json
{"error":{"code":"string_code","message":"human readable","details":null}}
```

Коды и envelope helpers лежат в `libs/common`.

## Общие Библиотеки

```text
libs/common   errors, auth context, JWT, request context, HTTP helpers
libs/proto    protobuf/gRPC contracts and userver codegen
libs/clients  shared gRPC client helpers
libs/events   Kafka envelope, publisher, consumer, outbox helpers
```

`libs/common` не содержит DTO и доменных сущностей сервисов.

## Структура Репозитория

```text
services/
  api-gateway/
  identity-service/
  lesson-service/
  assignment-service/
  finance-service/
  file-service/
  notification-service/
  report-service/
  chat-service/
  realtime-service/
libs/
  common/
  clients/
  proto/
  events/
migrations/
frontend/
scripts/
deploy/
docker/postgres/initdb/
docs/
```

Типовая структура C++ сервиса:

```text
src/
  handlers/       HTTP handlers where needed
  grpc/           gRPC service implementation
  domain/         business rules and models
  repositories/   own database access
  clients/        outgoing service clients, if local to service
  consumers/      Kafka consumers
  outbox/         event publishing workers
  workers/        background workers
  storages/       storage adapters where relevant
```

Не у каждого сервиса есть все папки. Например, `realtime-service` использует
`ws/`, `kafka/`, `redis/`, а `file-service` использует `storages/`.

## Локальный Запуск

```bash
cp .env.example .env
COMPOSE_PARALLEL_LIMIT=1 docker compose build
docker compose up -d
curl http://localhost:8080/health
```

Для локального dev/demo кластера Kafka из трёх брокеров: `docker compose down -v && docker compose -f docker-compose.yml -f docker-compose.scale.yml up --build -d`. Команда намеренно удаляет локальные volumes: KRaft metadata одноузлового и трёхузлового кворумов несовместимы.

Миграции применяются автоматически через `migrator`. Ручной прогон:

```bash
./scripts/migrate.sh all
```

Сброс dev-окружения:

```bash
docker compose down -v
```

Frontend отдельно:

```bash
cd frontend
npm install
npm run dev
```

По умолчанию:

- frontend: `http://localhost:5173`
- gateway: `http://localhost:8080`
- realtime: `ws://localhost:8089/ws`

## Проверки

Smoke через внешний gateway:

```bash
python3 scripts/smoke_mvp.py
```

Pytest:

```bash
python3 -m pytest tests
```

Frontend build:

```bash
cd frontend
npx vite build
```

Production compose config:

```bash
docker compose --env-file deploy/.env.prod.example -f docker-compose.prod.yml config
```

Smoke против production demo:

```bash
GATEWAY_URL=https://netwatch-arsen-demo.ru python3 scripts/smoke_mvp.py
```

## Deploy

CI/CD workflow:

```text
push/manual dispatch
  -> GitHub Actions
  -> build Docker images
  -> push to GHCR
  -> SSH to server
  -> docker compose pull
  -> docker compose up -d
```

На сервере проект живёт в `/opt/tutorflow`. Production compose использует:

- `docker-compose.prod.yml`;
- `/opt/tutorflow/.env`;
- `deploy/Caddyfile`;
- GHCR images tagged by commit SHA.

Caddy routing:

```text
/ws*                         -> realtime-service:8089
/auth, /lessons, /files, ... -> api-gateway:8080
/*                           -> frontend:80
www                          -> redirect to apex domain
```
