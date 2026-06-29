# TutorFlow frontend roadmap

> Рабочий план для агентов по фронтенду. Источник backend-контрактов:
> `docs/api-contracts/gateway.openapi.yaml`; frontend ходит только в
> `api-gateway` через `frontend/src/api.ts`. Если для страницы нет дизайн-референса,
> делаем только аккуратный скелет в текущей дизайн-системе, чтобы дизайнер позже
> мог заменить композицию без переписывания backend-интеграции.

Обновлено: 2026-06-28.

Текущая итерация: F1.2/F2/F3 skeleton routes созданы. Следующий фокус —
полировка UX, вынос общих компонентов и замена skeleton-композиций после
появления дизайн-референсов.

Актуальная синхронизация с обновленными `.dc.html` референсами:
`docs/frontend-design-sync-plan.md`.

---

## Текущее состояние

### Уже есть дизайн-референсы

Файлы в `frontend/design/`:

- `Дизайн-система.dc.html` — токены, shell, карточки, кнопки, pills, modal/toast.
- `Вход и регистрация.dc.html` — login/register и first-login password change.
- `Кабинет преподавателя.dc.html` — общая сводка преподавателя.
- `Кабинет ученика.dc.html` — общая сводка ученика.
- `Ученики.dc.html` — список учеников, grid/list, create-student modal.
- `Занятия преподавателя.dc.html` — расписание, фильтры, create/reschedule modal.
- `Карточка ученика.dc.html` — профиль ученика, финансы, журнал операций.
- `Домашние задания.dc.html` — список ДЗ, tabs, create-homework modal.
- `Проверка ДЗ.dc.html` — проверка домашней работы.
- `Финансы преподавателя.dc.html` — KPI, долги учеников, операции, shortcut к чекам.
- `Чеки на подтверждение.dc.html` — tabs, rule banner, confirm/reject modal.
- `Чат.dc.html` — полноэкранный чат.

### Уже реализовано в frontend

- Общий shell: `frontend/src/ui.tsx` (`AppShell`, `Card`, `Icon`,
  `StatusPill`, notifications, file chips).
- Общая дизайн-система: `frontend/src/styles.css` (Manrope, Material Symbols,
  role accents, sidebar/header, cards, tables/lists, modal, chat layout).
- Auth: `frontend/src/pages/Login.tsx`, `Register.tsx`, `frontend/src/auth.tsx`.
- Teacher dashboard: `frontend/src/pages/Teacher.tsx`.
- Student dashboard: `frontend/src/pages/Student.tsx`.
- Teacher lessons page: `frontend/src/pages/TeacherLessons.tsx`.
- Teacher student card: `frontend/src/pages/TeacherStudentCard.tsx`.
- Assignment review: `frontend/src/pages/TeacherAssignmentReview.tsx`.
- Full chat page: `frontend/src/pages/ChatPage.tsx`.

### Важное ограничение

Сейчас часть кабинетов работает как агрегированная страница с якорями
(`/teacher#students`, `/teacher#assignments`, `/teacher#finance`). Для следующих
итераций лучше постепенно выделять отдельные route-страницы, но не ломать
существующие dashboard-сценарии.

---

## Правила для дальнейшей frontend-реализации

1. **Не менять backend-контракты молча.** Если не хватает данных для UI, сначала
   зафиксировать gap и согласовать контракт.
2. **Все запросы только через gateway.** Не ходить во внутренние сервисы и не
   добавлять прямые service URLs во frontend.
3. **Скелеты без референсов должны быть нейтральными.** Использовать `AppShell`,
   `Card`, `metric`, `row`, `summary-row`, `StatusPill`, `modal-*`, `chat-page-*`;
   не придумывать новую визуальную тему.
4. **Не плодить mock-data в runtime.** Для пустых данных показывать empty-state.
   Mock/fixtures допустимы только в dev story/demo, если такая зона будет
   отдельно заведена.
5. **Route-first для новых экранов.** Даже если композиция временная, страница
   должна иметь стабильный route и nav item, чтобы дизайнер мог открыть экран.
6. **Никаких новых зависимостей без причины.** Текущий стек React + Vite +
   react-router-dom достаточен.
7. **Definition of Done для каждой frontend-итерации:** `npm run build`, ручная
   проверка в Vite URL, отсутствие console errors, корректные loading/error/empty
   states, отсутствие горизонтального overflow на desktop/mobile.

---

## Этап F1 — Укрепить frontend foundation

Цель: сделать основу удобной для добавления скелетов и будущего дизайна.

