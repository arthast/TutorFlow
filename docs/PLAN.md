# TutorFlow — Архитектура, домен и координация (план фундамента)

> Единый источник правды для двух агентов, разрабатывающих параллельно.
> Замораживает «стыки» (контракты, домен, auth), чтобы каждый агент писал код
> против контракта, а не против чужой реализации.
>
> **Git-политика:** в git коммитятся исходники + `docs/` (PLAN, контракты) +
> `AGENTS.md` + `README.md` + `.env.example`. Контракты — источник правды, видны
> в PR. Локальны только `.env`, `AGENTS.local.md` и `*.local.md` (см. `.gitignore`).

Статус: **черновик фундамента, до первого коммита в `main`.**
Дата: 2026-06-22.

---

## 1. Принятые решения

| Тема | Решение |
|---|---|
| Язык/фреймворк | C++20 + **userver** |
| Транспорт (MVP) | REST/HTTP между сервисами; gRPC и Kafka — позже, места размечаем |
| База данных | один контейнер PostgreSQL, **отдельная БД на сервис** + отдельная роль |
| Auth | identity подписывает JWT; **gateway валидирует JWT локально** общим секретом; per-request validate-token НЕ зовём (см. §5) |
| Роли | в MVP у пользователя **ровно одна роль**; в JWT `roles: [...]`, заголовок `X-User-Roles` (задел под мультироль) |
| Внутренние сервисы | наружу не торчат; снаружи доступен только gateway |
| Формат ошибок | единый envelope во всех сервисах (см. §6) |
| Finance | **журнал операций** (append-only): `financial_transactions` + `payment_receipts` (см. §8.4) |
| Charge при завершении занятия | создаёт **lesson-service** (не gateway), идемпотентно `unique(lesson_id)` (см. §8.2, §10) |
| Координация | отдельные git-ветки + отдельные рабочие папки (git worktree) |
| Старт | маленький **foundation-коммит в `main`**, затем оба ветвятся |
| Git-политика | в git: исходники + docs/ (PLAN, контракты) + AGENTS.md + README + .env.example; локальны только .env и *.local.md |
| Роли | **Lead = Agent A**; ownership: A = identity+file+gateway, B (Codex) = lesson+assignment+finance (см. §14) |

---

## 2. MVP-сценарии (что должно работать первым)

Главный приоритет. Сервисы считаются «правильными», только если проходят этот путь:

1. Teacher регистрируется / логинится.
2. Teacher создаёт ученика (или приглашает) и связь teacher↔student.
3. Student логинится, видит свою связь с teacher.
4. Teacher создаёт занятие (на слот/время) для ученика.
5. Teacher отмечает занятие как проведённое (`complete`).
6. lesson-service создаёт начисление `charge` в finance (идемпотентно).
7. Student видит свой баланс/долг.
8. Student загружает файл-чек оплаты (через file-service) и создаёт `payment_receipt` (`pending_review`).
9. Teacher подтверждает чек → finance создаёт операцию `payment` → баланс меняется.
10. Teacher создаёт домашнее задание ученику.
11. Student видит ДЗ, отправляет решение (текст и/или файл).
12. Teacher проверяет решение (review): статус + комментарий.

Критично: **простая загрузка чека баланс не меняет**; баланс меняется только после
подтверждения teacher (шаг 9).

---

## 3. Структура репозитория

```text
tutorflow/
  docker-compose.yml          # коммитится
  README.md                   # коммитится (минимальный)
  .env.example                # коммитится
  .gitignore                  # коммитится
  AGENTS.md                   # коммитится (общие правила)
  AGENTS.local.md             # ЛОКАЛЬНЫЙ (gitignore): личное назначение агента
  AGENTS.local.example.md     # коммитится (шаблон)
  docs/                       # коммитится
    PLAN.md                   # этот файл
    SETUP.md                  # инструкция по worktree/git
    architecture.md
    api-contracts/            # OpenAPI YAML — источник правды по эндпоинтам
      gateway.openapi.yaml
      identity.openapi.yaml
      lesson.openapi.yaml
      assignment.openapi.yaml
      finance.openapi.yaml
      file.openapi.yaml
  services/                   # коммитится
    api-gateway/
    identity-service/
    lesson-service/
    assignment-service/
    finance-service/
    file-service/
  libs/
    common/                   # коммитится
  migrations/                 # коммитится
    identity/  lesson/  assignment/  finance/  file/
  scripts/
    migrate.sh                # применение SQL-миграций в dev
```

