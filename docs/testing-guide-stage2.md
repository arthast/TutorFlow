# Этап 2 — Тесты: подробный гайд (для самостоятельной реализации)

> Цель этапа: поверх зелёного smoke зафиксировать корректность системы набором
> небольших независимых тестов. Не пишем 100 unit-тестов — проверяем поведение
> через gateway (как реальный клиент): health, негативный auth, контроль доступа,
> идемпотентность и правила баланса.
>
> Всё ходит ТОЛЬКО в gateway (http://localhost:8080). Внутренние сервисы не
> дёргаем напрямую. Тесты создают свои данные с уникальными email → повторяемы.

---

## 0. Что должно получиться

```text
tests/
  _client.py          # общий HTTP-клиент (stdlib, без зависимостей)
  conftest.py         # pytest-фикстуры (base_url, teacher, student, lesson...)
  test_health.py
  test_auth.py        # негативный auth + защита от подделки X-User-*
  test_access.py      # контроль доступа (чужой ученик / чужие ДЗ)
  test_finance.py     # идемпотентность charge + правила баланса/чеков
requirements-dev.txt  # pytest (dev-only зависимость)
```

7 ключевых проверок (из roadmap Этап 2):
1. нельзя подделать `X-User-Id` / `X-User-Roles` извне (gateway срезает);
2. ученик не видит чужие ДЗ;
3. преподаватель не работает с чужим учеником (check-access);
4. повторный `complete` не создаёт второй `charge`;
5. загрузка чека не меняет баланс;
6. баланс меняется только после `confirm`;
7. `rejected` чек не создаёт `payment`.

---

## 1. Подготовка окружения

```bash
cp .env.example .env            # если ещё нет
docker compose up -d --build    # поднять все сервисы + postgres
./scripts/migrate.sh all        # применить миграции
python3 scripts/smoke_mvp.py    # убедиться, что базовый flow зелёный (SMOKE OK)
```

Тесты гоняем при поднятом стеке. `requirements-dev.txt`:
```text
pytest>=8
```
Установка: `pip install -r requirements-dev.txt --break-system-packages`
(pytest — dev-only зависимость, в проде не нужна; добавление оправдано).

---

## 2. Общий клиент `tests/_client.py` (можно взять как есть)

Лёгкая обёртка над `urllib` (как в smoke), плюс хелперы регистрации/логина и
multipart-загрузки файла.

```python
import json
import os
import urllib.error
import urllib.request
import uuid

BASE_URL = os.environ.get("GATEWAY_URL", "http://localhost:8080")


def _request(method, path, token=None, body=None, extra_headers=None,
             multipart=None):
    url = BASE_URL + path
    headers = {}
    data = None
    if multipart is not None:          # multipart = (field_name, filename, content_bytes, purpose)
        boundary = "----tf" + uuid.uuid4().hex
        data = _build_multipart(boundary, multipart)
        headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"
    elif body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    if extra_headers:
        headers.update(extra_headers)

    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req) as r:
            raw = r.read().decode()
            return r.status, _parse(raw)
    except urllib.error.HTTPError as e:
        raw = e.read().decode()
        return e.code, _parse(raw)


def _parse(raw):
    try:
        return json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        return raw


def _build_multipart(boundary, multipart):
    field, filename, content, purpose = multipart
    nl = "\r\n"
    parts = []
    parts.append(f"--{boundary}{nl}")
    parts.append(f'Content-Disposition: form-data; name="purpose"{nl}{nl}')
    parts.append(f"{purpose}{nl}")
    parts.append(f"--{boundary}{nl}")
    parts.append(
        f'Content-Disposition: form-data; name="{field}"; filename="{filename}"{nl}')
    parts.append(f"Content-Type: application/octet-stream{nl}{nl}")
    head = "".join(parts).encode()
    tail = f"{nl}--{boundary}--{nl}".encode()
    return head + content + tail


def get(path, token=None, extra_headers=None):
    return _request("GET", path, token=token, extra_headers=extra_headers)


def post(path, token=None, body=None, extra_headers=None, multipart=None):
    return _request("POST", path, token=token, body=body,
                    extra_headers=extra_headers, multipart=multipart)


# ---- высокоуровневые хелперы ----

def unique_email(prefix):
    return f"{prefix}+{uuid.uuid4().hex[:10]}@example.test"


def register_teacher():
    email = unique_email("teacher")
    status, body = post("/auth/register", body={
        "email": email, "password": "passw0rd123", "role": "teacher",
        "display_name": "T " + email,
    })
    assert status == 201, (status, body)
    return {"email": email, "password": "passw0rd123",
            "token": body["access_token"], "user_id": body["user_id"]}


def create_student(teacher_token, temp_password="temp12345"):
    email = unique_email("student")
    status, link = post("/students", token=teacher_token, body={
        "email": email, "password": temp_password,
        "display_name": "S " + email, "hourly_rate": 1000,
    })
    assert status == 201, (status, link)
    # ученик логинится временным паролем
    s, tok = post("/auth/login", body={"email": email, "password": temp_password})
    assert s == 200, (s, tok)
    return {"email": email, "password": temp_password,
            "token": tok["access_token"], "user_id": tok["user_id"],
            "link": link, "teacher_id": link["teacher_id"]}


def upload_receipt_file(student_token):
    status, meta = post("/files", token=student_token,
                        multipart=("file", "receipt.txt", b"dummy receipt", "payment_receipt"))
    assert status == 201, (status, meta)
    return meta["id"]
```

> Если в проекте окажется, что какое-то поле/код отличается — правь клиент под
> реальный ответ (контракты в `docs/api-contracts/` — источник правды).

---

## 3. Фикстуры `tests/conftest.py`

```python
import pytest
from tests import _client as api


@pytest.fixture
def teacher():
    return api.register_teacher()


@pytest.fixture
def student(teacher):
    return api.create_student(teacher["token"])


@pytest.fixture
def lesson(teacher, student):
    status, body = api.post("/lessons", token=teacher["token"], body={
        "student_id": student["user_id"],
        "starts_at": "2026-07-01T10:00:00Z",
        "ends_at": "2026-07-01T11:00:00Z",
        "topic": "Test lesson",
        "price": 1000,
    })
    assert status == 201, (status, body)
    return body
```

(Чтобы `from tests import _client` работал — добавь пустой `tests/__init__.py`,
либо запускай pytest из корня репо. Проще: положи `_client.py` и тесты в один
пакет с `__init__.py`.)

---

## 4. Тесты — спецификация (пиши сам по шагам)

Для каждого теста: **дано → действие → проверить**. Коды статусов сверяй с
контрактом; где поведение зависит от реализации — посмотри фактический ответ и
зафиксируй его в ассерте (помечено ⚠️).

### test_health.py
- `GET /health` без токена → `200`, тело `{"status":"ok"}`.

### test_auth.py — негативный auth и защита от подделки (проверка #1)
1. **Нет токена** → `GET /me` без `Authorization` → `401`, тело в envelope
   (`body["error"]["code"]` присутствует).
2. **Битый токен** → `GET /me` с `Authorization: Bearer garbage` → `401`.
3. **Подделка X-User-Id игнорируется** → залогинь teacher A. Вызови `GET /me`
   с валидным токеном A, но добавь заголовок `X-User-Id: <random uuid>`.
   Ожидание: вернётся пользователь A (`body["user_id"] == A.user_id`), а не
   подставленный id. (Gateway срезает входящие `X-User-*`.)
4. **Подделка роли не повышает прав** → создай student S. Возьми его токен и
   попробуй учительское действие `POST /students` с заголовком
   `X-User-Roles: teacher`. Ожидание: `403` (gateway переустанавливает роль из
   токена = student; заголовок не действует). ⚠️ уточни код: 403 ожидается.
5. **change-password негатив** → для teacher A:
   - `POST /auth/change-password {current_password: "wrong", new_password: "abcd1234"}`
     → `401`;
   - `POST /auth/change-password {current_password: <верный>, new_password: "123"}`
     → `400` (короткий новый пароль).

### test_access.py — контроль доступа (проверки #2, #3)
1. **Преподаватель не работает с чужим учеником (#3)** →
   - teacher T1, student S1 (создан T1);
   - регистрируем teacher T2 (`register_teacher`);
   - T2 пытается `POST /lessons {student_id: S1.user_id, ...}` → `403`
     (check-access в identity отказывает: связи T2↔S1 нет);
   - аналогично T2 `POST /assignments {student_id: S1.user_id, title:"x"}` → `403`.
2. **Ученик не видит чужие ДЗ (#2)** →
   - teacher T, students S1 и S2 (оба у T);
   - T создаёт ДЗ для S1: `POST /assignments {student_id: S1.user_id, title:"A1"}` → `201`,
     запомни `assignment_id`;
   - логинимся как S2:
     - `GET /assignments` (токен S2) → в списке НЕТ `assignment_id` S1;
     - `GET /assignments/{assignment_id}` (токен S2) → НЕ `200`
       (ожидается `403` или `404`). ⚠️ посмотри фактический код и зафиксируй;
   - контроль-позитив: `GET /assignments/{assignment_id}` токеном S1 → `200`.

### test_finance.py — идемпотентность и баланс (проверки #4–#7)
Используй фикстуру `lesson` (teacher + student + занятие с `price=1000`).
Хелпер для чтения баланса:
```python
def balance(student):
    s, b = api.get(f"/students/{student['user_id']}/balance", token=student["token"])
    assert s == 200, (s, b)
    return b["balance"]
```

1. **Повторный complete не создаёт второй charge (#4)** →
   - `POST /lessons/{lesson.id}/complete` (teacher) → `200`;
   - `balance(student) == 1000`;
   - ещё раз `POST /lessons/{lesson.id}/complete` → `200` (или `409` —
     зависит от реализации ⚠️), но `balance(student)` всё ещё `1000`;
   - `GET /students/{id}/transactions` → ровно один элемент `type == "charge"`.
2. **Загрузка чека не меняет баланс (#5)** →
   - `file_id = upload_receipt_file(student.token)`;
   - `POST /payments/receipts {teacher_id: student.teacher_id, file_id, amount: 400}`
     (токен student) → `201`, запомни `receipt_id`, статус `pending_review`;
   - `balance(student) == 1000` (НЕ изменился).
3. **Баланс меняется только после confirm (#6)** →
   - `POST /payments/receipts/{receipt_id}/confirm` (токен teacher) → `200`;
   - `balance(student) == 600` (1000 − 400);
   - в `transactions` появился `type == "payment"` на 400.
4. **Rejected чек не создаёт payment (#7)** →
   - залей второй чек: `file_id2`, `POST /payments/receipts {... amount: 200}` → `201`,
     `receipt_id2`;
   - `POST /payments/receipts/{receipt_id2}/reject {comment:"bad"}` (teacher) → `200`;
   - `balance(student) == 600` (НЕ изменился);
   - в `transactions` НЕТ payment на 200 (всего один payment — на 400).

---

## 5. Запуск

```bash
pip install -r requirements-dev.txt --break-system-packages
GATEWAY_URL=http://localhost:8080 python3 -m pytest tests/ -v
```

Отдельный файл/тест:
```bash
python3 -m pytest tests/test_finance.py -v
python3 -m pytest tests/test_finance.py::test_complete_idempotent -v
```

---

## 6. Definition of Done
- все тесты зелёные на чистом `docker compose up` + миграции;
- тесты независимы и повторяемы (уникальные email на прогон, не зависят от порядка);
- покрыты все 7 проверок + health;
- ничего не ходит мимо gateway; внутренние сервисы не публикуются наружу;
- короткая инструкция запуска — в этом файле или в README (раздел Testing).

## 7. Грабли и подсказки
- **Уникальные email** на каждый прогон — иначе `users.email UNIQUE` даст ошибку
  (а дубль-email → 409 — это отдельная задача 1.6, для тестов просто не плодим дубли).
- **Где взять `teacher_id` для чека:** из `StudentLink` (поле `teacher_id`),
  которое вернул `POST /students` — он уже сохранён в `create_student`.
- **Знаки баланса:** `balance > 0` = долг ученика; `charge` увеличивает,
  `payment` уменьшает (`balance = Σcharge − Σpayment + Σcorrection − Σrefund`).
- **⚠️-места** (повторный complete: 200 vs 409; чужой assignment по id: 403 vs 404)
  — посмотри фактический ответ сервиса и зафиксируй его в ассерте; если решишь,
  что код «неправильный», это уже вопрос к контракту → через координатора, не
  меняй контракт молча.
- Если решишь обойтись без pytest — те же сценарии можно оформить как функции с
  `assert` и запускать одним runner-скриптом (как `smoke_mvp.py`). pytest просто
  удобнее группирует и показывает падения.
```

