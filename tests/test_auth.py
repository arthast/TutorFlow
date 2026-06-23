import uuid

from tests import _client as api


def assert_error_envelope(body):
    assert isinstance(body, dict), body
    assert "error" in body, body
    assert body["error"].get("code"), body


def test_me_requires_token():
    status, body = api.get("/me")

    assert status == 401
    assert_error_envelope(body)


def test_me_rejects_invalid_token():
    status, body = api.get("/me", token="garbage")

    assert status == 401
    assert_error_envelope(body)


def test_gateway_ignores_forged_user_id_header(teacher):
    forged_user_id = str(uuid.uuid4())

    status, body = api.get(
        "/me",
        token=teacher["token"],
        extra_headers={"X-User-Id": forged_user_id},
    )

    assert status == 200, body
    assert (body.get("user_id") or body.get("id")) == teacher["user_id"]


def test_gateway_ignores_forged_role_header(teacher, student):
    status, body = api.post(
        "/students",
        token=student["token"],
        extra_headers={"X-User-Roles": "teacher"},
        body={
            "email": api.unique_email("forged-role"),
            "password": "temp12345",
            "display_name": "Forbidden student",
            "hourly_rate": 1000,
        },
    )

    assert status == 403, body


def test_change_password_negative_cases(teacher):
    status, body = api.post(
        "/auth/change-password",
        token=teacher["token"],
        body={"current_password": "wrong-password", "new_password": "abcd1234"},
    )
    assert status == 401, body
    assert_error_envelope(body)

    status, body = api.post(
        "/auth/change-password",
        token=teacher["token"],
        body={"current_password": teacher["password"], "new_password": "123"},
    )
    assert status == 400, body
    assert_error_envelope(body)