Внутренняя структура сервиса (под SOLID, слои):

```text
services/<svc>/src/
  handlers/      # HTTP-ручки: разбор запроса/ответа, без бизнес-логики
  domain/        # сущности + доменные сервисы (бизнес-правила)
  repositories/  # доступ к своей БД (интерфейс + pg-реализация)
  clients/       # клиенты к другим сервисам (через libs/common http-client)
  dto/           # сериализация/десериализация
```

Поток: `handlers -> domain -> repositories`, исходящие — через `clients`.
Репозитории и клиенты за интерфейсами (DIP); реализация инжектится компонентом userver.

---

## 4. Карта портов и БД

| Сервис | Порт | Наружу | База данных |
|---|---|---|---|
| api-gateway | 8080 | **да** | — |
| identity-service | 8081 | нет | `identity_db` |
| lesson-service | 8082 | нет | `lesson_db` |
| assignment-service | 8083 | нет | `assignment_db` |
| finance-service | 8084 | нет | `finance_db` |
| file-service | 8085 | нет | `file_db` |
| postgres | 5432 | нет | (все базы) |

Внутренние URL по имени сервиса: `http://identity-service:8081`.

---

## 5. Auth-стык (заморожено)

1. Клиент шлёт `Authorization: Bearer <JWT>` только в gateway.
2. **identity-service** подписывает JWT (`JWT_SECRET`, TTL из конфига).
   Payload: `{ "sub": <user_id>, "roles": ["teacher"], "exp": ... }`.
3. **api-gateway валидирует JWT локально** тем же `JWT_SECRET`. identity на каждый
   запрос НЕ дёргается (validate-token остаётся как опциональный internal-эндпоинт,
   но в горячем пути не используется).
4. Gateway **обязан удалить любые входящие** `X-User-*` заголовки от клиента и
   выставить их заново только после успешной валидации:
   - `X-User-Id: <uuid>`
   - `X-User-Roles: teacher` (CSV, в MVP всегда одна роль)
5. Внутренние сервисы JWT не валидируют — доверяют `X-User-*` от gateway и
   проверяют только бизнес-доступ (через identity `check-access`, см. §8.1).

MVP-ограничение: **один пользователь = одна роль**. Мультироль не поддерживается
(но формат `roles[]`/`X-User-Roles` уже готов к ней).

---

## 6. Единый формат ошибок

```json
{ "error": { "code": "string_code", "message": "human readable", "details": {} } }
```

HTTP: 400 валидация, 401 нет/битый токен, 403 нет прав, 404 не найдено,
409 конфликт состояния, 422 бизнес-правило, 500 внутренняя.
Машинные коды — константы в `libs/common`, чтобы не расходились.

---

## 7. `libs/common` (узкий каркас)

Только инфраструктура, одинаковая для всех сервисов. Замораживается в foundation.

Входит:
- `errors` — тип ошибки, envelope-сериализация, перечень кодов, исключения.
- `auth_context` — парсинг `X-User-Id` / `X-User-Roles`, require-хелперы (teacher/student).
- `request_context` — correlation-id (задел под трейсинг), проброс между вызовами.
- `http_client_base` — обёртка над userver http-client: базовый URL из конфига,
  проброс `X-User-*` и correlation-id, единый разбор ошибок-envelope.
- `json` — мелкие хелперы (только если реально нужны).

**Не входит** (намеренно): `pg`-обёртка (userver уже даёт postgres-компоненты),
тяжёлый кастомный `log`. **Запрещено** класть сюда DTO и доменные сущности сервисов.

---

## 8. Доменные сущности и статусы (заморожено)

Источник правды по полям/эндпоинтам — `docs/api-contracts/*.openapi.yaml`.
Ниже — каноническая модель, чтобы агенты не расходились.

### 8.1 identity-service (`identity_db`)

```text
users(id, email UNIQUE, password_hash, role, created_at)
teacher_profiles(id, user_id, display_name, timezone, created_at)
student_profiles(id, user_id, display_name, created_at)
teacher_student_links(id, teacher_id, student_id, subject, goal,
                      hourly_rate, status, created_at)
```

