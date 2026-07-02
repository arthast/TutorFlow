# TutorFlow — Frontend: план и прогресс

Рабочий документ по доведению фронтенда (`frontend/`, React + TS + Vite) до макетов
из `frontend/design/`. Координатор ведёт статус фаз и backend-беклог здесь.

## Принципы

- Все запросы — только через `src/api.ts` (api-gateway :8080). Контракты и backend не менять.
- Доработка на месте: сохранять структуру, переиспользовать примитивы `src/ui.tsx`.
- Новые npm-зависимости не добавлять без явной причины.
- Фиделити — прагматично близко к макетам (следовать дизайн-системе и компоновке).
- Backend-gap не фейкать: оставлять TODO и выносить в беклог ниже.
- Менять по одной фазе/экрану за заход; после каждого — `npm run build` зелёный, консоль чистая.

## Статус фаз

| Фаза | Экран(ы) | Статус |
|---|---|---|
| 0 — Основа | токены `styles.css`, примитивы `ui.tsx`, эталон `Teacher.tsx` | ✅ |
| 1 — Дашборд ученика | `Student.tsx` (+ общие `NotificationsPanel`/`MessagesCard`) | ✅ |
| 2 — Занятия | `TeacherLessons` (жизненный цикл), `StudentLessons` | ✅ |
| 3 — Домашние задания | `TeacherAssignments`, `TeacherAssignmentReview`, `StudentAssignments` | ✅ |
| 4 — Финансы и чеки | `TeacherFinance`, `TeacherReceipts`, `StudentPayments`, `StudentReceipts` | ✅ |
| 5 — Ученики | `TeacherStudents`, `TeacherStudentCard` | ✅ |
| 6 — Чат и уведомления | `ChatPage`, `NotificationsPanel` + тосты (`chat.tsx` удалён в Ф8) | ✅ |
| 7 — Вход | `Login`, `Register`, `AuthLayout`, смена временного пароля | ✅ |
| 8 — Сквозное | адаптив, скелетоны, пустые состояния, тосты, a11y, финальный QA | ✅ |

DoD фазы: визуал ≈ макет, все действия (поддержанные gateway) подключены,
есть loading/empty/error, `npm run build` зелёный.

## Фаза 8 — сделано (полировка)

- Чистка: удалены мёртвые `src/chat.tsx` (старый ChatCard), `TopBar`/`Notice`/`ListState`/
  `NotificationsCard` из `ui.tsx` и ~35 неиспользуемых CSS-классов (таймлайн сдач ДЗ,
  старый чат-виджет, `.role-segment`, `.topbar` и др.).
- Live «✓✓ прочитано» в `ChatPage` по realtime `chat.read`: realtime-service доставляет
  событие peer'у с payload `{dialog_id, reader_id, up_to_message_id}` (поле именно
  `reader_id`, не `user_id`). Свои сообщения до `up_to_message_id` получают `done_all`.
  Не переживает reload — персистентная версия в беклоге backend.
- Деньги: единые `money()`/`signedMoney()` в `ui.tsx` («15 000 ₽», «—» для пустых),
  реэкспорт из `teacherNav`/`studentNav`; локальные formatMoney/formatSignedBalance удалены.
- Состояния: общий `ErrorState` (EmptyState + «Повторить») на всех списках/карточках;
  скелетоны в чате (диалоги и тред), EmptyState для пустого списка диалогов.
- A11y: фокус-трап + возврат фокуса + `role="dialog"` в `Modal`, aria-label на всех
  icon-кнопках, `:focus-visible`-стили, клавиатурный `ListRow`, `type="button"` в
  Tabs/Segmented, `.visually-hidden` для скринридеров.
- Адаптив: мобильный дровер-сайдбар (≤760px, с бэкдропом; на узких экранах стартует
  закрытым независимо от localStorage), media-правила для `finance-kpi-grid`,
  `student-payment-layout`, `student-finance-row`, `student-overview-grid`,
  `assignment-list-row`, `receipt-row`, `finance-student-row`, `journal-row`,
  `page-tabs-bar`. Горизонтального скролла нет на 375px на всех экранах обеих ролей.

## Готовые примитивы (Фаза 0, `src/ui.tsx`)

