# Этап 5H — report-service / read-models (design + контракты)

> Координаторский спек под выдачу агенту. Согласовано с человеком (2026-06-26).
> Источник правды по эндпоинтам — `docs/api-contracts/*` и `libs/proto/*`; event-
> контракты — `docs/event-contracts/*`. Здесь — ЧТО и КАК строим.

## 1. Цель и принципы

Сейчас dashboard собирался бы синхронным fan-out gateway → 4 сервиса. report-service
строит **read-models из событий** и отдаёт готовый dashboard по gRPC за один вызов.

Жёсткие принципы:
- **report-service — НЕ source of truth.** Истина остаётся в lesson/assignment/finance/
  identity. report хранит только read-model и **пересобираем** (reset consumer group +
  truncate → перечитать из Kafka).
- **Никакой финансовой математики в report.** Он НЕ складывает charge/payment/correction/
  refund. finance считает баланс и публикует готовый абсолютный `balance_amount` в
  `balance.changed`; report хранит это значение (last-write по `student_id`).
- **Eventual consistency.** После `lesson.complete` charge создаётся асинхронно, поэтому
  dashboard короткое время может показывать прежний баланс. Это нормально; в ответе есть
  `updated_at`. Фронт после complete делает polling/refresh.
- Чистый внутренний сервис: только gRPC-сервер + Kafka-consumer, без REST. Своя БД `report_db`.

## 2. Денежная модель в dashboard (решено)

report хранит абсолютный `balance_amount` из `balance.changed` и отдаёт наружу
разложенное, чтобы фронт не гадал про знак:
```text
balance_amount  — сырое signed-значение (как прислал finance)
debt_amount     — max(balance_amount, 0)   (сколько должен ученик)
overpaid_amount — max(-balance_amount, 0)  (переплата)
```
**Чеки на проверке считаем ОТДЕЛЬНО и НЕ уменьшаем долг** до `payment.confirmed`:
`pending_receipts_count` / `pending_receipts_amount`. Пример: `debt_amount=3000` и
`pending_receipts_amount=3000` = «должен 3000, но уже загрузил чек на 3000, ждёт проверки».

**Teacher-агрегаты считаем по `debt_amount`, а не по `balance_amount`:**
`total_debt_amount = Σ debt_amount по ученикам` (если один должен 5000, а другой переплатил
5000 — общий долг 5000, не 0). Также `total_overpaid_amount`, `students_with_debt_count`.

## 3. Контракты (пререквизит — уже применён координатором)

`docs/event-contracts/balance.changed.v1.json` расширен: добавлен **обязательный**
`balance_amount` (абсолютный баланс ПОСЛЕ операции; `delta` оставлен для аудита).
**Требуется доработка finance-service (5H-0):** в каждом месте, где finance пишет
`balance.changed` в outbox (charge/payment/correction/refund), дополнительно посчитать
текущий баланс ученика (`SUM(...)` той же транзакцией) и положить в `balance_amount`.
Это отдельный маленький согласованный шаг ПЕРЕД запуском consumer-а report.

Деньги хранить в том же представлении, что шлёт finance (сейчас `numeric`/RUB) — НЕ
переводить молча в integer-копейки, иначе рассинхрон округления с finance.

## 4. read-model таблицы (`report_db`)

```sql
-- активность ученика (из lesson.* / assignment.*)
student_activity_summary(
  teacher_id, student_id,
  upcoming_lessons_count, completed_lessons_count, cancelled_lessons_count,
  active_assignments_count, submitted_assignments_count, reviewed_assignments_count,
  last_lesson_at, next_lesson_at, updated_at,
  PRIMARY KEY (teacher_id, student_id))

-- финансы ученика (из balance.changed + payment_receipt.* / payment.*)
student_finance_summary(
  teacher_id, student_id,
  balance_amount, debt_amount, overpaid_amount, currency,
  pending_receipts_count, pending_receipts_amount, last_payment_at,
  last_balance_event_at, updated_at,
  PRIMARY KEY (teacher_id, student_id))

-- агрегаты преподавателя (пересчёт при изменении строк ученика)
teacher_summary(
  teacher_id PRIMARY KEY,
  students_count, upcoming_lessons_count, pending_submissions_count,
  pending_receipts_count, pending_receipts_amount,
  total_debt_amount, total_overpaid_amount, students_with_debt_count, updated_at)

-- inbox идемпотентности (ОБЯЗАТЕЛЬНО)
report_processed_events(event_id PRIMARY KEY, event_type, processed_at)
```
Предпочтительно держать read-model **пересобираемым**: где несложно — хранить состояние
на сущность (строка на занятие/ДЗ с текущим статусом) и считать `COUNT(...)`, а не голые
мутируемые счётчики. События кейятся по aggregate-id → порядок по конкретному
занятию/ДЗ/ученику сохранён, так что обновления безопасны. `student_name` в таблицах НЕ
храним (identity-событий нет) — имя подтягиваем из identity по gRPC на чтении.

