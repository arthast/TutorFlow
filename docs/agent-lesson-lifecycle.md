# Этап 5L — Lesson lifecycle + finance corrections (design + контракты)

> Координаторский спек под выдачу агенту. Источник правды по эндпоинтам остаётся
> `docs/api-contracts/*.openapi.yaml` и `libs/proto/*`; этот файл фиксирует, ЧТО и
> КАК меняем, чтобы исполнитель писал против контракта, а не против чужой реализации.
> Изменение контрактов согласовано с человеком (AGENTS §9). Дата: 2026-06-24.

## 1. Цель и охват

Расширить жизненный цикл занятия и связать его с финансами:

1. **Reschedule** — перенос времени занятия (`scheduled`, новое время/слот).
2. **Reactivate** — восстановление отменённого занятия (`cancelled → scheduled`).
3. **Cancel completed** — отмена уже завершённого занятия (`completed → cancelled`)
   с автоматической финансовой компенсацией.
4. **Manual correction** — ручная корректировка баланса ученика преподавателем
   (операция, не привязанная к занятию).

Ключевой инвариант (AGENTS §9): **finance append-only.** Charge никогда не
удаляется и не редактируется. Любой откат — это ДОБАВЛЕНИЕ компенсирующей
операции `correction` (тип уже есть в модели finance, PLAN §8.4).

## 2. State-machine занятия (после 5L)

```text
            create
              │
              ▼
        ┌───────────┐  reschedule (новое время, status не меняется)
        │ scheduled │◄──────────────┐
        └───────────┘               │
          │   │   │                 │
 complete │   │   │ cancel          │ reactivate
          ▼   │   ▼                 │
   ┌───────────┐ ┌───────────┐      │
   │ completed │ │ cancelled │──────┘
   └───────────┘ └───────────┘
          │            ▲
          │ cancel     │
          └────────────┘  (completed → cancelled, + компенсация в finance)
```

Разрешённые переходы:
- `scheduled → completed` (есть)
- `scheduled → cancelled` (есть)
- `scheduled → scheduled` — reschedule, меняется только время/слот (**новое**)
- `cancelled → scheduled` — reactivate (**новое**)
- `completed → cancelled` — отмена завершённого + компенсация (**новое**)

Запрещено: `completed → scheduled`, `completed → completed`, любой переход из
`cancelled`/`completed` кроме перечисленных. Идемпотентность: повтор операции,
приводящей в то же состояние, → `200` с текущим объектом, без побочных эффектов.

## 3. Финансовая модель компенсации

- `complete` → charge (как сейчас, `unique(lesson_id)`, через `lesson.completed`).
- `completed → cancelled` → lesson эмитит `lesson.cancelled` c `previous_status=completed`
  и `price`. finance-consumer добавляет `correction` на `-price`, **идемпотентно
  по `lesson_id`** (одна компенсация на занятие).
- Баланс = `sum(charge) − sum(payment) + sum(correction) − sum(refund)`. После
  компенсации вклад занятия в баланс = `+price − price = 0`.
- **Принятое product-поведение:** если ученик уже оплатил это занятие, после
  компенсации баланс уходит в минус — у ученика образуется кредит/предоплата.
  Это допустимо и считается само.
- Ручная `correction` (manual): преподаватель задаёт `amount` (±) + `comment`;
  finance добавляет строку `correction`. Назначение — «разные ситуации» вне
  привязки к занятию.

## 4. Решённые edge-cases (дефолты согласованы)

1. **`unique(lesson_id)` на charge vs повторное начисление.** После компенсации
   через `correction` сам charge остаётся в журнале. Поэтому `reactivate → complete`
   того же занятия **второй charge не создаст** (упрётся в `unique(lesson_id)`).
   **Дефолт:** reactivate всегда возвращает в `scheduled` без авто-charge;
   повторное начисление при необходимости делается **ручной `correction`**.
   Идемпотентность charge НЕ усложняем.
2. **Кредит при отмене оплаченного завершённого занятия** — допустим (см. §3).
3. **Reschedule/reactivate занятия с `starts_at` в прошлом** — **разрешаем**
   (бывает задним числом), отдельной проверки времени не добавляем.

## 5. Изменения контрактов

> Менять `libs/proto/*` и `docs/api-contracts/*` — согласованный шаг. Ниже —
> целевые сигнатуры. Внешний REST расширяется только добавлением новых ручек.

### 5.1 lesson — `libs/proto/tutorflow/lesson.proto`

Добавить RPC:
```proto
rpc RescheduleLesson(RescheduleLessonRequest) returns (Lesson);
rpc ReactivateLesson(ReactivateLessonRequest) returns (Lesson);
// CancelLesson — существует; расширяется поведение (completed → cancelled)

message RescheduleLessonRequest {
  tutorflow.common.v1.UserContext user = 1;
  string lesson_id = 2;
  string new_starts_at = 3;   // RFC3339
  string new_ends_at = 4;     // RFC3339
  string new_slot_id = 5;     // optional; пусто = без слота
}

message ReactivateLessonRequest {
  tutorflow.common.v1.UserContext user = 1;
  string lesson_id = 2;
}
```

### 5.2 finance — `libs/proto/tutorflow/finance.proto`