### F1.1 Разделить dashboard widgets и route pages

Сейчас `Teacher.tsx` и `Student.tsx` содержат много внутренних карточек. Нужно
постепенно вынести повторяемые pieces в `frontend/src/components/` или
`frontend/src/features/*`, не меняя поведение:

- metrics/grid primitives;
- list row primitives для lessons/assignments/receipts/transactions;
- page toolbar + segmented filters;
- modal form shell;
- empty/loading/error blocks.

DoD: страницы выглядят как сейчас, но новые screens можно собирать из общих
компонентов без копипасты.

### F1.2 Навигация и route map

Завести явную карту frontend routes рядом с nav helpers:

- teacher routes:
  - `/teacher`
  - `/teacher/students`
  - `/teacher/students/:studentId`
  - `/teacher/lessons`
  - `/teacher/assignments`
  - `/teacher/assignments/:assignmentId/review`
  - `/teacher/finance`
  - `/teacher/receipts`
  - `/teacher/chat`
  - `/teacher/settings`
- student routes:
  - `/student`
  - `/student/lessons`
  - `/student/assignments`
  - `/student/payments`
  - `/student/receipts`
  - `/student/chat`
  - `/student/settings`

DoD: nav items ведут на стабильные route-страницы; старые hash-ссылки можно
оставить как fallback только до переноса соответствующей страницы.

### F1.3 Состояния страниц

Для каждой route-страницы должны быть:

- loading state;
- empty state;
- error state с retry, где это уместно;
- disabled/busy state для action buttons;
- success notice/toast после изменения данных.

---

## Этап F2 — Teacher pages

Цель: покрыть кабинет преподавателя отдельными страницами. Если дизайна нет,
делаем рабочий skeleton в существующей системе.

### F2.1 `/teacher/students`

Скелет страницы списка учеников:

- header: `Ученики`, action `Новый ученик`;
- toolbar: search, status filter, subject filter placeholder;
- table/list: ученик, предмет/цель, ставка, долг, ближайшее занятие, статус;
- row click/link: `/teacher/students/:studentId`;
- modal/create panel: существующий create-student flow из `Teacher.tsx`.

Backend источники: `GET /students`, `GET /dashboard/teacher`,
`POST /students`.

### F2.2 `/teacher/assignments`

Скелет страницы домашних заданий:

- header: `Домашние задания`, action `Новое ДЗ`;
- tabs: `Все`, `Выдано`, `Сдано`, `На правках`, `Проверено`;
- list/table: title, student, status, created/submitted date, attachments;
- quick actions: `Проверка`, `Комментарий`, `Открыть`;
- create assignment modal из текущего `Teacher.tsx`.

Backend источники: `GET /assignments`, `POST /assignments`,
`GET /students`, `POST /assignments/{id}/comments`.

### F2.3 `/teacher/finance`

Скелет финансов:

- top metrics: общий долг, переплаты, pending receipts, должники;
- debt by student list;
- transactions journal with filters by student/type/date;
- manual correction entry point links to student card until separate design exists.

Backend источники: `GET /dashboard/teacher`, `GET /students`,
`GET /students/{id}/transactions`, `POST /students/{id}/corrections`.

### F2.4 `/teacher/receipts`

Скелет чеков:

- tabs: `На проверке`, `Подтверждены`, `Отклонены`, `Все`;
- list/table: student, amount, submitted_at, status, file action;
- actions: open file, confirm, reject;
- confirmation/rejection modal with explicit busy/error states.

Backend источники: `GET /payments/receipts`, `POST /payments/receipts/{id}/confirm`,
`POST /payments/receipts/{id}/reject`, file download через `openFile`.

### F2.5 Довести `/teacher/lessons`

По существующему референсу:

- create lesson уже должен быть modal;
- reschedule тоже привести к modal, а не inline-form;
- action menu для lesson row можно оставить skeleton, если нет готовой логики;
- добавить calendar/list view toggle как UI-skeleton, если backend данных хватает.

Backend источники уже есть: `GET /lessons`, `POST /lessons`,
`POST /lessons/{id}/complete|reschedule|cancel|reactivate`.

### F2.6 `/teacher/settings`

Скелет настроек:

- profile card: display_name/email read-only из `/me`;
- password change form;
- notification preferences placeholder, disabled until backend contract exists;
- danger zone placeholder не делать, пока нет контрактов.

Backend источники: `/me`, `POST /auth/change-password`.

---

## Этап F3 — Student pages

Цель: дать ученику отдельные страницы вместо одного длинного dashboard.

