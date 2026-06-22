# AGENTS.md — общие правила для агентов TutorFlow

> Общий файл, **коммитится** в git. Источник правды по архитектуре/домену —
> `docs/PLAN.md`, по эндпоинтам — `docs/api-contracts/*.openapi.yaml`.
> Личное назначение и заметки агента — в `AGENTS.local.md` (НЕ коммитится).

## Стек
C++20 + userver, PostgreSQL (одна БД на сервис), Docker Compose. REST между сервисами.
Kafka/Redis/чат/уведомления/реальные платежи — НЕ в MVP (только задел в коде).

## Роли и ownership
- **Lead (главный): Agent A.** Владеет контрактами и `docs/PLAN.md`, собирает
  foundation, ревьюит и утверждает изменения контрактов, делает интеграционные
  мержи в `main`, разрешает спорные кросс-сервисные вопросы. При конфликте
  решений — слово за Lead.
- **Agent A:** identity-service, file-service, api-gateway.
- **Agent B:** lesson-service, assignment-service, finance-service.

Каждый агент ДОЛЖЕН получить явное назначение перед стартом («Ты Agent A» /
«Ты Agent B») через свой `AGENTS.local.md`. Не предполагай свою роль по умолчанию.

## Порядок работы
1. Не начинать доменную логику, пока не поднимается foundation:
   `docker compose up --build` и `curl /health` через gateway отвечают.
2. Сначала миграции + минимальные internal endpoints (CRUD) своего сервиса.
3. Потом public endpoints через gateway.
4. Потом smoke-path из `PLAN.md` §2.

Не реализуй зависящий шаг, пока не готов или не замокан его источник (например,
не пиши lesson `complete` без понятного finance internal charge — сначала замокай).

## Git-процесс
- Каждому агенту своя папка через `git worktree`, своя ветка `feat/<svc>-<кратко>`.
- Перед PR: `git pull --rebase origin main`. Мерж в `main` — через Lead.
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
4. Дождаться подтверждения Lead/человека.

Только после этого — правка контракта и кода, затем синхронизация (commit + ребейз
второго агента).

## Stub/mock чужих сервисов
Нужен чужой сервис — НЕ лезть в его код. Завести client-interface + временную
stub/mock-реализацию у себя (по контракту). После готовности чужого сервиса —
переключить на реальный HTTP client. Так Agent B не ждёт identity/file.

## docker-compose.yml
Общий файл. После foundation-коммита менять только при необходимости.
Изменение, касающееся чужого сервиса, — сначала согласовать с Lead.
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