Добавить RPC ручной коррекции:
```proto
rpc CreateCorrection(CreateCorrectionRequest) returns (Transaction);

message CreateCorrectionRequest {
  tutorflow.common.v1.UserContext user = 1; // должен быть teacher
  string student_id = 2;
  double amount = 3;       // ± (знак задаёт направление коррекции)
  string currency = 4;     // дефолт RUB
  string comment = 5;
}
```

### 5.3 gateway — `docs/api-contracts/gateway.openapi.yaml`

Новые публичные ручки (через gateway, тонко проксируют в сервисы):
```text
POST /lessons/{lessonId}/reschedule   body { new_starts_at, new_ends_at, new_slot_id? }  (teacher)
POST /lessons/{lessonId}/reactivate                                                       (teacher)
POST /lessons/{lessonId}/cancel        (есть или добавить) — теперь допускает completed   (teacher)
POST /students/{studentId}/corrections body { amount, currency?, comment }                (teacher)
```
Gateway остаётся тонким: auth + routing + mapping, без бизнес-логики (AGENTS §2).

### 5.4 finance — `docs/api-contracts/finance.openapi.yaml`

Internal `POST /internal/students/{id}/corrections` (вызов от gateway), плюс
описание компенсации из события (consumer, не REST).

### 5.5 Коды ошибок

`409 conflict` — слот занят при reschedule/reactivate; недопустимый переход
статуса. `403` — не свой ученик/занятие. `404` — занятие/ученик не найдены.
`422 business_rule` — некорректная сумма коррекции (если решим валидировать ноль).
Всё — в едином envelope (PLAN §6).

## 6. События

Новые контракты (добавлены): `docs/event-contracts/lesson.rescheduled.v1.json`,
`docs/event-contracts/lesson.cancelled.v1.json`.

- `lesson.rescheduled` — producer lesson; consumers notification, report.
- `lesson.cancelled` — producer lesson; consumers **finance** (компенсация при
  `previous_status=completed`), notification, report.
- Reactivate переиспользует существующий `lesson.scheduled` (5F-3), без нового типа.
- Ручная `correction` и компенсация отражаются существующим `balance.changed`
  (новый тип события для коррекций НЕ заводим — не плодим события).

Все события — через transactional outbox соответствующего сервиса (как 5E/5F).
finance-consumer на `lesson.cancelled` идемпотентен по `lesson_id` (inbox
`processed_events` + уникальность компенсации на занятие).

## 7. Разбивка задач (порядок и зависимости)

```text
5L.0  Контракты (координатор): proto lesson/finance + gateway/finance OpenAPI +
      event-contracts (готово: 2 JSON). Согласовать → закоммитить → ребейз ветки.
5L.1  lesson: RescheduleLesson — domain + repo (освободить старый слот, забронировать
      новый, 409 если занят) + outbox lesson.rescheduled.            [после 5L.0]
5L.2  lesson: ReactivateLesson (cancelled→scheduled) — repo (ребронь слота, 409),
      идемпотентность, повторно эмитит lesson.scheduled.             [после 5L.0]
5L.3  lesson: расширить CancelLesson (снять запрет completed→cancelled), освободить
      слот, эмитить lesson.cancelled с previous_status + price/currency. [после 5L.0]
5L.4  finance: consumer lesson.cancelled → компенсирующая correction (idempotent
      по lesson_id), reuse outbox→balance.changed.                   [после 5L.3]
5L.5  finance: CreateCorrection (manual) — gRPC + repo (append correction),
      check-access teacher↔student, outbox balance.changed.          [после 5L.0]
5L.6  gateway: роуты reschedule/reactivate/cancel/corrections (тонкое проксирование). [после 5L.1-5L.5]
5L.7  notification-service: подписать lesson.rescheduled, lesson.cancelled,
      balance.changed(correction) → уведомления ученику/преподавателю. [после 5L.6]
5L.8  frontend: кнопки teacher — перенести/восстановить/отменить занятие,
      «скорректировать баланс» (сумма+коммент); показ статусов.        [после 5L.6]
5L.9  tests + smoke: новые переходы, компенсация, идемпотентность, доступ.  [последним]
```

Дробить на сессии: одна задача = одна ветка (AGENTS §git). Минимально жизнеспособный
срез без фронта — 5L.0→5L.6 (+5L.9).

## 8. Definition of Done

1. Все затронутые сервисы собираются (`COMPOSE_PARALLEL_LIMIT=1 docker compose build`).
2. Миграции (если нужны finance для уникальности компенсации) применяются на чистую БД.
3. Переходы статусов работают через gateway (`curl`): reschedule, reactivate,
   cancel-completed, manual correction.
4. **Компенсация:** отмена завершённого занятия возвращает его вклад в баланс к 0;
   повторная отмена/replay второй correction НЕ создаёт (идемпотентность по lesson_id).
5. **Append-only:** charge не удаляется; в журнале видны charge + correction.
6. Ошибки — в едином envelope; 409 на занятый слот/недопустимый переход.
7. notification-service шлёт уведомления на новые события.
8. `python3 scripts/smoke_mvp.py` → SMOKE OK; `pytest tests` зелёный (+ новые кейсы).
9. Порты наружу не публикуются кроме gateway.

## 9. Что НЕ делаем в 5L

- Не усложняем идемпотентность charge ради повторного начисления (см. §4.1 —
  через ручную correction).
- Не заводим отдельный тип события под коррекции (reuse `balance.changed`).
- Не делаем reschedule на уровне finance (денег не касается).
- Не вводим refund-поток к ученику (отдельное решение при реальной надобности).
