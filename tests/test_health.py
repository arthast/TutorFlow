from tests import _client as api


def test_health_without_token():
    status, body = api.get("/health")

    assert status == 200, body
    assert body == {"status": "ok"}
