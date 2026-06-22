# TutorFlow — Roadmap (общий, для любого агента)

> Очередь оставшихся задач. Привязки «сервис → агент» нет: исполнителя на каждую
> задачу выбирает человек, включая нужного агента (Claude или Codex) поочерёдно.
> Координатор — Claude (контракты, PLAN, ревью, интеграция). Источники правды:
> `docs/PLAN.md`, `docs/api-contracts/*.openapi.yaml`, общие правила — `AGENTS.md`.
>
> Как пользоваться: человек выдаёт активному агенту **одну** задачу из списка.
> Агент делает ровно её, не выходя за рамки, и обновляет соответствующую заметку
> `docs/agent-<…>.md`. Контракты молча не менять — эскалировать координатору.

Обновлено: 2026-06-22.

---

## Статус

| Сервис | Состояние |
|---|---|
| `libs/common` | каркас готов (errors, auth_context, http_client, jwt) |
| identity-service | реализован, в `main` |
| file-service | реализован, в `main` |
| lesson-service | internal endpoints в `main` |
| assignment-service | internal endpoints в `main` |
| finance-service | internal endpoints в `main` |
| **api-gateway** | **только скелет (`/health`), реальной логики нет — следующая задача** |

---

## Задача 1 (следующая) — api-gateway

**Цель:** единственная внешняя точка входа. Валидирует JWT, срезает и заново ставит
`X-User-*`, маршрутизирует во внутренние сервисы. **Без бизнес-логики.**

Контракт — источник правды: `docs/api-contracts/gateway.openapi.yaml`.
Порт `8080`, наружу публикуется только он (см. PLAN §4). Скелет уже есть в
`services/api-gateway/` (`main.cpp`, `configs/static_config.yaml`, `Dockerfile`,
`CMakeLists.txt`).

### Что сделать
1. **Auth-проброс (без JWT):**
   - `POST /auth/register` → identity `POST /internal/auth/register`.
   - `POST /auth/login` → identity `POST /internal/auth/login`.
   - Эти ручки `security: []` — токен не требуется, тело прокидывается как есть.
2. **Локальная валидация JWT** на всех остальных ручках через
   `libs/common/.../jwt.hpp` `Verify` (секрет `JWT_SECRET`). Нет/битый/просроченный
   токен → `401` в envelope-формате.
3. **Заголовки `X-User-*`:** перед проксированием **удалить любые входящие**
   `X-User-*` от клиента и выставить заново из проверенного JWT:
   `X-User-Id: <sub>`, `X-User-Roles: <roles CSV>` (в MVP — одна роль).
4. **Маршрутизация** (gateway path → internal service, base-url из конфига):
   - `/me` → identity (`GET /internal/users/{X-User-Id}`);
   - `/students*` → identity (create/list/get связи teacher-student);
   - `/students/{id}/balance`, `/students/{id}/transactions` → finance;
   - `/availability`, `/lessons*` → lesson;
   - `/assignments*` → assignment;
   - `/payments/receipts*` → finance;
   - `/files*` → file (multipart прокидывать как есть, не буферизуя сверх нужного).
5. **Единый envelope ошибок** (PLAN §6): ошибки самого gateway (401/недоступность
   внутреннего сервиса → 502/503) — в том же формате; ответы внутренних сервисов
   (включая их ошибки) прокидывать как есть.
6. **Никакой оркестрации.** Загрузка чека = клиент сам грузит в `/files`, затем шлёт
   `file_id` в `/payments/receipts`. charge инициирует lesson-service, не gateway.

### Definition of Done
- `docker compose up --build` поднимает gateway + зависимые сервисы.
- `GET http://localhost:8080/health → {"status":"ok"}`.
- register → login → получить токен → дёрнуть `/me` с `Authorization: Bearer` —
  возвращает пользователя; без токена — `401`.
- Клиентские `X-User-*` игнорируются (подделать роль через заголовок нельзя).
- Порты внутренних сервисов наружу не опубликованы (только 8080).
- Заметка реализации: `docs/agent-api-gateway.md` (эндпоинты, имена компонентов,
  ограничения), по образцу `docs/agent-a-identity-service.md`.

---

## Задача 2 — подключить `hourly_rate` в lesson-service (контракт уже обновлён)

identity отдаёт `check-access → {allowed, status, hourly_rate}` (контракт уже в `main`).
В lesson-service: добавить `std::optional<double> hourly_rate` в `AccessCheckResult`,
парсить из ответа; при `CreateLessonRequest.price == null` снимать `lessons.price`
из `hourly_rate`; убрать временный fallback `422 business_rule` (оставить `422`
только когда и `price`, и `hourly_rate` пусты). Убрать раздел «Known Contract Gap»
из `docs/agent-b-lesson-service.md`.

## Задача 3 — общие handler-хелперы в `libs/common` (по желанию, аккуратно)

Вынести в `libs/common/.../handler_helpers.hpp` повторяющиеся хелперы
(`HandleEnvelope`, `ParseJsonBody`, `RequireString`, `OptionalString/Double`,
`JsonResponse`, `ErrorResponse`) и заменить локальные копии в handler-ах сервисов.
Это изменение публичной сигнатуры `libs/common` → согласовать с координатором,
делать одним проходом, затем ребейз веток остальных задач.

## Задача 4 — сквозной smoke happy-path (после api-gateway)

Прогнать через живой gateway полный сценарий PLAN §2:
register/login → создать ученика → слот → занятие → complete (charge один раз,
идемпотентно) → баланс → загрузка чека (баланс НЕ меняется) → подтверждение
(payment, баланс меняется) → ДЗ → submit → review. Зафиксировать команды в
`scripts/` (smoke-скрипт) и/или в заметке. Полноценный userver testsuite — позже.

---

## Правила назначения задач
- В один момент активен **один** агент с **одной** задачей (см. PLAN §13).
- Человек называет агенту задачу из этого списка (по номеру/названию).
- Зависимости: Задача 2 требует identity в `main` (уже выполнено). Задача 4
  требует готового api-gateway (Задача 1). Задача 3 — независима, но трогает общий
  `libs/common`, поэтому только по согласованию с координатором.
- Менять контракты — только через координатора (Claude) с подтверждением человека.