Статусы связи: `invited | active | archived`.

Канонический способ проверки доступа (один на всех):

```http
POST /internal/relations/check-access
body: { "teacher_id": "...", "student_id": "..." }
resp: { "allowed": true, "status": "active" }
```

Прочие internal-эндпоинты: register, login, validate-token (опц.), users/{id},
students CRUD, teachers/{id}/students.

### 8.2 lesson-service (`lesson_db`) — расписание И занятия

Отвечает за: доступные слоты преподавателя, запланированные занятия,
отмену/перенос, завершение, снимок цены занятия.

```text
availability_slots(id, teacher_id, starts_at, ends_at, status, created_at)
lessons(id, teacher_id, student_id, slot_id?, starts_at, ends_at,
        status, topic, notes, price, created_at, completed_at)
```

Статус слота: `open | booked`.
Статус занятия: `scheduled | completed | cancelled`.

Цена: снимок `lessons.price` фиксируется при создании занятия (из
`teacher_student_links.hourly_rate` или явно). При завершении (`complete`)
lesson-service создаёт `charge` в finance с `amount = lessons.price`,
**идемпотентно** (повторный complete не создаёт дубль).

Перед созданием занятия — `check-access` в identity.

### 8.3 assignment-service (`assignment_db`)

```text
assignments(id, teacher_id, student_id, title, description, due_at,
            status, created_at)
assignment_files(id, assignment_id, file_id, created_at)
submissions(id, assignment_id, student_id, text_answer, status, submitted_at)
submission_files(id, submission_id, file_id, created_at)
assignment_comments(id, assignment_id, author_id, text, created_at)
```

Статус ДЗ: `assigned | submitted | reviewed | needs_fix | done | expired`.
Статус решения: `submitted | reviewed | needs_fix | accepted`.

Комментарии живут здесь же (это НЕ chat-service). Перед созданием ДЗ — `check-access`.
Файлы: сначала в file-service, в assignment хранится только `file_id`.

### 8.4 finance-service (`finance_db`) — журнал операций

Реальных платежей нет. Модель — **append-only журнал** + чеки.

```text
financial_transactions(id, teacher_id, student_id, type, amount, currency,
                       lesson_id?, receipt_id?, comment, created_at)
payment_receipts(id, teacher_id, student_id, file_id, amount, currency,
                 status, submitted_at, reviewed_at, reviewed_by, comment)
```

Тип операции: `charge | payment | correction | refund`.
Статус чека: `pending_review | confirmed | rejected`.

Баланс (долг ученика):
```text
balance = sum(charge) - sum(payment) + sum(correction) - sum(refund)
```

Правила:
- `charge` создаётся при завершении занятия (из lesson-service), `unique(lesson_id)`.
- Загрузка чека → `payment_receipt(pending_review)`; **баланс не меняется**.
- Подтверждение чека teacher → операция `payment(amount = receipt.amount)`;
  баланс меняется. Чек привязан к ученику, НЕ к одному начислению (оплата может
  быть наперёд/частичной).
- Операции не редактируем — только добавляем `correction`/`refund`.

### 8.5 file-service (`file_db`) — метаданные + локальный том

В MVP файлы лежат в локальном volume (`FILE_STORAGE_DIR`), в БД — только метаданные.

```text
files(id, owner_user_id, purpose, original_name, content_type,
      size_bytes, storage_key, created_at)
```

`purpose`: `assignment_attachment | submission_file | payment_receipt`
(`chat_message` — позже).

Ограничения: `FILE_MAX_SIZE_BYTES` (по умолчанию 10 МБ), белый список
content-type (изображения + pdf для чеков; уточним в контракте).

Правила доступа на скачивание:
- владелец (`owner_user_id == X-User-Id`) — да;
- teacher, связанный с владельцем-учеником (через identity `check-access`) — да;
- остальные — 403.

---

## 9. Контракты (OpenAPI)

Источник правды по эндпоинтам — `docs/api-contracts/<svc>.openapi.yaml`:
method, path, request/response body, коды ошибок, требования auth.
Markdown — только пояснения. Контракты **коммитятся** (видны в PR). Менять контракт —
отдельным согласованным шагом с подтверждением Lead и пингом второго агента.

