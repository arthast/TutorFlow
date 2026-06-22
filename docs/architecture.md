# TutorFlow — архитектура (обзор)

> Источник правды — `docs/PLAN.md` (домен, решения) и
> `docs/api-contracts/*.openapi.yaml` (эндпоинты). Этот файл — карта верхнего
> уровня, не дублирует детали.

## Стиль

Микросервисы на C++20 + userver, REST между сервисами, PostgreSQL (отдельная БД
на сервис). Наружу торчит только `api-gateway`. Подробности — PLAN §1.

## Сервисы и порты (PLAN §4)

| Сервис | Порт | Наружу | БД | Owner |
|---|---|---|---|---|
| api-gateway | 8080 | да | — | A |
| identity-service | 8081 | нет | identity_db | A |
| lesson-service | 8082 | нет | lesson_db | B |
| assignment-service | 8083 | нет | assignment_db | B |
| finance-service | 8084 | нет | finance_db | B |
| file-service | 8085 | нет | file_db | A |

## Топология

```text
        client
          │  Authorization: Bearer <JWT>
          ▼
   ┌──────────────┐   валидирует JWT локально, срезает и проставляет X-User-*
   │  api-gateway │
   └──────┬───────┘
          │ internal REST (X-User-Id, X-User-Roles, X-Request-Id)
   ┌──────┼───────────────┬───────────────┬───────────────┐
   ▼      ▼               ▼               ▼               ▼
identity  lesson      assignment       finance          file
  │        │  check-access│  check-access  │  check-access │
  │        │   ┌──────────┘                │               │
  │        │   ▼                           │               │
  │        └─► identity                    │               │
  │        │                               │               │
  │        └─ charge (complete) ──────────►finance         │
  │                                        │               │
  └────────────────── postgres (БД на сервис) ─────────────┘
```

## Слои сервиса (PLAN §3)

```text
handlers/      HTTP-ручки: разбор запроса/ответа, без бизнес-логики
domain/        сущности + доменные сервисы (бизнес-правила)
repositories/  доступ к своей БД (интерфейс + pg-реализация)
clients/       клиенты к другим сервисам (через libs/common HttpClientBase)
dto/           сериализация/десериализация
```

Поток: `handlers -> domain -> repositories`; исходящие — через `clients`.
Репозитории и клиенты за интерфейсами (DIP), реализация инжектится компонентом
userver.

## Сквозные правила (см. AGENTS.md, PLAN §5–§10)

- **Auth.** identity подписывает JWT; gateway валидирует локально тем же
  секретом; per-request validate-token не зовём.
- **Границы БД.** Сервис ходит только в свою БД. Чужие данные — через internal
  HTTP API. JOIN между БД запрещён.
- **Связь teacher↔student** — только через identity `check-access`.
- **Файлы** — только через file-service; в других сервисах хранится `file_id`.
- **charge** создаёт lesson-service при `complete`, идемпотентно
  `unique(lesson_id)` (частичный индекс в finance).
- **Баланс** меняется только после подтверждения чека teacher.
- **Ошибки** — единый envelope, коды из `libs/common`.

## libs/common (PLAN §7)

Узкий инфраструктурный каркас: `errors` (envelope + коды), `auth_context`
(`X-User-*`), `request_context` (correlation-id), `http_client_base`,
`health_handler`. Подробности — `libs/common/README.md`.

## Задел под будущее (PLAN §17)

Места под Kafka-события помечаем `// TODO(event): <name>` уже сейчас
(`lesson_completed`, `payment_confirmed`, …). В MVP событий нет — синхронный REST.
```
