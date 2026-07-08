# Стажёрский backlog — backend (постановки координатора)

Дата постановки: 2026-07-08. Исполнитель: стажёр (backend).
Порядок выполнения: 1 → 2 → 3 → 4 → 5. Одна задача = одна ветка `feat/<svc>-<кратко>`.

Общие правила (кратко из AGENTS.md):

- Перед стартом: `git pull --rebase origin main`.
- Изменение контрактов (`libs/proto`, `docs/api-contracts`, public-сигнатуры `libs/common`) —
  СНАЧАЛА предложение координатору, потом код.
- DoD проекта для каждой задачи: сервис собирается; миграции накатываются на чистую БД;
  `/health` работает; happy-path через `curl` через gateway; ошибки в едином envelope;
  внутренние порты наружу не публикуются.
- Чужую БД не читать; teacher↔student проверять только через identity gRPC.

---

## Задача 1. finance-service: причина отклонения чека (roadmap 6.4)

**Ветка:** `feat/finance-reject-reason`
**Суть:** фронт при `rejected` показывает `receipt.comment`, но неясно, заполняется ли он.
Исследовать и починить внутри finance; контракт скорее всего менять не нужно.

Шаги:
1. Найти reject-путь: `services/finance-service/src/domain/` + `repositories/`
   (метод отклонения чека) и gRPC-обвязку.
2. Проверить: принимает ли reject `comment`/причину; пишется ли она в
   `finance_db.payment_receipts`; отдаётся ли в списке чеков через gateway
   (`GET /payments/receipts`).
3. Если поле теряется по пути — дописать (SQL/маппинг). Если его нет и в proto —
   СТОП, принести координатору предложение по контракту.
4. Проверить руками: reject с комментарием → `GET /payments/receipts` от ученика
   содержит причину.

**DoD:** ученик видит реальную причину отклонения; событие `payment.rejected`
не сломано (outbox пишется как раньше).

---

## Задача 2. tests: pytest на переотправку ДЗ

**Ветка:** `feat/tests-assignment-resubmit`
**Суть:** backend уже разрешает повторный submit (блокирует только `done`/`expired`),
но сценарий не покрыт тестом.

Сценарий (через gateway, образец — `tests/test_finance.py` / `test_chat.py`):
1. teacher создаёт ученика и assignment;
2. student submit №1 → статус assignment `submitted`;
3. teacher review `needs_fix` → статус `needs_fix`;
4. student submit №2 → снова `submitted`; в detail две submissions,
   review применяется к последней;
5. teacher review `accepted` → статус `done`;
6. student submit №3 → ожидаем 409, envelope `{"error":{"code":"conflict",...}}`.

**DoD:** `python3 -m pytest tests` зелёный локально; тест не зависит от порядка
запуска других тестов (свои пользователи/сущности).

---

## Задача 3. notification-service: bulk mark-read (roadmap 6.5)

**Ветка:** `feat/notification-read-all`
**Меняет контракт → сначала согласование.**

1. Принести координатору предложение: RPC `MarkAllRead` в
   `libs/proto/tutorflow/notification.proto` (вход — UserContext, выход —
   количество помеченных или Empty).
2. После апрува: реализация в notification-service — один
   `UPDATE notifications SET is_read = true WHERE user_id = $1 AND is_read = false`.
3. gateway: route `POST /notifications/read-all` (маппинг X-User-Id → UserContext).
4. Зеркало в `docs/api-contracts/gateway.openapi.yaml`.
5. Фронт-кнопку переключит агент отдельной задачей — фронт не трогать.

**DoD:** один запрос помечает все уведомления пользователя; повторный вызов
идемпотентен (0 строк, не ошибка); чужие уведомления не задеваются.

---

## Задача 4. api-gateway: имя собеседника в чате (roadmap 7.2)

**Ветка:** `feat/gateway-chat-peer-name`
**Меняет только ответ gateway (+OpenAPI); identity proto НЕ трогаем** —
`IdentityService.GetUser(user_id) → User{display_name}` уже существует.

1. В gateway-handler `GET /chats` (см. `proxy_handlers.cpp`, ~строка 809,
   `Chat().ListDialogs`) после получения диалогов собрать МНОЖЕСТВО уникальных
   peer_id (для teacher это student_id, для student — teacher_id).
2. Один вызов `identity.GetUser` на каждый уникальный id (не на каждый диалог);
   маппинг id → display_name.
3. Добавить поле `peer_name` в JSON каждого диалога.
4. Ошибка identity по конкретному id (недоступен/не найден) → диалог отдаём БЕЗ
   `peer_name`, не 500. Логировать warning.
5. Обновить схему `ChatDialog` в `docs/api-contracts/gateway.openapi.yaml`
   (`peer_name`, optional).

**DoD:** student видит имя преподавателя в `GET /chats` даже при пустом
report read-model; при недоступном identity список чатов продолжает открываться.

---

## Задача 5. identity-service: флаг must_change_password (roadmap 6.3)

**Ветка:** `feat/identity-must-change-password`
**Меняет контракт → сначала согласование.**

1. Предложение координатору: булево поле в `users` (или в student-профиле —
   аргументировать выбор), выставляется в `CreateStudent` (временный пароль),
   снимается в `ChangePassword`; наружу — поле в `TokenResponse` логина и/или `/me`.
2. После апрува: миграция в `migrations/identity/` (накат на чистую БД:
   `./scripts/migrate.sh identity`); default `false`, существующие строки не ломать.
3. domain: выставление/сброс флага; proto + gateway + OpenAPI.
4. Проверка: создать ученика → login → флаг `true`; сменить пароль → login →
   флаг `false`.

**DoD:** флаг корректно живёт по всему циклу temp-password; старые пользователи
логинятся как раньше (обратная совместимость ответа).

---

## На вырост (после 1–5, брать по согласованию)

- **6.2 Персистентные «прочитано» в чате** — read-маркер уже есть в `chat_db`;
  выбрать вариант отдачи (поле в Message / `GetReadMarker` / доставка автору),
  написать мини-ADR в `docs/adr/`, потом реализация.
- **Readiness check для одного сервиса** — эталонная реализация (Postgres/Kafka/
  Redis/MinIO зависимости), которую агент растиражирует на остальные девять.
