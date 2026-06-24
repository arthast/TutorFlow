# AGENTS.md — общие правила для агентов TutorFlow

> Общий файл, **коммитится** в git. Источник правды по архитектуре/домену —
> `docs/PLAN.md`, по эндпоинтам — `docs/api-contracts/*.openapi.yaml`.
> Личное назначение и заметки агента — в `AGENTS.local.md` (НЕ коммитится).

## Стек
C++20 + userver, PostgreSQL (одна БД на сервис), Docker Compose.
Коммуникации (внедрено, Этап 5 roadmap): **REST** — только снаружи (frontend →
api-gateway) и файлы (gateway → file-service, multipart). **gRPC** — синхронные
внутренние вызовы (gateway → сервисы; lesson/assignment/finance/file → identity
check-access). **Kafka** — асинхронные события; внедрён первый flow
`lesson.completed` (outbox в lesson → Kafka → finance создаёт charge идемпотентно).
Ещё НЕ в MVP: остальные доменные события (5F), сервисы-консьюмеры
notification/report/chat (5G), Redis, реальные платежи.

## Роли и распределение задач
- **Координатор — Claude.** Владеет контрактами и `docs/PLAN.md`, ревьюит и
  утверждает изменения контрактов и публичных сигнатур `libs/common`, ведёт
  `docs/roadmap.md`, делает интеграционные мержи в `main`, разрешает спорные
  кросс-сервисные вопросы. При конфликте решений — слово за координатором.
- **Фиксированной привязки «сервис → агент нет».** Работают два агента
  (Claude и Codex), которых человек включает **поочерёдно** (по одному за раз) и
  которым выдаёт конкретную задачу на сессию. Любой агент может работать над любым
  сервисом — выбор определяет человек, а не роль агента.

Перед стартом агент берёт задачу из явного указания человека (и/или `AGENTS.local.md`
+ `docs/roadmap.md`). Не предполагай задачу по умолчанию и не выходи за её рамки.

## Порядок работы
1. Не начинать доменную логику, пока не поднимается foundation:
   `docker compose up --build` и `curl /health` через gateway отвечают.
2. Сначала миграции + минимальные internal endpoints (CRUD) своего сервиса.
3. Потом public endpoints через gateway.
4. Потом smoke-path из `PLAN.md` §2.

Не реализуй зависящий шаг, пока не готов или не замокан его источник (например,
не пиши lesson `complete` без понятного finance internal charge — сначала замокай).

## Git-процесс
- Одна общая папка, агенты включаются поочерёдно (worktree не нужен). Одна задача =
  одна ветка `feat/<svc>-<кратко>` (или текущая фиче-ветка, если так попросил человек).
- Перед началом задачи: `git pull --rebase origin main`. Мерж в `main` — через PR /
  подтверждение человека (координатор — Claude).
- **Коммитятся:** исходники, `docs/PLAN.md`, `docs/api-contracts/*`, `AGENTS.md`,
  `README.md`, `.env.example`.
- **НЕ коммитятся:** `.env`, `AGENTS.local.md`, `*.local.md`, локальные заметки,
  `build/`, `storage/`.

## Ключевые правила
1. Внешние запросы — только через api-gateway; внутренние сервисы наружу не торчат.
2. **gateway без бизнес-логики.** Можно: auth, routing, request/response mapping,
   простая агрегация на чтение. Нельзя: создавать charge, менять статусы
   lesson/assignment/finance, реализовывать доменные проверки вместо сервисов.
3. **Границы БД.** Сервис работает только со своей БД. Запрещено: подключаться к
   чужому `DATABASE_URL` (даже read-only), JOIN между БД разных сервисов. Чужие
   данные — только через internal HTTP API.
4. Связь teacher↔student проверяется ТОЛЬКО через identity
   `POST /internal/relations/check-access`.
5. Файлы — только через file-service; в других сервисах хранится `file_id`.
6. Auth: identity подписывает JWT; gateway валидирует локально; gateway срезает
   входящие `X-User-*` и заново ставит `X-User-Id` / `X-User-Roles`. В MVP — одна роль.
