#!/usr/bin/env python3

import json
import os
import sys
import time
import uuid
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, Iterable, List, Optional, Tuple
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


BASE_URL = os.environ.get("GATEWAY_URL", "http://localhost:8080").rstrip("/")


class SmokeFailure(Exception):
    pass


def fail(step: str, message: str, body: Optional[str] = None) -> None:
    print(f"SMOKE FAIL at {step}: {message}", file=sys.stderr)
    if body:
        print("Response body:", file=sys.stderr)
        print(body, file=sys.stderr)
    raise SmokeFailure(message)


def decode_json(step: str, body: bytes) -> Any:
    text = body.decode("utf-8", errors="replace")
    if not text:
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        fail(step, f"response is not JSON: {exc}", text)


def http_request(
    step: str,
    method: str,
    path: str,
    *,
    expected: Iterable[int],
    token: Optional[str] = None,
    json_body: Optional[Dict[str, Any]] = None,
    raw_body: Optional[bytes] = None,
    content_type: Optional[str] = None,
) -> Tuple[int, Any]:
    headers = {"Accept": "application/json"}
    data = None
    if json_body is not None:
        data = json.dumps(json_body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    elif raw_body is not None:
        data = raw_body
        if content_type:
            headers["Content-Type"] = content_type
    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = Request(BASE_URL + path, data=data, headers=headers, method=method)
    try:
        with urlopen(request, timeout=10) as response:
            status = response.getcode()
            body_bytes = response.read()
    except HTTPError as exc:
        status = exc.code
        body_bytes = exc.read()
    except URLError as exc:
        fail(step, f"request failed: {exc}")

    parsed = decode_json(step, body_bytes)
    if status not in set(expected):
        fail(step, f"{method} {path} expected {list(expected)}, got {status}",
             body_bytes.decode("utf-8", errors="replace"))
    return status, parsed


def post_json(step: str, path: str, body: Dict[str, Any], expected: Iterable[int],
              token: Optional[str] = None) -> Any:
    return http_request(
        step, "POST", path, expected=expected, token=token, json_body=body
    )[1]


def get_json(step: str, path: str, expected: Iterable[int],
             token: Optional[str] = None) -> Any:
    return http_request(step, "GET", path, expected=expected, token=token)[1]


def make_multipart(fields: Dict[str, str], file_field: str, filename: str,
                   content_type: str, content: bytes) -> Tuple[str, bytes]:
    boundary = "----TutorFlowSmoke" + uuid.uuid4().hex
    chunks: List[bytes] = []
    for name, value in fields.items():
        chunks.extend([
            f"--{boundary}\r\n".encode("ascii"),
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode("ascii"),
            value.encode("utf-8"),
            b"\r\n",
        ])
    chunks.extend([
        f"--{boundary}\r\n".encode("ascii"),
        (
            f'Content-Disposition: form-data; name="{file_field}"; '
            f'filename="{filename}"\r\n'
        ).encode("ascii"),
        f"Content-Type: {content_type}\r\n\r\n".encode("ascii"),
        content,
        b"\r\n",
        f"--{boundary}--\r\n".encode("ascii"),
    ])
    return f"multipart/form-data; boundary={boundary}", b"".join(chunks)


def require_dict(step: str, value: Any) -> Dict[str, Any]:
    if not isinstance(value, dict):
        fail(step, "expected JSON object", json.dumps(value, ensure_ascii=False))
    return value


def require_list(step: str, value: Any) -> List[Any]:
    if not isinstance(value, list):
        fail(step, "expected JSON array", json.dumps(value, ensure_ascii=False))
    return value


def require_field(step: str, obj: Dict[str, Any], name: str) -> Any:
    value = obj.get(name)
    if value in (None, ""):
        fail(step, f"missing field: {name}", json.dumps(obj, ensure_ascii=False))
    return value


def require_equal(step: str, actual: Any, expected: Any, message: str) -> None:
    if actual != expected:
        fail(step, f"{message}: expected {expected!r}, got {actual!r}")


def require_money(step: str, actual: Any, expected: float, message: str) -> None:
    try:
        value = float(actual)
    except (TypeError, ValueError):
        fail(step, f"{message}: expected numeric value, got {actual!r}")
    if abs(value - expected) > 0.001:
        fail(step, f"{message}: expected {expected}, got {value}")


def token_from(step: str, obj: Any) -> str:
    data = require_dict(step, obj)
    token = data.get("access_token")
    if not isinstance(token, str) or not token:
        fail(step, "missing access_token", json.dumps(data, ensure_ascii=False))
    return token


def iso_at(hours_from_now: int) -> str:
    value = datetime.now(timezone.utc) + timedelta(hours=hours_from_now)
    return value.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def transactions_for(step: str, token: str, student_id: str) -> List[Dict[str, Any]]:
    data = get_json(step, f"/students/{student_id}/transactions", [200], token)
    return [require_dict(step, item) for item in require_list(step, data)]


def balance_for(step: str, token: str, student_id: str) -> Dict[str, Any]:
    return require_dict(
        step, get_json(step, f"/students/{student_id}/balance", [200], token)
    )


def wait_for_lesson_charge(step: str, token: str, student_id: str,
                           lesson_id: str, lesson_price: float,
                           timeout_seconds: float = 15.0) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    deadline = time.monotonic() + timeout_seconds
    last_charges: List[Dict[str, Any]] = []
    last_balance: Dict[str, Any] = {}

    while time.monotonic() < deadline:
        transactions = transactions_for(step, token, student_id)
        last_charges = [
            tx for tx in transactions
            if tx.get("type") == "charge" and tx.get("lesson_id") == lesson_id
        ]
        last_balance = balance_for(step, token, student_id)
        try:
            balance_value = float(last_balance.get("balance"))
            charge_amount = (
                float(last_charges[0].get("amount"))
                if len(last_charges) == 1 else None
            )
        except (TypeError, ValueError):
            balance_value = float("nan")
            charge_amount = None
        if (
            len(last_charges) == 1
            and charge_amount is not None
            and abs(charge_amount - lesson_price) <= 0.001
            and abs(balance_value - lesson_price) <= 0.001
        ):
            return last_charges, last_balance
        time.sleep(0.5)

    fail(
        step,
        "charge/balance did not become visible before timeout",
        json.dumps(
            {"charges": last_charges, "balance": last_balance},
            ensure_ascii=False,
        ),
    )


def corrections_for(step: str, token: str, student_id: str,
                    lesson_id: str) -> List[Dict[str, Any]]:
    return [
        tx for tx in transactions_for(step, token, student_id)
        if tx.get("type") == "correction" and tx.get("lesson_id") == lesson_id
    ]


def charges_for(step: str, token: str, student_id: str,
                lesson_id: str) -> List[Dict[str, Any]]:
    return [
        tx for tx in transactions_for(step, token, student_id)
        if tx.get("type") == "charge" and tx.get("lesson_id") == lesson_id
    ]


def wait_for_lesson_correction(step: str, token: str, student_id: str,
                               lesson_id: str,
                               timeout_seconds: float = 15.0) -> List[Dict[str, Any]]:
    # Компенсация отменённого завершённого занятия создаётся consumer'ом
    # из события lesson.cancelled — eventual, как и charge.
    deadline = time.monotonic() + timeout_seconds
    last: List[Dict[str, Any]] = []
    while time.monotonic() < deadline:
        last = corrections_for(step, token, student_id, lesson_id)
        if len(last) == 1:
            return last
        time.sleep(0.5)
    fail(
        step,
        "compensation correction did not become visible before timeout",
        json.dumps({"corrections": last}, ensure_ascii=False),
    )


def main() -> int:
    run_id = f"{int(time.time())}-{uuid.uuid4().hex[:8]}"
    teacher_email = f"teacher-{run_id}@example.com"
    student_email = f"student-{run_id}@example.com"
    teacher_password = "TeacherPass123"
    student_temp_password = "StudentTemp123"
    student_new_password = "StudentNew123"
    lesson_price = 1000.0

    try:
        step = "1 register teacher"
        teacher_register = post_json(
            step,
            "/auth/register",
            {
                "email": teacher_email,
                "password": teacher_password,
                "role": "teacher",
                "display_name": "Smoke Teacher",
            },
            [201],
        )
        teacher_token = token_from(step, teacher_register)

        step = "2 login teacher"
        teacher_login = post_json(
            step,
            "/auth/login",
            {"email": teacher_email, "password": teacher_password},
            [200],
        )
        teacher_token = token_from(step, teacher_login)

        step = "3 create student"
        student_link = require_dict(
            step,
            post_json(
                step,
                "/students",
                {
                    "email": student_email,
                    "password": student_temp_password,
                    "display_name": "Smoke Student",
                    "subject": "Math",
                    "goal": "MVP smoke",
                    "hourly_rate": lesson_price,
                },
                [201],
                teacher_token,
            ),
        )
        student_id = require_field(step, student_link, "student_id")
        teacher_id = require_field(step, student_link, "teacher_id")
        require_equal(step, student_link.get("status"), "active", "student link status")

        step = "4 student login temp and change password"
        student_temp_login = post_json(
            step,
            "/auth/login",
            {"email": student_email, "password": student_temp_password},
            [200],
        )
        student_token = token_from(step, student_temp_login)
        change_response = require_dict(
            step,
            post_json(
                step,
                "/auth/change-password",
                {
                    "current_password": student_temp_password,
                    "new_password": student_new_password,
                },
                [200],
                student_token,
            ),
        )
        require_equal(step, change_response.get("status"), "ok", "change password")

        step = "5 student re-login new password"
        student_login = post_json(
            step,
            "/auth/login",
            {"email": student_email, "password": student_new_password},
            [200],
        )
        student_token = token_from(step, student_login)

        step = "6 teacher creates lesson"
        lesson = require_dict(
            step,
            post_json(
                step,
                "/lessons",
                {
                    "student_id": student_id,
                    "starts_at": iso_at(24),
                    "ends_at": iso_at(25),
                    "topic": "Smoke lesson",
                    "price": lesson_price,
                },
                [201],
                teacher_token,
            ),
        )
        lesson_id = require_field(step, lesson, "id")
        require_equal(step, lesson.get("status"), "scheduled", "lesson status")
        require_money(step, lesson.get("price"), lesson_price, "lesson price")

        step = "7 teacher creates assignment"
        assignment = require_dict(
            step,
            post_json(
                step,
                "/assignments",
                {
                    "student_id": student_id,
                    "title": "Smoke assignment",
                    "description": "Solve the smoke task",
                    "due_at": iso_at(24 * 7),
                    "file_ids": [],
                },
                [201],
                teacher_token,
            ),
        )
        assignment_id = require_field(step, assignment, "id")
        require_equal(step, assignment.get("status"), "assigned", "assignment status")

        step = "8 student submits solution"
        submission = require_dict(
            step,
            post_json(
                step,
                f"/assignments/{assignment_id}/submit",
                {"text_answer": "Smoke solution", "file_ids": []},
                [201],
                student_token,
            ),
        )
        require_equal(step, submission.get("assignment_id"), assignment_id,
                      "submission assignment")
        require_equal(step, submission.get("status"), "submitted", "submission status")

        step = "9 teacher reviews assignment"
        reviewed = require_dict(
            step,
            post_json(
                step,
                f"/assignments/{assignment_id}/review",
                {"status": "accepted", "comment": "Looks good"},
                [200],
                teacher_token,
            ),
        )
        require_equal(step, reviewed.get("assignment_id"), assignment_id,
                      "review assignment")
        require_equal(step, reviewed.get("status"), "accepted", "review status")

        step = "10 teacher comments assignment"
        comment = require_dict(
            step,
            post_json(
                step,
                f"/assignments/{assignment_id}/comments",
                {"text": "Smoke follow-up comment"},
                [201],
                teacher_token,
            ),
        )
        require_equal(step, comment.get("assignment_id"), assignment_id,
                      "comment assignment")
        require_equal(step, comment.get("author_id"), teacher_id,
                      "comment author")
        require_equal(step, comment.get("text"), "Smoke follow-up comment",
                      "comment text")

        step = "11 teacher completes lesson"
        complete_response = require_dict(
            step,
            post_json(step, f"/lessons/{lesson_id}/complete", {}, [200], teacher_token),
        )
        completed_lesson = require_dict(step, complete_response.get("lesson"))
        require_equal(step, completed_lesson.get("status"), "completed",
                      "lesson complete status")
        require_equal(step, complete_response.get("charge_status"), "pending",
                      "lesson complete charge_status")

        step = "12 finance creates charge eventually"
        charges, balance = wait_for_lesson_charge(
            step, teacher_token, student_id, lesson_id, lesson_price
        )
        require_equal(step, len(charges), 1, "charge count for lesson")
        require_money(step, charges[0].get("amount"), lesson_price, "charge amount")
        require_money(step, balance.get("balance"), lesson_price,
                      "balance after charge")

        repeat_complete = require_dict(
            step,
            post_json(step, f"/lessons/{lesson_id}/complete", {}, [200], teacher_token),
        )
        require_equal(step, repeat_complete.get("charge_status"), "pending",
                      "repeat complete charge_status")
        transactions_after_repeat = transactions_for(step, teacher_token, student_id)
        repeat_charges = [
            tx for tx in transactions_after_repeat
            if tx.get("type") == "charge" and tx.get("lesson_id") == lesson_id
        ]
        require_equal(step, len(repeat_charges), 1,
                      "idempotent complete charge count")
        repeat_balance = balance_for(step, teacher_token, student_id)
        require_money(step, repeat_balance.get("balance"), lesson_price,
                      "idempotent complete balance")

        step = "13 student uploads receipt"
        content_type, multipart_body = make_multipart(
            {"purpose": "payment_receipt"},
            "file",
            "receipt.txt",
            "text/plain",
            b"TutorFlow smoke receipt\n",
        )
        uploaded_file = require_dict(
            step,
            http_request(
                step,
                "POST",
                "/files",
                expected=[201],
                token=student_token,
                raw_body=multipart_body,
                content_type=content_type,
            )[1],
        )
        file_id = require_field(step, uploaded_file, "id")
        require_equal(step, uploaded_file.get("purpose"), "payment_receipt",
                      "file purpose")
        receipt = require_dict(
            step,
            post_json(
                step,
                "/payments/receipts",
                {
                    "teacher_id": teacher_id,
                    "file_id": file_id,
                    "amount": lesson_price,
                    "currency": "RUB",
                    "comment": "Smoke payment receipt",
                },
                [201],
                student_token,
            ),
        )
        receipt_id = require_field(step, receipt, "id")
        require_equal(step, receipt.get("status"), "pending_review", "receipt status")
        balance_before_confirm = balance_for(step, teacher_token, student_id)
        require_money(step, balance_before_confirm.get("balance"), lesson_price,
                      "balance before receipt confirm")

        step = "13 teacher confirms receipt"
        confirmed = require_dict(
            step,
            post_json(
                step,
                f"/payments/receipts/{receipt_id}/confirm",
                {},
                [200],
                teacher_token,
            ),
        )
        require_equal(step, confirmed.get("status"), "confirmed", "receipt confirm")

        step = "14 finance creates payment"
        transactions = transactions_for(step, teacher_token, student_id)
        payments = [
            tx for tx in transactions
            if tx.get("type") == "payment" and tx.get("receipt_id") == receipt_id
        ]
        require_equal(step, len(payments), 1, "payment count for receipt")
        require_money(step, payments[0].get("amount"), lesson_price, "payment amount")

        step = "15 balance reflects charge minus payment"
        final_balance = balance_for(step, teacher_token, student_id)
        require_equal(step, final_balance.get("student_id"), student_id,
                      "balance student_id")
        require_money(step, final_balance.get("balance"), 0.0, "final balance")

        # 5L.3-5L.4: отмена ЗАВЕРШЁННОГО занятия -> finance асинхронно добавляет
        # компенсирующую correction(-price). charge НЕ удаляется (append-only),
        # вклад занятия в баланс возвращается к 0. Ученик уже оплатил -> остаётся
        # кредит (-price), что допустимо (design §3).
        step = "16 cancel completed lesson"
        cancelled_lesson = require_dict(
            step,
            post_json(step, f"/lessons/{lesson_id}/cancel", {}, [200], teacher_token),
        )
        require_equal(step, cancelled_lesson.get("status"), "cancelled",
                      "lesson status after cancel")

        step = "16 finance compensates charge eventually"
        corrections = wait_for_lesson_correction(
            step, teacher_token, student_id, lesson_id
        )
        require_money(step, corrections[0].get("amount"), -lesson_price,
                      "compensation correction amount")
        post_charges = charges_for(step, teacher_token, student_id, lesson_id)
        require_equal(step, len(post_charges), 1,
                      "charge still present after cancel (append-only)")
        lesson_net = (float(post_charges[0].get("amount"))
                      + float(corrections[0].get("amount")))
        require_money(step, lesson_net, 0.0,
                      "lesson net contribution after compensation")

        step = "16 repeat cancel/replay does not double-compensate"
        repeat_cancel = require_dict(
            step,
            post_json(step, f"/lessons/{lesson_id}/cancel", {}, [200], teacher_token),
        )
        require_equal(step, repeat_cancel.get("status"), "cancelled",
                      "repeat cancel status")
        time.sleep(2.0)
        corrections_again = corrections_for(step, teacher_token, student_id, lesson_id)
        require_equal(step, len(corrections_again), 1,
                      "idempotent compensation correction count")

    except SmokeFailure:
        return 1

    print("SMOKE OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
