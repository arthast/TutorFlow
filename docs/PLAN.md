# TutorFlow — Архитектура, домен и координация

> Единый источник правды для двух агентов, разрабатывающих параллельно.
> Замораживает «стыки» (контракты, домен, auth), чтобы каждый агент писал код
> против контракта, а не против чужой реализации.
>
> **Git-политика:** в git коммитятся исходники + `docs/` (PLAN, контракты) +
> `AGENTS.md` + `README.md` + `.env.example`. Контракты — источник правды, видны
> в PR. Локальны только `.env`, `AGENTS.local.md` и `*.local.md` (см. `.gitignore`).

Статус: **MVP-ядро реализовано: 6 сервисов + gateway + frontend, gRPC между
сервисами, Kafka/outbox для `lesson.completed -> charge`.**
Модель координации: агенты включаются поочерёдно, задачи назначает человек (см. §13–§14).
Дата: 2026-06-24.

---

## 1. Принятые решения

| Тема | Решение |
|---|---|
| Язык/фреймворк | C++20 + **userver** |
| Транспорт | **REST** снаружи (frontend→gateway) и файлы (gateway→file, multipart); **gRPC** для синхронных внутренних вызовов (gateway→сервисы, *→identity check-access); **Kafka** для событий — внедрён `lesson.completed` (outbox→Kafka→finance charge). Внедрено на Этапе 5 roadmap. |
| База данных | один контейнер PostgreSQL, **отдельная БД на сервис** + отдельная роль |
| Auth | identity подписывает JWT; **gateway валидирует JWT локально** общим секретом; per-request validate-token НЕ зовём (см. §5) |
| Роли | в MVP у пользователя **ровно одна роль**; в JWT `roles: [...]`, заголовок `X-User-Roles` (задел под мультироль) |
| Внутренние сервисы | наружу не торчат; снаружи доступен только gateway |
| Формат ошибок | единый envelope во всех сервисах (см. §6) |
| Finance | **журнал операций** (append-only): `financial_transactions` + `payment_receipts` (см. §8.4) |
| Charge при завершении занятия | `lesson-service` фиксирует `lesson.completed` в outbox; `finance-service` consumer создаёт `charge` из Kafka идемпотентно `unique(lesson_id)` (см. §8.2, §10, roadmap 5E) |
| Координация | один репозиторий, одна рабочая папка; агенты включаются **поочерёдно** (по одному за раз), задачу на сессию назначает человек |
| Старт | MVP-ядро собрано; следующий фокус — UI gaps и расширение event-driven слоя (roadmap 5F+) |
| Git-политика | в git: исходники + docs/ (PLAN, контракты) + AGENTS.md + README + .env.example; локальны только .env и *.local.md |
| Роли | **Координатор — Claude** (контракты, PLAN, ревью, интеграция). Привязки «сервис → агент» нет: исполнителя на каждую задачу выбирает человек, оба агента взаимозаменяемы (см. §14) |

---

## 2. MVP-сценарии (что должно работать первым)

Главный приоритет. Сервисы считаются «правильными», только если проходят этот путь:

1. Teacher регистрируется / логинится.
2. Teacher создаёт ученика (или приглашает) и связь teacher↔student.
3. Student логинится, видит свою связь с teacher.
4. Teacher создаёт занятие (на слот/время) для ученика.
5. Teacher отмечает занятие как проведённое (`complete`).
6. lesson-service публикует `lesson.completed`, finance-service создаёт `charge`
   асинхронно через Kafka consumer (идемпотентно).
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
    common/                   # инфраструктура, ошибки, auth/helpers
    proto/                    # protobuf/gRPC контракты и codegen
    clients/                  # общие gRPC clients/wrappers
    events/                   # Kafka envelope, publisher/consumer helpers
  frontend/                   # React + TypeScript + Vite demo UI
  migrations/                 # коммитится
    identity/  lesson/  assignment/  finance/  file/
  scripts/
    migrate.sh                # применение SQL-миграций в dev
```

Внутренняя структура сервиса (под SOLID, слои):

```text
services/<svc>/src/
  handlers/      # внешние/legacy HTTP-ручки там, где они есть
  grpc/          # gRPC service implementation для внутренних синхронных вызовов
  domain/        # сущности + доменные сервисы (бизнес-правила)
  repositories/  # доступ к своей БД (интерфейс + pg-реализация)
  clients/       # локальные клиенты только если нельзя вынести в libs/clients
  dto/           # сериализация/десериализация
