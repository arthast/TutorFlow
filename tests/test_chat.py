import time

from tests import _client as api


def wait_for_notification(token, ntype, predicate, timeout=10.0):
    deadline = time.monotonic() + timeout
    last = []
    while time.monotonic() < deadline:
        status, body = api.get("/notifications", token=token)
        assert status == 200, body
        last = body
        for item in body:
            if item["type"] == ntype and predicate(item):
                return item
        time.sleep(0.5)
    raise AssertionError({"type": ntype, "last": last})


def open_dialog(token, other_user_id):
    status, body = api.post(
        "/chats", token=token, body={"other_user_id": other_user_id})
    assert status == 201, body
    return body


def test_create_dialog_requires_link(teacher, student):
    # Преподаватель и НЕсвязанный с ним ученик -> 403.
    other_teacher = api.register_teacher()
    other_student = api.create_student(other_teacher["token"])
    status, body = api.post(
        "/chats", token=teacher["token"],
        body={"other_user_id": other_student["user_id"]})
    assert status == 403, body


def test_chat_happy_path(teacher, student):
    dialog = open_dialog(teacher["token"], student["user_id"])
    assert dialog["teacher_id"] == teacher["user_id"]
    assert dialog["student_id"] == student["user_id"]
    assert dialog["unread_count"] == 0

    # Идемпотентность: та же пара -> тот же диалог (с обеих сторон).
    assert open_dialog(teacher["token"], student["user_id"])["id"] == dialog["id"]
    assert open_dialog(student["token"], teacher["user_id"])["id"] == dialog["id"]

    status, msg = api.post(
        f"/chats/{dialog['id']}/messages", token=teacher["token"],
        body={"text": "Привет"})
    assert status == 201, msg
    assert msg["sender_id"] == teacher["user_id"]
    assert msg["text"] == "Привет"

    # Ученик видит сообщение и unread.
    status, dialogs = api.get("/chats", token=student["token"])
    assert status == 200, dialogs
    sd = next(d for d in dialogs if d["id"] == dialog["id"])
    assert sd["unread_count"] >= 1
    assert sd["last_message"]["text"] == "Привет"

    # message.sent -> уведомление получателю.
    wait_for_notification(
        student["token"], "message_sent",
        lambda n: n["payload"].get("message_id") == msg["id"])

    # Список сообщений по возрастанию.
    status, msgs = api.get(
        f"/chats/{dialog['id']}/messages", token=student["token"])
    assert status == 200, msgs
    assert [m["id"] for m in msgs] == [msg["id"]]

    # mark-read -> unread 0.
    status, marker = api.post(
        f"/chats/{dialog['id']}/read", token=student["token"],
        body={"up_to_message_id": msg["id"]})
    assert status == 200, marker
    assert marker["last_read_message_id"] == msg["id"]
    status, dialogs = api.get("/chats", token=student["token"])
    sd = next(d for d in dialogs if d["id"] == dialog["id"])
    assert sd["unread_count"] == 0


def test_non_participant_forbidden(teacher, student):
    dialog = open_dialog(teacher["token"], student["user_id"])
    outsider = api.register_teacher()
    status, body = api.get(
        f"/chats/{dialog['id']}/messages", token=outsider["token"])
    assert status == 403, body
    status, body = api.post(
        f"/chats/{dialog['id']}/messages", token=outsider["token"],
        body={"text": "intrusion"})
    assert status == 403, body
    status, body = api.post(
        f"/chats/{dialog['id']}/read", token=outsider["token"],
        body={"up_to_message_id": dialog["id"]})
    assert status == 403, body


def test_mark_read_only_moves_forward(teacher, student):
    dialog = open_dialog(teacher["token"], student["user_id"])
    status, m1 = api.post(
        f"/chats/{dialog['id']}/messages", token=teacher["token"],
        body={"text": "one"})
    assert status == 201, m1
    status, m2 = api.post(
        f"/chats/{dialog['id']}/messages", token=teacher["token"],
        body={"text": "two"})
    assert status == 201, m2

    api.post(f"/chats/{dialog['id']}/read", token=student["token"],
             body={"up_to_message_id": m2["id"]})
    status, marker = api.post(
        f"/chats/{dialog['id']}/read", token=student["token"],
        body={"up_to_message_id": m1["id"]})
    assert status == 200, marker
    # Указатель не откатывается назад.
    assert marker["last_read_message_id"] == m2["id"]


def test_attachment_downloadable_by_both_sides(teacher, student):
    dialog = open_dialog(teacher["token"], student["user_id"])
    status, meta = api.post(
        "/files", token=student["token"],
        multipart=("file", "note.txt", b"chat attachment", "chat_message"))
    assert status == 201, meta
    file_id = meta["id"]
    status, msg = api.post(
        f"/chats/{dialog['id']}/messages", token=student["token"],
        body={"text": "вложение", "file_ids": [file_id]})
    assert status == 201, msg
    assert msg["file_ids"] == [file_id]
    for token in (student["token"], teacher["token"]):
        status, _ = api.get(f"/files/{file_id}/download", token=token)
        assert status == 200


def test_send_requires_text_or_attachment(teacher, student):
    dialog = open_dialog(teacher["token"], student["user_id"])
    status, body = api.post(
        f"/chats/{dialog['id']}/messages", token=teacher["token"],
        body={"text": "   "})
    assert status == 400, body
