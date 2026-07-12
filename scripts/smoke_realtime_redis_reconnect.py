#!/usr/bin/env python3

import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
READY_URLS = (
    "http://localhost:8089/ready",
    "http://localhost:8090/ready",
)
REPLICA_TEST = (
    "tests/test_realtime.py::"
    "test_ws_replicas_receive_one_copy_of_the_same_event"
)


def compose(*args: str) -> None:
    subprocess.run(
        ["docker", "compose", *args],
        cwd=ROOT,
        check=True,
    )


def run_cross_replica_test(retry_timeout: float = 0.0) -> None:
    deadline = time.monotonic() + retry_timeout
    retrying = retry_timeout > 0
    while True:
        result = subprocess.run(
            [sys.executable, "-m", "pytest", "-q", REPLICA_TEST],
            cwd=ROOT,
            check=False,
            capture_output=retrying,
            text=retrying,
        )
        if result.returncode == 0:
            if retrying:
                print(result.stdout, end="")
            return
        if time.monotonic() >= deadline:
            if retrying:
                print(result.stdout, end="", file=sys.stderr)
                print(result.stderr, end="", file=sys.stderr)
            raise subprocess.CalledProcessError(result.returncode, result.args)
        time.sleep(1)


def is_ready(url: str) -> bool:
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            if response.status != 200:
                return False
            body = json.loads(response.read())
            return body.get("status") == "ready"
    except (OSError, ValueError, urllib.error.URLError):
        return False


def wait_for_ready(url: str, expected: bool, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if is_ready(url) is expected:
            return
        time.sleep(0.5)
    raise TimeoutError(f"{url} did not become ready={expected}")


def main() -> None:
    run_cross_replica_test()

    try:
        compose("stop", "redis")
        for url in READY_URLS:
            wait_for_ready(url, expected=False)
    finally:
        compose("start", "redis")

    for url in READY_URLS:
        wait_for_ready(url, expected=True)
    run_cross_replica_test(retry_timeout=60.0)
    print("REALTIME REDIS RECONNECT SMOKE OK")


if __name__ == "__main__":
    main()