```

Поток: `handlers -> domain -> repositories`, исходящие — через `clients`.
Репозитории и клиенты за интерфейсами (DIP); реализация инжектится компонентом userver.

---

## 4. Карта портов и БД

| Сервис | HTTP | gRPC | Наружу | База данных |
|---|---:|---:|---|---|
| api-gateway | 8080 | — | **да** | — |
| identity-service | 8081 | 9081 | нет | `identity_db` |
| lesson-service | 8082 | 9082 | нет | `lesson_db` |
| assignment-service | 8083 | 9083 | нет | `assignment_db` |
| finance-service | 8084 | 9084 | нет | `finance_db` |
| file-service | 8085 | — | нет | `file_db` |
| postgres | 5432 | — | нет | (все базы) |
| kafka | 9092 | — | нет | — |

Снаружи доступен только gateway. Внутренние синхронные доменные вызовы идут по
gRPC; file upload/download остаётся HTTP multipart.

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

```text
gRPC IdentityService.CheckTeacherStudentAccess
req:  { teacher_id: "...", student_id: "..." }
resp: { allowed: true, status: "active", hourly_rate?: 1000.0 }
```

Gateway вызывает identity по gRPC для register/login/change-password/users/students.
Legacy/internal REST identity endpoints не считаются основным путём и не должны
использоваться в новых интеграциях.

### 8.2 lesson-service (`lesson_db`) — расписание И занятия

Отвечает за: доступные слоты преподавателя, запланированные занятия,
отмену/перенос, завершение, снимок цены занятия.

```text
availability_slots(id, teacher_id, starts_at, ends_at, status, created_at)
lessons(id, teacher_id, student_id, slot_id?, starts_at, ends_at,
        status, topic, notes, price, created_at, completed_at)
