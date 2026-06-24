import json
import os
import time
import uuid
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, Optional, Tuple
from urllib.error import HTTPError
from urllib.request import Request, urlopen


BASE_URL = os.environ.get("GATEWAY_URL", "http://localhost:8080").rstrip("/")
PASSWORD = "passw0rd123"
LESSON_PRICE = 1000


def _parse(raw: bytes) -> Any:
    text = raw.decode("utf-8", errors="replace")
    if not text:
        return {}
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def _build_multipart(boundary: str, multipart: Tuple[str, str, bytes, str]) -> bytes:
    field, filename, content, purpose = multipart
    newline = "\r\n"
    head = "".join([
        f"--{boundary}{newline}",
        f'Content-Disposition: form-data; name="purpose"{newline}{newline}',
        f"{purpose}{newline}",
        f"--{boundary}{newline}",
        (
            f'Content-Disposition: form-data; name="{field}"; '
            f'filename="{filename}"{newline}'
        ),
        f"Content-Type: application/octet-stream{newline}{newline}",
    ]).encode("utf-8")
    tail = f"{newline}--{boundary}--{newline}".encode("utf-8")
    return head + content + tail


def request(
    method: str,
    path: str,
    *,
    token: Optional[str] = None,
    body: Optional[Dict[str, Any]] = None,
    extra_headers: Optional[Dict[str, str]] = None,
    multipart: Optional[Tuple[str, str, bytes, str]] = None,
) -> Tuple[int, Any]:
    headers = {"Accept": "application/json"}
    data = None
    if multipart is not None:
        boundary = "----tf" + uuid.uuid4().hex
        data = _build_multipart(boundary, multipart)
        headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"
    elif body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    if extra_headers:
        headers.update(extra_headers)

    req = Request(BASE_URL + path, data=data, headers=headers, method=method)
    try:
        with urlopen(req, timeout=10) as response:
            return response.status, _parse(response.read())
    except HTTPError as error:
        return error.code, _parse(error.read())


def get(path: str, *, token: Optional[str] = None,
        extra_headers: Optional[Dict[str, str]] = None) -> Tuple[int, Any]:
    return request("GET", path, token=token, extra_headers=extra_headers)


def post(path: str, *, token: Optional[str] = None,
         body: Optional[Dict[str, Any]] = None,
         extra_headers: Optional[Dict[str, str]] = None,
         multipart: Optional[Tuple[str, str, bytes, str]] = None) -> Tuple[int, Any]:
    return request(
        "POST",
        path,
        token=token,
        body=body,
        extra_headers=extra_headers,
        multipart=multipart,
    )


def unique_email(prefix: str) -> str:
    return f"{prefix}+{uuid.uuid4().hex[:12]}@example.test"


def user_id_from_me(token: str) -> str:
    status, body = get("/me", token=token)
    assert status == 200, (status, body)
    user_id = body.get("user_id") or body.get("id")
    assert user_id, body
    return user_id


def token_from(body: Dict[str, Any]) -> str:
    token = body.get("access_token")
    assert token, body
    return token


def register_teacher(password: str = PASSWORD) -> Dict[str, Any]:
    email = unique_email("teacher")
    status, body = post("/auth/register", body={
        "email": email,
        "password": password,
        "role": "teacher",
        "display_name": "Teacher " + email,
    })
    assert status == 201, (status, body)
    token = token_from(body)
    return {
        "email": email,
        "password": password,
        "token": token,
        "user_id": user_id_from_me(token),
    }


def login(email: str, password: str) -> Dict[str, Any]:
    status, body = post("/auth/login", body={"email": email, "password": password})
    assert status == 200, (status, body)
    token = token_from(body)
    return {"email": email, "password": password, "token": token,
            "user_id": user_id_from_me(token)}


def create_student(teacher_token: str, temp_password: str = "temp12345") -> Dict[str, Any]:
    email = unique_email("student")
    status, link = post("/students", token=teacher_token, body={
        "email": email,
        "password": temp_password,
        "display_name": "Student " + email,
        "hourly_rate": LESSON_PRICE,
    })
    assert status == 201, (status, link)
    login_data = login(email, temp_password)
    return {
        "email": email,
        "password": temp_password,
        "token": login_data["token"],
        "user_id": login_data["user_id"],
        "teacher_id": link["teacher_id"],
        "link": link,
    }


def iso_at(hours_from_now: int) -> str:
    value = datetime.now(timezone.utc) + timedelta(hours=hours_from_now)
    return value.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def create_lesson(teacher_token: str, student_id: str,
                  price: int = LESSON_PRICE) -> Dict[str, Any]:
    status, body = post("/lessons", token=teacher_token, body={
        "student_id": student_id,
        "starts_at": iso_at(24),
        "ends_at": iso_at(25),
        "topic": "Test lesson",
        "price": price,
    })
    assert status == 201, (status, body)
    return body


def create_assignment(teacher_token: str, student_id: str,
                      title: str = "Test assignment") -> Dict[str, Any]:
    status, body = post("/assignments", token=teacher_token, body={
        "student_id": student_id,
        "title": title,
        "description": "Test assignment description",
        "file_ids": [],
    })
    assert status == 201, (status, body)
    return body


def upload_receipt_file(student_token: str) -> str:
    status, meta = post(
        "/files",
        token=student_token,
        multipart=("file", "receipt.txt", b"dummy receipt", "payment_receipt"),
    )
    assert status == 201, (status, meta)
    return meta["id"]


def balance(student: Dict[str, Any], token: Optional[str] = None) -> float:
    status, body = get(
        f"/students/{student['user_id']}/balance",
        token=token or student["token"],
    )
    assert status == 200, (status, body)
    return float(body["balance"])


def transactions(student: Dict[str, Any], token: Optional[str] = None) -> Any:
    status, body = get(
        f"/students/{student['user_id']}/transactions",
        token=token or student["token"],
    )
    assert status == 200, (status, body)
    return body


def wait_for_lesson_charge(
    student: Dict[str, Any],
    lesson_id: str,
    *,
    amount: float = LESSON_PRICE,
    expected_balance: float = LESSON_PRICE,
    timeout: float = 15.0,
) -> Any:
    deadline = time.monotonic() + timeout
    last_balance = None
    last_charges = []

    while time.monotonic() < deadline:
        last_charges = [
            tx for tx in transactions(student)
            if tx["type"] == "charge" and tx.get("lesson_id") == lesson_id
        ]
        last_balance = balance(student)
        if (
            len(last_charges) == 1
            and round(float(last_charges[0]["amount"]), 2) == round(float(amount), 2)
            and round(float(last_balance), 2) == round(float(expected_balance), 2)
        ):
            return last_charges
        time.sleep(0.5)

    raise AssertionError({
        "lesson_id": lesson_id,
        "expected_amount": amount,
        "expected_balance": expected_balance,
        "last_balance": last_balance,
        "last_charges": last_charges,
    })