`Button` (primary/secondary/ghost/danger × sm/md/lg, иконка, loading), `Field`/`Input`/
`Select`/`Textarea` (invalid/error), `StatusPill` (lesson/assignment вкл. expired,
receipt, finance), `Badge`, `Counter`, `Avatar` (+presence), `Metric`, `ListRow`,
`EmptyState`, `Skeleton`/`SkeletonRows`, `Tabs`, `Segmented`, `Modal`, `Toast`+`useToast`,
`Card` (title: ReactNode). Общие блоки: `NotificationsPanel`, `MessagesCard`.

## Backend-беклог (накоплено по ходу; не блокеры)

> Формализовано как задачи в `docs/roadmap.md` → «Этап 6 — доработки backend под
> фронтенд» (с приоритетами и признаком изменения контракта). Ниже — исходные заметки.

- **Bulk mark-read уведомлений** — нет пакетного эндпоинта; «Прочитать всё» шлёт N
  запросов `POST /notifications/{id}/read`. Возможный `POST /notifications/read-all` —
  только отдельным согласованным изменением контракта. Приоритет: low.
- **Разбивка баланса (начислено/оплачено)** — в `FinanceSummary`/`StudentDashboard` нет
  `total_charged`/`total_paid`. Это v2 (report-service `GetStudentSummary` FinanceSummary
  отложен в roadmap). Пока показываем доступное (чеки на проверке + сумма). Приоритет: v2.
- **Помесячные агрегаты финансов + выгрузка отчёта** (из макета «Финансы преподавателя») —
  эндпоинтов нет; KPI собраны из доступных полей, кнопка экспорта не добавлена. Приоритет: v2.
- **Причина отклонения чека** — фронт читает `receipt.comment` с fallback; подтвердить в
  finance-контракте, что `comment` заполняется при reject (если нет — backend-доработка).
- **Редактирование профиля ученика (КАНДИДАТ В BACKEND после фронта)** — `GET /students/{id}`
  есть, `PATCH/PUT` нет; `StudentLink` без email/телефона/формата/заметок/даты начала. Карточка
  показывает доступное read-only. Полноценное редактирование = новый эндпоинт + поля в identity,
  отдельным согласованным решением. Выгрузка журнала/презенс ученика в списке — тоже нет.
- **Персистентные «прочитано» в чате** — `Message` без флага read, GET read-маркера собеседника
  нет. Live-версия по realtime `chat.read` сделана в Фазе 8 (payload `reader_id`/`up_to_message_id`),
  но после reload состояние теряется. Полноценно нужен флаг в `Message` или GET маркера. Typing —
  события в контракте нет.
- **Признак первого входа (`must_change_password`/`is_temporary`)** — нет в `/me` и ответе логина;
  форс-экран смены временного пароля не делаем, только мягкая подсказка. Кандидат в backend
  (поле в identity). Сброс пароля («забыли пароль?») — эндпоинта нет.
- **KPI «учеников онлайн»** — агрегата presence нет (онлайн только пер-юзер по WS).
  Прокси «активных: N» по `status==active`. Полноценный online-count — вне MVP.
- **(НЕ backend) имя файла чека** — в `Receipt` только `file_id`; имя резолвится через
  `GET /files/{file_id}` (`original_name`), как делает `FileChip`. Применить на странице
  чеков в Фазе 4; на дашбордах сумма+дата достаточно.
- **(By design, не делаем) `completed → scheduled`** — обратного перехода нет намеренно
  (charge уже создан, finance append-only). Для завершённого занятия — только отмена
  (с компенсацией) или reactivate отменённого. Имя преподавателя в `Lesson` отсутствует —
  резолвится из `studentDashboard().summaries` с фолбэком; контракт не меняем.
- **(Уточнения по ДЗ, контракт не меняем)** решения ученика грузятся с `purpose=submission_file`
  (реальное значение enum, НЕ `submission_attachment`); вердикт `/review` принимает только
  `{status, comment}` (файл к проверке не прикладывается); `Comment` без имени автора — роль
  по сравнению с `teacher_id`; timeline попыток сдачи нет — заменён тредом комментариев
  (неиспользованный CSS таймлайна вычистить в Фазе 8).
