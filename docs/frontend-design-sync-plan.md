# Frontend design sync plan

Обновлено: 2026-06-28.

Цель: синхронизировать текущий React/Vite frontend с обновленными файлами в
`frontend/design/`, не меняя дизайн-систему и не меняя backend-контракты без
отдельного согласования.

## Источники

Backend-контракты: `docs/api-contracts/gateway.openapi.yaml`.
Frontend должен ходить только в `api-gateway` через `frontend/src/api.ts`.

Дизайн-референсы:

| Файл | Route / область | Статус реализации |
| --- | --- | --- |
| `Дизайн-система.dc.html` | tokens, shell, buttons, inputs, tabs, modal/toast | Использовать как общий источник визуальных правил. |
| `Вход и регистрация.dc.html` | `/login`, `/register`, first-login password change | Новый референс. Нужно привести auth-экраны к split layout и добавить состояние смены временного пароля, если контракт уже есть. |
| `Кабинет преподавателя.dc.html` | `/teacher` | Базово реализовано. После новых страниц проверить nav counters/toasts. |
| `Кабинет ученика.dc.html` | `/student` | Базово реализовано. Отдельные student routes остаются skeleton без новых референсов. |
| `Ученики.dc.html` | `/teacher/students` | Новый референс. Текущий skeleton нужно заменить на grid/list, filters, create-student modal и created-credentials step. |
| `Карточка ученика.dc.html` | `/teacher/students/:studentId` | Изменен. Нужно добавить tabbed layout, finance tab, operations journal и balance adjustment modal. |
| `Занятия преподавателя.dc.html` | `/teacher/lessons` | Изменен. Create/reschedule должны быть modal, строки уроков должны иметь tabs и kebab actions. |
| `Домашние задания.dc.html` | `/teacher/assignments` | Новый референс. Текущий skeleton заменить на tabs, list rows, create-homework modal с attachments. |
| `Проверка ДЗ.dc.html` | `/teacher/assignments/:assignmentId/review` | Изменен. Нужно сверить left assignment/submission/history area и right verdict panel. |
| `Финансы преподавателя.dc.html` | `/teacher/finance` | Новый референс. Текущий skeleton заменить на KPI, debt-by-student, recent operations и pending receipts shortcut. |
| `Чеки на подтверждение.dc.html` | `/teacher/receipts` | Новый референс. Текущий skeleton заменить на tabs, rule banner, receipt rows и confirm/reject modal. |
| `Чат.dc.html` | `/chat` | Базово реализовано. После общих UI правок сверить sidebar badges, search и composer. |
| `Визуальные направления.dc.html` | visual direction archive | Не использовать как отдельный экран; применять только если нужно сверить стиль. |

## Порядок реализации

### D0. Закрепить инвентарь

- Считать этот файл актуальным checklist для ближайших frontend-итераций.
- При появлении новых `.dc.html` сначала обновлять таблицу выше.
- Не удалять текущие рабочие skeleton routes до замены на дизайн-версию.

### D1. Общие UI-примитивы

Перед точечной версткой страниц вынести или доработать общие элементы:

- `modal` shell: overlay, close, footer actions, busy/error state;
- `toast` stack: success/error/info, dismiss, shared placement;
- `page toolbar`: title, menu button, right actions, notification badge;
- `tabs` with counters;
- `segmented filters`;
- `empty-state`;
- `file chip` and attachment upload row;
- `kebab/action menu` for lesson rows.

Критерий готовности: новые страницы собираются из общих классов/компонентов, а не
копируют крупные блоки CSS.

### D2. Auth

Routes: `/login`, `/register`.

- Привести layout к `Вход и регистрация.dc.html`: left brand panel + right form.
- Сохранить существующую auth-интеграцию через gateway.
- Добавить UI-состояние first-login password change только если оно уже
  поддержано текущим API. Если нет, зафиксировать gap, контракт не менять.
- Добавить toasts для success/error.

Проверка: login/register проходят в браузере; first-login state не ломает обычный
логин.

### D3. Teacher list pages

Routes: `/teacher/students`, `/teacher/assignments`, `/teacher/finance`,
`/teacher/receipts`.

Порядок:

1. `/teacher/students`: grid/list, search, filters, create-student modal,
   credentials step, links to student card.
2. `/teacher/assignments`: tabs by status, search, assignment rows, create-homework
   modal, attachments UI.
3. `/teacher/receipts`: pending/confirmed/rejected tabs, rule banner, file preview
   action, confirm/reject modal.
4. `/teacher/finance`: KPI, per-student debts, recent operations, pending receipts
   shortcut.

Backend gaps to check before coding:

- whether `GET /payments/receipts` returns enough file/student/status data for the
  receipt row and modal;
- whether finance has a cheap enough source for recent operations across all
  students. If not, show per-student debt from dashboard/students and keep journal
  scoped or partial, without adding new API silently;
- whether create-student response exposes temporary password for the credentials
  step.

### D4. Updated teacher detail pages

Routes: `/teacher/lessons`, `/teacher/students/:studentId`,
`/teacher/assignments/:assignmentId/review`.

- `/teacher/lessons`: replace inline create/reschedule with modal, add action menu,
  keep lifecycle actions mapped to existing endpoints.
- `/teacher/students/:studentId`: implement profile tabs; finance tab should show
  debt/accrued/paid cards, pending note, operations journal and adjustment modal.
- `/teacher/assignments/:assignmentId/review`: align assignment/submission/history
  layout and verdict panel; keep review submit flow through existing assignment API.

### D5. Student pages without new references

Student subpages still do not have separate page-level references beyond
`Кабинет ученика.dc.html`.

Keep current skeletons neutral:

- `/student/lessons`;
- `/student/assignments`;
- `/student/payments`;
- `/student/receipts`;
- `/student/settings`.

Do not invent new visual patterns here. Use existing shell, cards, tables, tabs,
modals and upload flow from the dashboard reference until designer provides
dedicated files.

### D6. QA per iteration

For every implementation chunk:

- `cd frontend && npm run build`;
- run Vite and manually check changed routes in browser;
- check desktop and narrow widths for horizontal overflow;
- check loading/error/empty states;
- check browser console for runtime errors;
- confirm all network calls still go through `frontend/src/api.ts` and gateway.

## Not doing now

- No new backend endpoints without explicit approval.
- No direct calls from frontend to internal services.
- No redesign of the supplied design system.
- No Telegram, email, real payments, calendar integrations.
- No separate student page visual invention beyond skeletons until references land.