lesson_files(id, lesson_id, file_id, created_at)
```

Статус слота: `open | booked`.
Статус занятия: `scheduled | completed | cancelled`.

Цена: снимок `lessons.price` фиксируется при создании занятия (из
`teacher_student_links.hourly_rate` или явно). При завершении (`complete`)
lesson-service меняет статус и пишет `lesson.completed` в outbox одной транзакцией.
`finance-service` читает событие из Kafka и создаёт `charge` с
`amount = lessons.price`, **идемпотентно** по `unique(lesson_id)`. Ответ complete
возвращает `charge_status: "pending"`, потому что начисление создаётся eventual.

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
- `charge` создаётся только из события `lesson.completed`, consumer-ом finance-service,
  `unique(lesson_id)` защищает от повторов/replay.
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

`purpose`: `assignment_attachment | submission_file | payment_receipt |
lesson_material` (`chat_message` — позже).

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
отдельным согласованным шагом с подтверждением координатора (Claude) и человека.

---

## 10. Внутренние зависимости (вызовы только в эту сторону)

Реально подключено в коде (MVP + Этап 5):
```text
gateway     -> identity/lesson/assignment/finance   (gRPC, auth + REST mapping)
gateway     -> file                                  (HTTP multipart upload/download)
lesson      -> identity                              (gRPC check-access)
assignment  -> identity                              (gRPC check-access)
finance     -> identity                              (gRPC check-access)
file        -> identity                              (gRPC check-access на скачивание)
lesson      -> Kafka                                 (`lesson.completed` outbox)
finance     <- Kafka                                 (`lesson.completed` consumer -> charge)
```

Разрешено, но пока НЕ подключено (заводить client при реальной надобности):
```text
assignment  -> file                (если понадобится валидация file_id)
finance     -> file
```
`finance -> lesson` и прямой `lesson -> finance` для charge намеренно отсутствуют:
источник события — lesson, создание начисления — responsibility finance consumer.
Файлы в assignment/lesson/finance хранятся как `file_id` без вызова file.

Gateway — **тонкий**: публичный HTTP API, auth, роутинг, при необходимости
агрегация на чтение. **Бизнес-оркестрации в gateway нет**: gateway не создаёт
charge, не меняет статусы доменных сущностей и не заменяет сервисные проверки.

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

> Примечание (исторически): foundation уже в `main`. docs/ и api-contracts
> в git, поэтому актуальны для любого агента после `git pull`.

---

## 13. Рабочий процесс (одна папка, агенты поочерёдно)

- Одна общая рабочая папка и один репозиторий. Агенты включаются **по одному за
  раз** — человек активирует нужного агента и выдаёт ему задачу на эту сессию.
  Параллельной работы двух агентов в одной папке нет, поэтому `git worktree` и
  раздельные деревья больше не нужны.
- Одна задача = одна ветка `feat/<svc>-<кратко>` (или работа прямо в текущей
  фиче-ветке, если человек так попросил). В один момент времени активна одна
  задача — конфликты владения исключены самим порядком.
- Перед началом задачи: `git pull --rebase origin main` (или мерж main в текущую
  ветку). Мерж в `main` — через PR/подтверждение человека.
- Агент трогает только то, что относится к выданной задаче: свой `services/<svc>/`
  и свою `migrations/<svc>/`. Чужие сервисы — не редактируем без явной задачи.
- Контракты (`docs/api-contracts/`) и публичные сигнатуры `libs/common` меняются
  только через координатора (Claude) и с подтверждением человека (см. §9, AGENTS.md).

---

## 14. Координация и распределение задач

Фиксированной привязки «сервис → агент» больше нет. Работают два агента
(Claude и Codex), которых человек включает **поочерёдно** и которым выдаёт
конкретную задачу на сессию. Любой агент может работать над любым сервисом —
задачу определяет человек, а не роль агента.

```text
Координатор: Claude.
  - владеет источниками правды: docs/PLAN.md и docs/api-contracts/*;
  - ревьюит и утверждает изменения контрактов и публичных сигнатур libs/common;
  - следит за консистентностью и ведёт roadmap (docs/roadmap.md);
  - разрешает кросс-сервисные споры; финальное слово по архитектуре.

Исполнитель (Claude или Codex — кого включил человек):
  - берёт ровно ту задачу, что выдал человек на эту сессию;
  - пишет против контракта; недостающие чужие сервисы мокает client-интерфейсом;
  - не меняет контракты молча — эскалирует координатору (см. §9, AGENTS.md).
```

Текущий статус: identity, file, lesson, assignment, finance, api-gateway, frontend,
gRPC foundation и Kafka/outbox flow реализованы. Полный список оставшихся работ и
их порядок — в `docs/roadmap.md`.

`AGENTS.local.md` (локальный, не коммитится) задаёт, какую роль/задачу выполняет
агент в текущей сессии. Шаблон — `AGENTS.local.example.md`.

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
docs/api-contracts/**        # источник правды по контрактам (в git, меняем через координатора)
migrations/ (общая папка)   # каждый агент — только своя подпапка <svc>/
.env.example
```

---

## 17. Задел под будущее

Правило выбора транспорта:
```text
REST  — внешний API frontend -> api-gateway; файлы gateway -> file-service multipart
gRPC  — команды/запросы, где пользователь или сервис ждёт ответ сейчас
Kafka — доменные факты, которые уже произошли, и побочные эффекты
```

Kafka НЕ используется для gateway-команд и access-check. `check-access`,
create/get/review/confirm/balance остаются синхронными gRPC-вызовами. File upload
на gRPC не переводим без отдельной причины.

Kafka-события (статус — см. roadmap Этап 5):

```text
lesson.completed            ✅ ВНЕДРЕНО (outbox lesson -> Kafka -> finance charge)
lesson.scheduled            ⬜ 5F-3
lesson.cancelled            ⬜ 5F-3
lesson.rescheduled          ⬜ 5F-3
assignment.created          ⬜ 5F-1
submission.uploaded         ⬜ 5F-1
assignment.reviewed         ⬜ 5F-1
assignment.needs_fix        ⬜ later
assignment.done             ⬜ later
payment_receipt.uploaded    ⬜ 5F-2
payment.confirmed           ⬜ 5F-2
payment.rejected            ⬜ 5F-2
charge.created              ⬜ 5F-2
balance.changed             ⬜ 5F-2
message.sent                ⬜ 5J
message.read                ⬜ 5J
user.registered             ⬜ later
student.created             ⬜ later
teacher_student_link.created ⬜ later
file.uploaded               ⬜ optional later
```

Целевой порядок развития:
```text
5F-0 event foundation hardening: naming, contracts, inbox/processed_events, retry/DLQ
5F-1 assignment.* events
5F-2 finance.* events
5F-3 дополнительные lesson.* events
5G notification-service: Kafka consumers -> notifications_db -> gRPC list/mark-read
5H report-service: Kafka read-models -> dashboard summaries по gRPC
5I MinIO/S3 для file-service без изменения внешнего API
5J chat-service: messages, read status, attachments, message.* events
5K production hardening: reverse proxy, CI/CD, readiness, logs/metrics, Kafka lag/DLQ
```

Не делаем сейчас: Redis/WebSocket до chat-service, email/Telegram/push до
notification MVP, реальные платежи, YDB/перенос всех БД, родительский аккаунт,
повторяющиеся занятия. Комментарии к ДЗ в MVP — внутри assignment-service
(это не chat-service).
