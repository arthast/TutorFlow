import concurrent.futures
import threading
import time
import uuid

from tests import _client as api


CHAT_DIALOG_NAMESPACE = uuid.UUID("b6f0d896-6d38-4b6f-8ec4-58a4f7a3c1d2")


def expected_dialog_id(teacher_id, student_id):
    return str(uuid.uuid5(
        CHAT_DIALOG_NAMESPACE,
        f"{teacher_id.lower()}:{student_id.lower()}",
    ))


def expected_shard(dialog_id):
    value = 14695981039346656037
    for byte in dialog_id.replace("-", "").lower().encode("ascii"):
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return value % 2


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


def test_chat_sharding_determinism_scatter_gather_and_pagination(teacher):
    pairs = []
    shards = set()
    while len(pairs) < 3 or len(shards) < 2:
        assert len(pairs) < 12, {"shards": shards, "pairs": pairs}
        student = api.create_student(teacher["token"])
        dialog = open_dialog(teacher["token"], student["user_id"])
        expected_id = expected_dialog_id(teacher["user_id"], student["user_id"])
        assert dialog["id"] == expected_id
        assert open_dialog(student["token"], teacher["user_id"])["id"] == expected_id
        shard = expected_shard(expected_id)
        shards.add(shard)
        pairs.append((student, dialog, shard))

    touched_dialog_ids = []
    first_messages = []
    for index, (student, dialog, _) in enumerate(pairs):
        message_count = 3 if index == 0 else 1
        for message_index in range(message_count):
            status, message = api.post(
                f"/chats/{dialog['id']}/messages",
                token=teacher["token"],
                body={"text": f"dialog-{index}-message-{message_index}"},
            )
            assert status == 201, message
            if index == 0:
                first_messages.append(message)
        touched_dialog_ids.append(dialog["id"])
        time.sleep(0.02)

        status, student_dialogs = api.get("/chats", token=student["token"])
        assert status == 200, student_dialogs
        own_dialog = next(d for d in student_dialogs if d["id"] == dialog["id"])
        assert own_dialog["unread_count"] == message_count

    status, dialogs = api.get("/chats", token=teacher["token"])
    assert status == 200, dialogs
    assert [item["id"] for item in dialogs] == list(reversed(touched_dialog_ids))

    first_dialog_id = pairs[0][1]["id"]
    status, page = api.get(
        f"/chats/{first_dialog_id}/messages?limit=2",
        token=teacher["token"],
    )
    assert status == 200, page
    assert [item["id"] for item in page] == [
        first_messages[1]["id"], first_messages[2]["id"]]

    status, previous_page = api.get(
        f"/chats/{first_dialog_id}/messages?limit=2&before={page[0]['id']}",
        token=teacher["token"],
    )
    assert status == 200, previous_page
    assert [item["id"] for item in previous_page] == [first_messages[0]["id"]]


def test_create_dialog_is_idempotent_under_concurrency(teacher):
    student = api.create_student(teacher["token"])
    workers = 8
    barrier = threading.Barrier(workers)

    def create_once():
        barrier.wait(timeout=5)
        return api.post(
            "/chats",
            token=teacher["token"],
            body={"other_user_id": student["user_id"]},
        )

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        results = list(executor.map(lambda _: create_once(), range(workers)))

    assert [status for status, _ in results] == [201] * workers, results
    ids = {body["id"] for _, body in results}
    assert ids == {expected_dialog_id(teacher["user_id"], student["user_id"])}


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
