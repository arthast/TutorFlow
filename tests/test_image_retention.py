import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "deploy/scripts/retain-images.sh"
NAMESPACE = "ghcr.io/arthast"


def run_retention(
    tmp_path: Path,
    *arguments: str,
    running_images: tuple[str, ...] = (),
    container_images: tuple[str, ...] = (),
    image_refs: tuple[str, ...] = (),
    initial_state: str | None = None,
) -> tuple[subprocess.CompletedProcess[str], str, list[str]]:
    tmp_path.mkdir(parents=True, exist_ok=True)
    fake_docker = tmp_path / "docker"
    fake_docker.write_text(
        """#!/usr/bin/env python3
import os
from pathlib import Path
import sys


args = sys.argv[1:]
if args[:1] == ["ps"]:
    key = (
        "FAKE_CONTAINER_IMAGES"
        if "-a" in args
        else "FAKE_RUNNING_IMAGES"
    )
    print(os.environ.get(key, ""))
elif args[:2] == ["image", "ls"]:
    print(os.environ.get("FAKE_IMAGE_REFS", ""))
elif args[:2] == ["image", "rm"] or args == ["image", "prune", "-f"]:
    log = Path(os.environ["FAKE_DOCKER_LOG"])
    with log.open("a") as output:
        output.write(" ".join(args) + "\\n")
else:
    print(f"unsupported fake docker command: {args}", file=sys.stderr)
    raise SystemExit(64)
"""
    )
    fake_docker.chmod(0o755)

    state_file = tmp_path / "retention.state"
    if initial_state is not None:
        state_file.write_text(initial_state)
    log_file = tmp_path / "docker.log"

    environment = os.environ.copy()
    environment.update(
        {
            "DOCKER_BIN": str(fake_docker),
            "FAKE_RUNNING_IMAGES": "\n".join(running_images),
            "FAKE_CONTAINER_IMAGES": "\n".join(container_images),
            "FAKE_IMAGE_REFS": "\n".join(image_refs),
            "FAKE_DOCKER_LOG": str(log_file),
        }
    )

    process = subprocess.run(
        [
            "bash",
            str(SCRIPT),
            *arguments,
            "--state-file",
            str(state_file),
        ],
        text=True,
        capture_output=True,
        env=environment,
        check=False,
    )
    state = state_file.read_text() if state_file.exists() else ""
    mutations = log_file.read_text().splitlines() if log_file.exists() else []
    return process, state, mutations


def test_first_remember_records_running_release_without_previous(
    tmp_path: Path,
) -> None:
    result, state, mutations = run_retention(
        tmp_path,
        "remember",
        "--namespace",
        NAMESPACE,
        running_images=(f"{NAMESPACE}/tutorflow-api-gateway:aaa",),
    )

    assert result.returncode == 0, result.stderr
    assert state == "current=aaa\nprevious=\n"
    assert mutations == []


def test_remember_does_not_promote_an_unverified_running_release(
    tmp_path: Path,
) -> None:
    result, state, mutations = run_retention(
        tmp_path,
        "remember",
        "--namespace",
        NAMESPACE,
        running_images=(f"{NAMESPACE}/tutorflow-api-gateway:failed",),
        initial_state="current=healthy\nprevious=older\n",
    )

    assert result.returncode == 0, result.stderr
    assert state == "current=healthy\nprevious=older\n"
    assert mutations == []


def test_new_successful_release_keeps_current_and_previous(
    tmp_path: Path,
) -> None:
    result, state, mutations = run_retention(
        tmp_path,
        "prune",
        "--namespace",
        NAMESPACE,
        "--current-tag",
        "bbb",
        initial_state="current=aaa\nprevious=\n",
        container_images=(f"{NAMESPACE}/tutorflow-api-gateway:bbb",),
        image_refs=(
            f"{NAMESPACE}/tutorflow-api-gateway:aaa",
            f"{NAMESPACE}/tutorflow-api-gateway:bbb",
        ),
    )

    assert result.returncode == 0, result.stderr
    assert state == "current=bbb\nprevious=aaa\n"
    assert mutations == ["image prune -f"]


def test_repeated_release_preserves_existing_previous(tmp_path: Path) -> None:
    result, state, mutations = run_retention(
        tmp_path,
        "prune",
        "--namespace",
        NAMESPACE,
        "--current-tag",
        "bbb",
        initial_state="current=bbb\nprevious=aaa\n",
        container_images=(f"{NAMESPACE}/tutorflow-api-gateway:bbb",),
        image_refs=(
            f"{NAMESPACE}/tutorflow-api-gateway:aaa",
            f"{NAMESPACE}/tutorflow-api-gateway:bbb",
        ),
    )

    assert result.returncode == 0, result.stderr
    assert state == "current=bbb\nprevious=aaa\n"
    assert mutations == ["image prune -f"]


def test_prune_never_removes_image_used_by_any_container(
    tmp_path: Path,
) -> None:
    used = f"{NAMESPACE}/tutorflow-lesson-service:legacy"
    result, _, mutations = run_retention(
        tmp_path,
        "prune",
        "--namespace",
        NAMESPACE,
        "--current-tag",
        "bbb",
        initial_state="current=aaa\nprevious=\n",
        container_images=(f"{NAMESPACE}/tutorflow-api-gateway:bbb", used),
        image_refs=(used,),
    )

    assert result.returncode == 0, result.stderr
    assert f"image rm {used}" not in mutations


def test_prune_removes_only_older_tutorflow_refs(tmp_path: Path) -> None:
    old_app = f"{NAMESPACE}/tutorflow-lesson-service:old"
    unrelated = "postgres:16-alpine"
    result, state, mutations = run_retention(
        tmp_path,
        "prune",
        "--namespace",
        NAMESPACE,
        "--current-tag",
        "bbb",
        initial_state="current=aaa\nprevious=\n",
        container_images=(f"{NAMESPACE}/tutorflow-api-gateway:bbb",),
        image_refs=(old_app, unrelated),
    )

    assert result.returncode == 0, result.stderr
    assert state == "current=bbb\nprevious=aaa\n"
    assert mutations == [f"image rm {old_app}", "image prune -f"]


def test_invalid_namespace_or_tag_performs_no_docker_mutation(
    tmp_path: Path,
) -> None:
    namespace_result, _, namespace_mutations = run_retention(
        tmp_path / "namespace",
        "prune",
        "--namespace",
        "../bad",
        "--current-tag",
        "bbb",
    )
    tag_result, _, tag_mutations = run_retention(
        tmp_path / "tag",
        "prune",
        "--namespace",
        NAMESPACE,
        "--current-tag",
        "bad/tag",
    )

    assert namespace_result.returncode != 0
    assert tag_result.returncode != 0
    assert namespace_mutations == []
    assert tag_mutations == []


def test_invalid_state_is_not_executed_or_mutated(tmp_path: Path) -> None:
    marker = tmp_path / "sourced"
    result, state, mutations = run_retention(
        tmp_path,
        "prune",
        "--namespace",
        NAMESPACE,
        "--current-tag",
        "bbb",
        initial_state=f"current=$(touch {marker})\nprevious=aaa\n",
    )

    assert result.returncode != 0
    assert state == f"current=$(touch {marker})\nprevious=aaa\n"
    assert not marker.exists()
    assert mutations == []
