# TutorFlow — Архитектура

Backend платформы «репетитор ↔ ученик»: расписание и занятия, домашние задания
со сдачей/проверкой, файлы, чеки об оплате с ручным подтверждением, финансовый
учёт, in-app уведомления, дашборды, личный чат и realtime push-канал.

Стиль — **microservices-lite** в одном монорепозитории: несколько узких сервисов
с чёткими границами, но без преждевременной инфраструктурной тяжести. Стек:
**C++20 + userver**, PostgreSQL (отдельная БД на сервис), Kafka, Docker Compose.

> Детали: домен/контракты — `docs/PLAN.md` и `docs/api-contracts/*`; события —
> `docs/EVENTS.md`; финансы — `docs/FINANCE_MODEL.md`; решения — `docs/adr/`.

## Компоненты

Снаружи открыты `api-gateway` (8080) и `realtime-service` (8089). Realtime —
осознанное исключение для stateful WebSocket-соединений; доменные команды всё
равно идут только через gateway.

| Компонент | HTTP | gRPC | БД | Роль |
|---|---:|---:|---|---|
| api-gateway | 8080 | — | — | публичный REST-фасад: auth, роутинг, маппинг |
| identity-service | 8081 | 9081 | `identity_db` | пользователи, JWT, связи teacher↔student |
| lesson-service | 8082 | 9082 | `lesson_db` | слоты, занятия, жизненный цикл |
| assignment-service | 8083 | 9083 | `assignment_db` | ДЗ, сдачи, review, комментарии |
| finance-service | 8084 | 9084 | `finance_db` | append-only журнал, чеки, баланс |
| file-service | 8085 | — | `file_db` | метаданные + хранение (local/S3) |
| notification-service | 8086 | 9086 | `notification_db` | in-app уведомления из событий |
| report-service | 8087 | 9087 | `report_db` | read-models, дашборды |
| chat-service | 8088 | 9088 | `chat_db` | личная переписка teacher↔student |
| realtime-service | 8089 | — | — | WebSocket push для чата/уведомлений |

Инфраструктура: PostgreSQL, Kafka (KRaft) + kafka-ui, Redis, MinIO (S3), one-shot
`migrator`. Frontend — React + TypeScript + Vite.

Операционные HTTP-пробы разведены по смыслу: `/health` — лёгкий liveness без
обращений к внешним зависимостям; `/ready` — readiness для балансировщиков и
compose/k8s, проверяющий только собственные критичные зависимости сервиса
(своя БД, Redis у realtime, S3 bucket у file-service в S3-режиме). Kafka и
чужие сервисы в `/ready` не входят: consumers/producers ретраятся сами.

## Транспорт (разведён по смыслу)

```
REST  — внешний API: frontend → api-gateway; файлы gateway → file-service (multipart)
gRPC  — синхронные внутренние вызовы: ответ нужен сейчас (check-access, get/create, dashboards)
Kafka — асинхронные доменные факты и побочные эффекты (через outbox/inbox)
```

Правило выбора: нужен ответ немедленно → gRPC; факт «уже произошло» с побочным
эффектом → Kafka. Access-check и команды через Kafka не делаем; file upload по
gRPC не гоняем (остаётся multipart).

## Граф вызовов

```
gateway → identity/lesson/assignment/finance/notification/report/chat   (gRPC)
gateway → file                                                          (HTTP multipart)
lesson/assignment/finance/file → identity                              (gRPC check-access)
lesson  → Kafka (lesson.*)      assignment → Kafka (assignment.*/submission.*)
finance ↔ Kafka (consumer lesson.completed/cancelled/restored; producer finance.*)
chat    → Kafka (message.*)
notification ← Kafka (почти все события → уведомления)
notification → Kafka (notification.created)
report       ← Kafka (lesson.*/assignment.*/finance.* → read-models)
realtime     ← Kafka (message.sent/read, notification.created) → Redis pub/sub → WebSocket
```

## Auth

1. Клиент шлёт `Authorization: Bearer <JWT>` только в gateway.
2. identity подписывает JWT (`JWT_SECRET`, пароли PBKDF2).
3. **gateway валидирует JWT локально** тем же секретом — per-request к identity не ходит.
4. Gateway удаляет любые входящие `X-User-*` и заново ставит `X-User-Id` / `X-User-Roles`.
5. Внутренние сервисы JWT не валидируют — доверяют `X-User-*` от gateway и проверяют
   бизнес-доступ через identity `CheckTeacherStudentAccess`.

В MVP — одна роль на пользователя. Refresh-токены, invite-flow, reset password,
email verification — пока нет (см. `docs/roadmap.md`).

## Единый формат ошибок

Все сервисы возвращают единый envelope; машинные коды — константы в `libs/common`:

```json
{ "error": { "code": "string_code", "message": "human readable", "details": {} } }
```

HTTP: 400 валидация, 401 нет/битый токен, 403 нет прав, 404 не найдено,
409 конфликт состояния, 422 бизнес-правило, 500 внутренняя.

## Ключевые принципы

1. Внешние доменные запросы — только через gateway; realtime-service наружу
   открыт только для WebSocket push, внутренние доменные сервисы наружу не торчат.
2. **Gateway тонкий**: auth, routing, mapping, лёгкая агрегация на чтение. Бизнес-логики,
   создания charge и смены доменных статусов в gateway нет.
3. **Граница БД**: каждый сервис работает только со своей БД; чужую не читает, JOIN между
   БД сервисов запрещён. Чужие данные — только через сервисный API (gRPC; file — multipart).
4. Связь teacher↔student проверяется ТОЛЬКО через identity `CheckTeacherStudentAccess`.
5. Файлы — только через file-service; в других сервисах хранится `file_id`.
6. **Finance append-only**: операции не редактируем/не удаляем — добавляем корректирующие
   (`docs/FINANCE_MODEL.md`).
7. **Идемпотентность** на горячих путях: повтор/replay не ломает состояние (`docs/EVENTS.md`).
8. Read-models (report) — не source of truth; при расхождении истина в доменных сервисах.

## Слои сервиса

```
handlers/ | grpc/   разбор запроса/ответа, без бизнес-логики
domain/             сущности + доменные сервисы (бизнес-правила)
repositories/       доступ к своей БД (интерфейс + pg-реализация)
clients/            вызовы других сервисов
```

Поток: `handlers/grpc → domain → repositories`; исходящие — через `clients`.
Репозитории и клиенты за интерфейсами (DIP), реализация инжектится компонентом userver.

## libs/

`common` — инфраструктура (errors/envelope, auth_context, request_context,
http_client_base, jwt). `proto` — protobuf/gRPC контракты `*.v1`. `clients` — общие
gRPC client/server-утилиты. `events` — Kafka envelope, publisher/consumer + outbox.

## Что вне MVP

Production hardening (reverse proxy, CI/CD, метрики, Kafka lag/DLQ, бэкапы),
внешняя доставка уведомлений (email/Telegram/push), полноценный auth, реальные
платежи. Очередь работ — `docs/roadmap.md`.

> Решение «identity = auth + user» (а не два отдельных сервиса) — осознанное;
> обоснование в `docs/adr/0001-identity-combines-auth-and-user.md`.
