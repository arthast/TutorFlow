# Задача: frontend — русские сообщения об ошибках (roadmap 7.5)

Дата постановки: 2026-07-08. Постановка координатора (Claude).
Ветка: `feat/frontend-ru-errors`. Backend НЕ трогать.

## Контекст (проверено по коду)

- Все сервисы возвращают envelope `{"error":{"code","message","details"}}`;
  `message` захардкожен на английском («assignment not found», «teacher-student
  relation is not active», …).
- Машинные коды едины для всех сервисов (`libs/common/include/tutorflow/common/error_codes.hpp`):
  `validation_error` (400), `unauthorized` (401), `forbidden` (403), `not_found` (404),
  `conflict` (409), `payload_too_large` (413), `unsupported_media_type` (415),
  `business_rule` (422), `internal_error` (500).
- Фронт бросает `ApiError(status, code, message)` в `frontend/src/api.ts`
  (функции `request` и `requestBlob`) и показывает `err.message` как есть:
  тосты `useToast` и `setError(...)` по страницам, компоненты `ErrorMsg`/`ErrorState`
  в `frontend/src/ui.tsx` (~строки 819, 825).
- Сетевая ошибка (fetch кинул TypeError — сервер недоступен) сейчас доходит
  до пользователя сырым «Failed to fetch».

## Цель

Пользователь видит понятный русский текст для любой ошибки. Точка перевода —
ОДНА, в `api.ts`; страницы не переписываем на 20 словарей.

## Что менять

### 1. `frontend/src/api.ts` — центральный перевод

- Словарь `RU_BY_CODE: Record<string, string>` по девяти кодам выше + fallback
  «Что-то пошло не так. Попробуйте ещё раз.». Примерные тексты:
  - `validation_error` → «Проверьте заполненные поля»
  - `unauthorized` → «Сессия истекла — войдите заново»
  - `forbidden` → «Нет доступа к этому действию»
  - `not_found` → «Не найдено — возможно, объект уже удалён»
  - `conflict` → «Действие уже выполнено или конфликтует с текущим состоянием»
  - `payload_too_large` → «Файл слишком большой»
  - `unsupported_media_type` → «Неподдерживаемый формат»
  - `business_rule` → «Действие недоступно по правилам платформы»
  - `internal_error` → «Ошибка на сервере. Попробуйте позже»
- В `ApiError` добавить поле `raw: string` (исходный английский message для
  отладки; `console.debug` при создании). `message` делать русским:
  `RU_BY_CODE[code] ?? fallback`.
- Уточнения по эндпоинту там, где generic-текст вреден, — через необязательный
  параметр `ruContext` или маленькую карту `path+code → текст` внутри api.ts
  (НЕ по страницам). Минимум обязательных уточнений:
  - `POST /assignments/{id}/submit` + `conflict` → «Задание уже закрыто — сдать решение нельзя»
  - `POST /auth/login` + `unauthorized` → «Неверный email или пароль»
  - `POST /students` + `conflict` → «Ученик с таким email уже существует»
  - `POST /files` + `payload_too_large` → «Файл слишком большой (лимит 10 МБ)»
- Обернуть `fetch` в try/catch: сетевая ошибка → `ApiError(0, "network_error",
  "Нет соединения с сервером. Проверьте интернет")`.

### 2. Точечная зачистка остатков

- Прогнать `grep -rn "(err as Error).message\|(e as Error).message" frontend/src` —
  эти места менять НЕ нужно (message уже русский после п.1), но проверить, что
  нигде не показывается `err.code`/статус без текста и что заголовки тостов
  согласуются с новым текстом (не «Ошибка: Ошибка»).
- `ui.tsx` `ErrorState`/`ErrorMsg` — убедиться, что дефолтные строки русские.
- `auth.tsx` — ошибки login/register проходят через тот же ApiError.

### 3. Что НЕ делать

- Не трогать backend, envelope, коды ошибок.
- Не добавлять i18n-библиотеку (react-i18next и т.п.) — просто словарь.
- Не переводить `raw` — он для консоли.
- Не менять UX-логику страниц (только тексты ошибок).

## Проверка / DoD

1. `cd frontend && npx vite build` — без ошибок типов.
2. Ручной прогон против dev-стенда: неверный пароль на логине; submit в закрытое
   задание; загрузка файла >10 МБ; открытие чужого ресурса (403); остановленный
   gateway (network error). Везде русский текст, в консоли — исходный английский.
3. `grep -rn "Failed to fetch\|assignment not found" frontend/src` — пользователю
   такие строки не отображаются.