---

## 10. Внутренние зависимости (вызовы только в эту сторону)

```text
lesson      -> identity            (check-access перед созданием занятия)
lesson      -> finance             (создать charge при complete, идемпотентно)
assignment  -> identity, file
finance     -> identity, file
gateway     -> все
```

Gateway — **тонкий**: публичный HTTP API, auth, роутинг, при необходимости
агрегация на чтение. **Бизнес-оркестрации в gateway нет** (charge инициирует
lesson-service, а не gateway).

---

## 11. Миграции (заморожено)

- SQL-файлы: `migrations/<service>/NNN_name.sql` (напр. `001_init.sql`).
- Применение в dev: `scripts/migrate.sh` через `psql` (по сервису/по всем).
- Правило: агент трогает **только** `migrations/<свой-сервис>/`.
- Каждая миграция идемпотентна по возможности (`IF NOT EXISTS`).

---

## 12. Состав foundation-коммита (в `main`)

Цель: ~30–60 минут, после чего оба агента ветвятся и пилят параллельно.

1. Дерево каталогов из §3 (пустые сервисы с `/health`).
2. `docker-compose.yml`: postgres + 6 сервисов, сеть, тома, наружу только gateway.
3. `libs/common` (§7) — рабочий каркас errors/auth_context/http_client_base.
4. Шаблон `CMakeLists.txt` + `Dockerfile` сервиса (эталон, остальные копируют).
5. `migrations/<svc>/001_init.sql` (первичные схемы из §8) + `scripts/migrate.sh`.
6. `.env.example`, `.gitignore`, минимальный `README.md`.

> Примечание: docs/ и api-contracts коммитятся вместе с фундаментом, поэтому
> оба worktree получают их из git автоматически.

---

## 13. Рабочий процесс (две папки, две ветки)

- На одном компе: каждому агенту своя папка через `git worktree`
  (`git worktree add ../TutorFlow-agentB -b feat/agentB-...`). Общая история,
  отдельные рабочие деревья.
- Ветки: `feat/<svc>-<кратко>`. Один сервис = один владелец за раз.
- Не лезем в чужой `services/<svc>/` и чужую `migrations/<svc>/`.
- Перед PR: `git pull --rebase origin main`. Мерж в `main` через PR.
- Контракты и публичные сигнатуры `libs/common` меняем только согласованно.

---

## 14. Ownership

```text
Lead (главный): Agent A — слово за ним по контрактам и кросс-сервисным спорам.

Agent A (Claude Code):
  - identity-service
  - file-service
  - api-gateway (auth + routing)

Agent B (Codex):
  - lesson-service
  - assignment-service
  - finance-service
```

Каждый агент получает явное назначение через `AGENTS.local.md` (не предполагает
роль по умолчанию). Agent B не ждёт реальный identity/file — пишет против контракта
и использует stub/mock-клиент, переключается на реальные при готовности.

---

## 15. Критерии готовности сервиса (не только /health)

1. `GET /health -> {"status":"ok"}`.
2. Миграции применяются из чистой БД.
3. Основные эндпоинты проходят smoke-тест (happy-path из §2).
4. Ошибки — в едином envelope (§6).
5. Порт наружу не публикуется (кроме gateway).

---

## 16. Замороженные общие файлы (риск коллизий)

```text
docker-compose.yml          # добавление сервиса — координируем порты/имена
libs/common/** (public)     # сигнатуры меняем согласованным шагом
docs/api-contracts/**        # источник правды по контрактам (в git, меняем через Lead)
migrations/ (общая папка)   # каждый агент — только своя подпапка <svc>/
.env.example
```

---

## 17. Задел под будущее (НЕ реализуем в MVP)

Места под Kafka-события помечаем `// TODO(event): <name>` уже сейчас:

```text
lesson_completed          (сейчас: lesson-service -> finance POST /internal/charges)
payment_receipt_uploaded
payment_confirmed
assignment_created
submission_uploaded
assignment_reviewed
```

Не делаем в MVP: chat/notification/report-сервисы, Kafka, Redis, Telegram,
Google Calendar, реальные платежи, frontend, родительский аккаунт,
повторяющиеся занятия. Комментарии к ДЗ в MVP — внутри assignment-service
(это не chat-service).