### F3.1 `/student/lessons`

Скелет:

- upcoming/completed/cancelled tabs;
- list with teacher name, topic, interval, status, price;
- attachments/materials entry if `file_ids` есть;
- empty state for no lessons.

Backend источники: `GET /lessons`, `GET /dashboard/student`.

### F3.2 `/student/assignments`

Скелет:

- tabs: `К сдаче`, `Сдано`, `На правках`, `Проверено`, `Все`;
- assignment detail drawer/modal skeleton;
- submit solution flow: text + file upload;
- comments thread block.

Backend источники: `GET /assignments`, `GET /assignments/{id}`,
`POST /assignments/{id}/submissions`, `POST /files`,
`POST /assignments/{id}/comments`.

### F3.3 `/student/payments`

Скелет:

- debt/overpaid/pending metrics by teacher;
- upload receipt modal: teacher select, amount, file;
- explanation-free operational copy: labels only, no marketing text;
- success/error state after upload.

Backend источники: `GET /dashboard/student`, `POST /files`,
`POST /payments/receipts`.

### F3.4 `/student/receipts`

Скелет:

- receipts list/table;
- status tabs;
- file open action;
- amount/date/teacher columns.

Backend источники: `GET /payments/receipts`, `openFile`.

### F3.5 `/student/settings`

Скелет:

- profile card: display_name/email read-only из `/me`;
- password change form;
- linked teachers summary from dashboard.

Backend источники: `/me`, `POST /auth/change-password`,
`GET /dashboard/student`.

---

## Этап F4 — Shared UX and realtime polish

### F4.1 Notifications surface

Сейчас есть карточка уведомлений. Нужен skeleton отдельной панели/страницы или
dropdown:

- unread/read filters;
- mark one read;
- mark all read only if backend endpoint exists; иначе не рисовать action.

Backend источники: `GET /notifications`, `POST /notifications/{id}/read`.

### F4.2 Chat polish

Уже есть full chat page. Следующие шаги:

- проверить mobile split layout;
- empty state for no selected dialog;
- message grouping by date;
- upload progress/busy state for attachments;
- unread badges в nav для teacher/student, если данные стабильно доступны.

### F4.3 File preview conventions

Сделать единый UI для файлов:

- file chip/list row;
- open/download action;
- loading/error state for file metadata if later появится endpoint metadata.

Пока хранить только `file_id`, как требует backend архитектура.

---

## Этап F5 — Design handoff readiness

Цель: чтобы дизайнер мог заменить скелеты без переписывания data-flow.

### F5.1 Page inventory

Поддерживать таблицу страниц:

| Route | Status | Design source | Backend wired |
|---|---|---|---|
| `/teacher` | implemented | `Кабинет преподавателя.dc.html` | yes |
| `/teacher/lessons` | implemented | `Занятия преподавателя.dc.html` | yes |
| `/teacher/students/:studentId` | implemented | `Карточка ученика.dc.html` | yes |
| `/teacher/assignments/:assignmentId/review` | implemented | `Проверка ДЗ.dc.html` | yes |
| `/teacher/chat` | implemented | `Чат.dc.html` | yes |
| `/student` | implemented | `Кабинет ученика.dc.html` | yes |
| `/student/chat` | implemented | `Чат.dc.html` | yes |
| `/teacher/students` | implemented skeleton | none | yes |
| `/teacher/assignments` | implemented skeleton | none | yes |
| `/teacher/finance` | implemented skeleton | none | yes |
| `/teacher/receipts` | implemented skeleton | none | yes |
| `/teacher/settings` | implemented skeleton | none | yes |
| `/student/lessons` | implemented skeleton | none | yes |
| `/student/assignments` | implemented skeleton | none | yes |
| `/student/payments` | implemented skeleton | none | yes |
| `/student/receipts` | implemented skeleton | none | yes |
| `/student/settings` | implemented skeleton | none | yes |

### F5.2 Visual QA checklist

Перед завершением каждой frontend задачи:

- desktop 1280px: no horizontal scroll, sidebar/header stable;
- mobile ~390px: no overlapping text/buttons;
- long names/topics/comments truncate or wrap intentionally;
- all icon buttons have `title`;
- no nested cards;
- no new one-off color palette outside `:root` tokens unless justified;
- `npm run build` passes.

### F5.3 Documentation updates

При добавлении route:

- обновить `frontend/README.md`;
- обновить route inventory в этом файле;
- если добавлен новый backend endpoint или изменён payload — остановиться и
  согласовать контракт до кода.