7. Charge создаёт **lesson-service** при `complete`, НЕ gateway.
8. Баланс меняется только после подтверждения чека teacher (не при загрузке).
9. Finance — append-only журнал (charge/payment/correction/refund); операции не
   редактируем, добавляем корректирующие.
10. **Идемпотентность** — повторный запрос не ломает состояние:
    - повторный `complete` не создаёт второй charge (`unique(lesson_id)`);
    - повторное подтверждение receipt не создаёт второй payment;
    - повторная загрузка файла не перезаписывает существующий.
11. Ошибки — единый envelope; коды — константы из `libs/common`.
12. `libs/common` — только инфраструктура, без DTO/домена и без pg-обёртки.
13. Не over-engineering, не рефакторить чужое, не плодить зависимости, не делать
    преждевременных универсальных абстракций.

## Изменение контрактов (жёсткое правило)
Если реализация требует изменить контракт (`api-contracts` или public-сигнатуру
`libs/common`):
1. НЕ менять код/контракт молча.
2. Описать проблему.
3. Предложить изменение контракта.
4. Дождаться подтверждения координатора (Claude) / человека.

Только после этого — правка контракта и кода, затем синхронизация (commit; при
необходимости — ребейз ветки следующей задачи на обновлённый `main`).

## Stub/mock чужих сервисов
Нужен чужой сервис, которого ещё нет, — НЕ лезть в его код. Завести client-interface
+ временную stub/mock-реализацию у себя (по контракту). После готовности чужого
сервиса — переключить на реальный HTTP client. Так задача не блокируется чужим кодом.

## docker-compose.yml
Общий файл. После foundation-коммита менять только при необходимости.
Изменение, касающееся чужого сервиса, — сначала согласовать с координатором (Claude).
Порты и имена сервисов не менять без правки `PLAN.md` §4.

## Code style
- Тонкие handlers: handler только парсит request и зовёт domain service.
- SQL держать рядом с repository / в понятном месте сервиса.
- Без глобального состояния.
- Зависимости — только по необходимости.
- Ошибки — через common envelope.

## Definition of Done (проверить перед завершением задачи)
1. Сервис собирается (`cmake --build` / `docker compose build`).
2. Миграции применяются на чистую БД.
3. `/health` работает.
4. Happy-path эндпоинта работает через `curl`.
5. Ошибки возвращаются в envelope-формате.
6. Порт наружу не публикуется (кроме gateway).

## Команды (уточнятся после foundation)
```bash
cp .env.example .env
docker compose up --build
./scripts/migrate.sh all          # или ./scripts/migrate.sh identity
curl http://localhost:8080/health # gateway снаружи
```

## Imported Claude Cowork project instructions

You are working on the TutorFlow project.

TutorFlow is a backend project for a platform where private teachers interact with students. The MVP supports teachers, students, lessons, homework assignments, submissions, comments, file uploads, payment receipts, and manual payment confirmation by a teacher.

Use a microservices-lite architecture in one monorepo.

MVP services:

* api-gateway
* identity-service
* lesson-service
* assignment-service
* finance-service
* file-service

Do not implement yet unless explicitly asked:

* Kafka
* Redis
* chat-service
* notification-service
* report-service
* frontend
* Telegram bot
* Google Calendar
* real payment integrations

Important architecture rules:

* All external requests must go through api-gateway.
* Internal services must not be exposed directly to clients.
* Each service owns its own database/schema.
* A service must not read another service's database directly.
* Files must go through file-service.
* finance-service does not process real payments.
* Payment flow is manual: student uploads a receipt file, teacher confirms it, then the balance changes.
* Balance must not change when a receipt is only uploaded. It changes only after teacher confirmation.
* Keep the implementation simple and focused on the MVP.
* Do not over-engineer.
* Do not refactor unrelated files.
* Do not add new dependencies without a clear reason.

Before making changes:

* inspect the current repository structure;
* read AGENTS.md if it exists;
* read docs/ if they exist;
* explain briefly what you plan to change.

After making changes:

* list changed files;
* explain what was implemented;
* explain what was intentionally not implemented;
* provide commands to build, run, and test the changed part.

When implementing tasks:

* make small scoped changes;
* follow existing project style;
* update documentation when API contracts, database schema, or architecture decisions change;
* prefer working code over theoretical completeness.