## 5. Какие события слушать

```text
lesson.scheduled / lesson.completed / lesson.cancelled / lesson.rescheduled / lesson.restored
    -> upcoming/completed/cancelled counts, last_lesson_at, next_lesson_at
assignment.created / submission.uploaded / assignment.reviewed
    -> active/submitted/reviewed counts, pending_submissions (teacher)
balance.changed
    -> balance_amount/debt/overpaid (берём готовое значение), last_balance_event_at
payment_receipt.uploaded   -> pending_receipts_count/amount += 
payment.confirmed          -> pending_receipts_count/amount -= ; last_payment_at
payment.rejected           -> pending_receipts_count/amount -=
```
Идемпотентность каждого обработчика — через `report_processed_events` (атомарно с апдейтом
read-model, как в finance/notification). charge.created слушать не обязательно — баланс
берём из `balance.changed`.

## 6. gRPC API (v1 — узко под таблицы)

```proto
service ReportService {
  rpc GetTeacherDashboard(GetTeacherDashboardRequest) returns (TeacherDashboard);
  rpc GetStudentDashboard(GetStudentDashboardRequest) returns (StudentDashboard);
  rpc GetStudentSummary(GetStudentSummaryRequest) returns (StudentSummary); // одна пара teacher↔student
}
```
`GetStudentProgress` / `GetTeacherFinanceSummary` — отложены в v2 (пока нет отдельных
read-model; не дублировать dashboard). Ответы содержат `finance{ balance_amount, debt_amount,
overpaid_amount, currency, pending_receipts_count, pending_receipts_amount, last_payment_at,
updated_at }` + activity-счётчики; teacher — агрегаты по §2.

## 7. Gateway (внешние эндпоинты)

```text
GET /dashboard/teacher          -> ReportService.GetTeacherDashboard (teacher = X-User-Id)
GET /dashboard/student          -> ReportService.GetStudentDashboard (student = X-User-Id)
GET /students/{studentId}/summary -> ReportService.GetStudentSummary (teacher = X-User-Id, проверка связи)
```
Gateway тонкий: auth + routing + при необходимости обогащение имён из identity по gRPC.
Фронт постепенно переводим со старого набора запросов на dashboard-эндпоинты.

## 8. Cold-start / backfill (пререквизит работоспособности)

report стартует с пустыми таблицами; события до его появления сами не попадут. Решение
для MVP: consumer читает топики **с earliest offset** (малый объём данных покрывается
ретеншеном Kafka). `balance.changed` несёт абсолютное значение → даже при реплее «выигрывает
последнее» по ученику, баланс сходится. Read-model объявляем пересобираемым: процедура
ребилда (reset offset + truncate) — в README сервиса.

## 9. Definition of Done

1. `docker compose build report-service finance-service api-gateway` — OK; миграции `report_db` на чистую БД.
2. finance шлёт `balance.changed` с `balance_amount` (5H-0); contract-тест полей.
3. report строит read-models из событий; идемпотентность по `report_processed_events`
   (replay того же event_id не двоит счётчики — обязательный тест).
4. gateway отдаёт `/dashboard/teacher`, `/dashboard/student`, `/students/{id}/summary`;
   деньги: `debt_amount/overpaid_amount` по знаку, `total_debt_amount` = Σ debt (не Σ balance).
5. Чек на проверке НЕ уменьшает долг до `payment.confirmed`.
6. Ребилд read-model описан и проверен (reset + перечитать → те же значения).
7. `scripts/smoke_mvp.py` → SMOKE OK; `pytest tests` зелёный (+ dashboard-кейсы).
8. report наружу не публикуется (только gateway); report ничьи БД не читает.

## 10. Что НЕ делаем в 5H

report НЕ считает финансовую математику (берёт `balance_amount` готовым); НЕ source of
truth; `GetStudentProgress`/`GetTeacherFinanceSummary` — v2; identity-события не вводим
(имена через gRPC); сложную аналитику/графики — позже.
