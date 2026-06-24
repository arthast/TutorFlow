#!/usr/bin/env python3
"""Демо-данные для показа TutorFlow через api-gateway (:8080).

Создаёт преподавателя, ученика, занятие, ДЗ и чек оплаты, затем печатает
demo-креды и ссылки. Идёт ТОЛЬКО в gateway (как фронт). Запуск:

    python3 scripts/demo_seed.py            # gateway на http://localhost:8080
    GATEWAY_URL=http://host:8080 python3 scripts/demo_seed.py

Повторный запуск создаёт новый набор (email с временной меткой), ничего не ломая.
"""

import json
import os
import sys
import uuid
from datetime import datetime, timedelta, timezone
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

BASE_URL = os.environ.get("GATEWAY_URL", "http://localhost:8080").rstrip("/")
STAMP = datetime.now().strftime("%Y%m%d-%H%M%S")
TEACHER_EMAIL = f"demo.teacher.{STAMP}@example.com"
TEACHER_PASS = "demo-teacher-pass"
STUDENT_EMAIL = f"demo.student.{STAMP}@example.com"
STUDENT_PASS = "demo-student-pass"


def die(msg: str, body: object = None) -> None:
    print(f"SEED FAIL: {msg}", file=sys.stderr)
    if body is not None:
        print(body, file=sys.stderr)
    sys.exit(1)


def call(method: str, path: str, *, token=None, body=None, raw=None, content_type=None):
    headers = {"Accept": "application/json"}
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    elif raw is not None:
        data = raw
        if content_type:
            headers["Content-Type"] = content_type
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = Request(BASE_URL + path, data=data, headers=headers, method=method)
    try:
        with urlopen(req, timeout=15) as resp:
            text = resp.read().decode("utf-8", "replace")
    except HTTPError as exc:
        die(f"{method} {path} -> {exc.code}", exc.read().decode("utf-8", "replace"))
    except URLError as exc:
        die(f"{method} {path} failed (gateway на {BASE_URL} поднят?): {exc}")
    return json.loads(text) if text else {}


def multipart(file_name: str, purpose: str):
    boundary = "----demo" + uuid.uuid4().hex
    content = f"demo receipt {STAMP}".encode()
    parts = [
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"purpose\"\r\n\r\n{purpose}\r\n".encode(),
        (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
            f"filename=\"{file_name}\"\r\nContent-Type: text/plain\r\n\r\n"
        ).encode()
        + content
        + b"\r\n",
        f"--{boundary}--\r\n".encode(),
    ]
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def main() -> None:
    print(f"gateway: {BASE_URL}")

    # 1. teacher
    tok = call("POST", "/auth/register", body={
        "email": TEACHER_EMAIL, "password": TEACHER_PASS,
        "role": "teacher", "display_name": f"Демо Преподаватель {STAMP}",
    })
    teacher_token = tok["access_token"]
    teacher_id = tok["user_id"]
    print("✓ преподаватель создан")

    # 2. student (email + временный пароль)
    call("POST", "/students", token=teacher_token, body={
        "email": STUDENT_EMAIL, "password": STUDENT_PASS,
        "display_name": f"Демо Ученик {STAMP}", "subject": "Математика", "hourly_rate": 1500,
    })
    students = call("GET", "/students", token=teacher_token)
    link = next((s for s in students if s.get("display_name", "").endswith(STAMP)), None)
    if not link:
        die("не нашёл созданного ученика в /students", students)
    student_id = link["student_id"]
    print("✓ ученик создан (логин + временный пароль)")

    # 3. lesson
    now = datetime.now(timezone.utc)
    call("POST", "/lessons", token=teacher_token, body={
        "student_id": student_id,
        "starts_at": (now + timedelta(days=1)).isoformat(),
        "ends_at": (now + timedelta(days=1, hours=1)).isoformat(),
        "topic": "Демо-занятие: производные",
    })
    print("✓ занятие создано")

    # 4. assignment
    call("POST", "/assignments", token=teacher_token, body={
        "student_id": student_id,
        "title": "Демо-ДЗ: задачи 1–5",
        "description": "Решить задачи из демо-набора.",
    })
    print("✓ ДЗ создано")

    # 5. receipt (от лица ученика)
    student_tok = call("POST", "/auth/login", body={"email": STUDENT_EMAIL, "password": STUDENT_PASS})
    student_token = student_tok["access_token"]
    raw, ctype = multipart("receipt.txt", "payment_receipt")
    meta = call("POST", "/files", token=student_token, raw=raw, content_type=ctype)
    call("POST", "/payments/receipts", token=student_token, body={
        "teacher_id": teacher_id, "file_id": meta["id"], "amount": 1500,
    })
    print("✓ чек оплаты загружен (ждёт подтверждения преподавателя)")

    print("\n=== DEMO готов ===")
    print(f"  Фронт:   http://localhost:5173   (gateway: {BASE_URL})")
    print(f"  Teacher: {TEACHER_EMAIL} / {TEACHER_PASS}")
    print(f"  Student: {STUDENT_EMAIL} / {STUDENT_PASS}")
    print("  Сценарий показа: войти преподавателем → подтвердить чек → завершить занятие →")
    print("  проверить ДЗ; войти учеником → отправить решение → загрузить ещё чек.")


if __name__ == "__main__":
    main()
