# Задача: frontend — автообновление страниц по realtime-событиям (roadmap 7.3, шаг 1)

Дата постановки: 2026-07-08. Постановка координатора (Claude).
Ветка: `feat/frontend-realtime-refresh`. Backend и realtime-service НЕ трогать —
это чисто фронтовая задача поверх существующего WebSocket-канала.

## Контекст (проверено по коду)

- realtime-service пушит ТОЛЬКО эти типы (см. `docs/agent-realtime-service.md`):
  `chat.message`, `chat.read`, `presence`, `notification`, `pong`.
  Типов `assignment.*` / `submission.*` / `review.*` / `lesson.*` НЕ существует.
- У события `notification` в payload есть поле `type` с доменным типом
  (`assignment.reviewed`, `submission.uploaded`, `payment.confirmed`,
  `lesson.completed`, …), плюс `user_id`, `notification_id`, `title`, `body`.
- Сломанные подписки (фильтруют по несуществующим верхнеуровневым типам,
  колбэк никогда не срабатывает):
  - `frontend/src/pages/StudentAssignments.tsx` (~строка 71):
    `["assignment","submission","review"].some(t => event.type.startsWith(t))`
  - `frontend/src/pages/TeacherAssignmentReview.tsx` (~строка 46): то же.
- Дашборды `Teacher.tsx` (~75) и `Student.tsx` (~84) РАБОТАЮТ — их список
  префиксов включает `notification`. Не ломать.
- Вообще не подписаны: `TeacherLessons.tsx`, `StudentLessons.tsx`,
  `TeacherReceipts.tsx`, `StudentPayments.tsx`, `TeacherFinance.tsx`,
  `StudentReceipts.tsx`, `StudentReceiptHistory.tsx`, `TeacherStudents.tsx`.
- Чат живёт на polling 3–5 с + realtime — его не трогать.
- Важно понимать модель доставки: `notification.created` приходит ПОЛУЧАТЕЛЮ
  события (student сдал ДЗ → notification у teacher). Собственные действия
  пользователя уже обновляются локальным reload после POST. Т.е. notification —
  правильный триггер именно для «чужих» изменений.

## Цель

Открытая страница любой роли отражает изменения, сделанные второй стороной
(сдача/проверка ДЗ, завершение/перенос урока, загрузка/подтверждение чека),
без ручного refresh.

## Что менять

### 1. `frontend/src/realtime.tsx` — общий хук

Добавить хук-обёртку (имя на твой вкус, например `useDomainRefresh`):

```ts
// вызывает reload, когда приходит notification с payload.type,
// начинающимся с одного из префиксов; плюс refetch при возврате фокуса вкладки
useDomainRefresh(reload: () => void, prefixes: string[]): void
```

Внутри:
- `useRealtimeEvent`: `event.type === "notification"` и
  `String(event.payload.type ?? "")` начинается с одного из `prefixes` → `reload()`;
- `visibilitychange`/`focus`: документ снова видим → `reload()` (не чаще раза
  в ~10 с, простым timestamp-guard, чтобы не дёргать API при alt-tab);
- никакого нового глобального состояния; подписка через существующий контекст.

### 2. Починить сломанные фильтры

- `StudentAssignments.tsx`, `TeacherAssignmentReview.tsx`: заменить текущий
  `useRealtimeEvent` на `useDomainRefresh(reload, ["assignment", "submission"])`
  (reload = текущие `assignments.reload()+dashboard.reload()` / `detail.reload()`).

### 3. Добавить подписки на неподписанные страницы

Каждой странице — её префиксы, reload = уже существующие reload-функции
`useAsync`:

| Страница | Префиксы |
|---|---|
| TeacherLessons / StudentLessons | `["lesson"]` |
| TeacherReceipts / StudentReceipts / StudentReceiptHistory | `["payment"]` |
| StudentPayments / TeacherFinance | `["payment", "charge", "balance"]` |
| TeacherStudents | `["lesson", "assignment", "payment"]` (счётчики карточек) |

Если на странице несколько `useAsync` — перезагружать все относящиеся к данным
домена (как это уже делает `reloadAll()` на дашбордах).

### 4. Дашборды

`Teacher.tsx` / `Student.tsx` работают — можно перевести на тот же
`useDomainRefresh` для единообразия (поведение не менять: триггер на любой
notification допустим, как сейчас), либо оставить как есть. На твоё усмотрение,
но без изменения поведения.

## Что НЕ делать

- Не трогать realtime-service, gateway, Kafka, контракты — расширение
  server-side событий это отдельный шаг 2 (roadmap 7.3).
- Не добавлять постоянный interval-polling на страницы (только focus-refetch);
  polling чата не трогать.
- Не добавлять библиотек (react-query и т.п.).
- Не менять `RealtimeToasts`/`NotificationsPanel` в `ui.tsx` — они уже работают.

## Проверка / DoD

1. `cd frontend && npx vite build` — без ошибок.
2. Два браузера (teacher + student), dev-стенд:
   - student сдаёт ДЗ → у teacher список заданий/ревью-страница обновляются сами;
   - teacher ставит review → у student страница заданий обновляется;
   - teacher завершает урок → у student уроки/дашборд обновляются;
   - student грузит чек → у teacher страница чеков обновляется;
   - teacher подтверждает чек → у student payments/дашборд обновляются.
3. Свернуть вкладку, сделать действие второй ролью, вернуть фокус → данные
   подтянулись и без WebSocket-события.
4. В консоли нет спама reload (guard по времени работает).
