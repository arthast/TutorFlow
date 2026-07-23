# Docker Runtime, Compose Layout, and Image Retention Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce small two-stage backend images, consolidate production Compose under `deploy/compose`, and automatically keep only the current and previous successful TutorFlow releases on the production host.

**Architecture:** The existing pinned userver image remains the builder, while a pinned Ubuntu 22.04 stage receives only the service binary, configs, CA certificates, and libraries discovered from `ldd`. Production logging and observability are merged into one Compose file. A host-side Bash script records release state before deployment and removes only old, unused TutorFlow SHA tags after public health succeeds.

**Tech Stack:** Docker BuildKit, Docker Compose v2, Bash, Python 3.12/pytest, GitHub Actions, userver/C++20, Ubuntu 22.04.

## Execution result (2026-07-23)

- Compose consolidation, minimal runtime images, retention, CI/deploy wiring,
  and tracked README references were implemented in scoped commits.
- General files under `docs/` are ignored in this checkout, so ignored local
  runbooks were not added back to Git; this tracked plan and the root README
  document the new paths and rollout boundary.
- Fresh verification covered 27 focused tests, collection of all 83 Python
  tests, three Compose render modes, actionlint, two amd64 runtime image builds,
  runtime dependency checks, and diff hygiene.
- No production rollout was triggered. Production still requires pushing an
  exact commit, running `Manual Tests` for that SHA, and invoking `Deploy` with
  the same SHA.

## Global Constraints

- Preserve the default root `docker-compose.yml` and the zero-argument `docker compose` local workflow.
- Remove old production and scale Compose paths without compatibility wrappers.
- Keep production logging rotation and observability mandatory.
- Keep exactly the current and previous successful TutorFlow release tags when available.
- Never delete an image referenced by any container.
- Never delete containers, networks, volumes, PostgreSQL, Kafka, Redis, MinIO, or Caddy data.
- Run retention pruning only after the public production health check succeeds.
- Do not change public API, gRPC, Kafka event, database, or persistent-volume contracts.
- Do not stage or commit unrelated files from the dirty working tree.
- Do not deploy an image that has not passed the existing Manual Tests gate for its exact commit.

---

## File Map

**Create**

- `deploy/compose/production.yml` — complete mandatory production stack.
- `deploy/compose/production.local-build.yml` — optional source-build overrides.
- `deploy/compose/local.kafka-cluster.yml` — optional three-broker local Kafka overlay.
- `deploy/scripts/retain-images.sh` — scoped release-state and image cleanup tool.
- `tests/test_docker_deploy_layout.py` — repository layout and workflow path checks.
- `tests/test_image_retention.py` — fake-Docker behavioral tests for retention.

**Modify**

- `docker/service.Dockerfile` — split builder and runtime stages while keeping
  `curl` available for existing container health checks.
- `tests/test_userver_toolchain.py` — assert the runtime boundary and new local-build path.
- `tests/test_file_s3_standardization.py` — read the moved production Compose file.
- `tests/test_realtime_redis_standardization.py` — read the moved production Compose file.
- `.github/workflows/ci.yml` — render all supported Compose combinations.
- `.github/workflows/deploy.yml` — use the new production path and retention phases.
- `README.md` — document the new Compose commands and paths.
- `docs/deploy.md` — update production, rollback, diagnostics, and local-scale commands.
- `docs/adr/0003-service-replicas-and-kafka-scaling.md` — update the Kafka overlay path.
- `docs/roadmap.md` — update live file references without rewriting historical decisions.

**Remove after equivalent files exist**

- `docker-compose.prod.yml`
- `docker-compose.prod.logging.yml`
- `docker-compose.prod.observability.yml`
- `docker-compose.prod.local-build.yml`
- `docker-compose.scale.yml`

---

### Task 1: Lock the target Compose layout with failing tests

**Files:**

- Create: `tests/test_docker_deploy_layout.py`
- Modify: `tests/test_userver_toolchain.py`
- Modify: `tests/test_file_s3_standardization.py`
- Modify: `tests/test_realtime_redis_standardization.py`

**Interfaces:**

- Consumes: current root Compose files and existing static pytest conventions.
- Produces: exact path expectations used by every later task.

- [ ] **Step 1: Add a failing layout test**

Create `tests/test_docker_deploy_layout.py` with:

```python
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "deploy/compose/production.yml"
LOCAL_BUILD = ROOT / "deploy/compose/production.local-build.yml"
KAFKA_CLUSTER = ROOT / "deploy/compose/local.kafka-cluster.yml"


def read(path: Path) -> str:
    return path.read_text()


def test_only_default_compose_remains_in_repository_root() -> None:
    compose_files = sorted(path.name for path in ROOT.glob("docker-compose*.yml"))
    assert compose_files == ["docker-compose.yml"]


def test_compose_variants_live_under_deploy_compose() -> None:
    assert PRODUCTION.is_file()
    assert LOCAL_BUILD.is_file()
    assert KAFKA_CLUSTER.is_file()


def test_production_compose_includes_mandatory_operations_services() -> None:
    production = read(PRODUCTION)
    assert "  prometheus:" in production
    assert "  kafka-exporter:" in production
    assert "  grafana:" in production
    assert 'max-size: "10m"' in production
    assert 'max-file: "3"' in production
    assert production.count("logging: *production-logging") >= 21


def test_active_automation_uses_only_new_production_path() -> None:
    active_files = (
        ROOT / ".github/workflows/ci.yml",
        ROOT / ".github/workflows/deploy.yml",
        ROOT / "README.md",
        ROOT / "docs/deploy.md",
    )
    combined = "\n".join(read(path) for path in active_files)
    assert "deploy/compose/production.yml" in combined
    for old_path in (
        "docker-compose.prod.yml",
        "docker-compose.prod.logging.yml",
        "docker-compose.prod.observability.yml",
        "docker-compose.prod.local-build.yml",
        "docker-compose.scale.yml",
    ):
        assert old_path not in combined
```

- [ ] **Step 2: Point existing static tests at the target paths**

Change:

```python
read("docker-compose.prod.yml")
```

to:

```python
read("deploy/compose/production.yml")
```

in the file-service and realtime static tests. Change the local-build test to:

```python
compose = read("deploy/compose/production.local-build.yml")
```

- [ ] **Step 3: Run the tests and verify RED**

Run:

```bash
python3 -m pytest \
  tests/test_docker_deploy_layout.py \
  tests/test_userver_toolchain.py \
  tests/test_file_s3_standardization.py \
  tests/test_realtime_redis_standardization.py -q
```

Expected: failures because `deploy/compose/*.yml` does not exist and old root files still exist.

---

### Task 2: Move and consolidate Compose configuration

**Files:**

- Create: `deploy/compose/production.yml`
- Create: `deploy/compose/production.local-build.yml`
- Create: `deploy/compose/local.kafka-cluster.yml`
- Remove: five old root Compose variant files

**Interfaces:**

- Consumes: root `docker-compose.yml`, existing production base, logging overlay, observability overlay, and optional overlays.
- Produces: the three paths locked by Task 1.

- [ ] **Step 1: Move the optional overlays**

Move content without behavior changes:

```text
docker-compose.prod.local-build.yml
  -> deploy/compose/production.local-build.yml

docker-compose.scale.yml
  -> deploy/compose/local.kafka-cluster.yml
```

Use `apply_patch` moves so Git records renames and no unrelated files are touched.

- [ ] **Step 2: Build the consolidated production file**

Move `docker-compose.prod.yml` to `deploy/compose/production.yml`. Add:

```yaml
x-production-logging: &production-logging
  driver: json-file
  options:
    max-size: "10m"
    max-file: "3"
```

Add:

```yaml
logging: *production-logging
```

to every production service, including `migrator`, `kafka-init`, `minio-init`,
Prometheus, Kafka Exporter, and Grafana. Append the three observability services
and the `prometheusdata` and `grafanadata` volumes from the existing
observability overlay.

- [ ] **Step 3: Remove superseded overlays**

Delete:

```text
docker-compose.prod.logging.yml
docker-compose.prod.observability.yml
```

only after their logging and observability definitions are present in the
consolidated production file.

- [ ] **Step 4: Verify Compose path semantics**

Run:

```bash
docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  config >/tmp/tutorflow-production.yml

docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  -f deploy/compose/production.local-build.yml \
  config >/tmp/tutorflow-production-local-build.yml

docker compose \
  -f docker-compose.yml \
  -f deploy/compose/local.kafka-cluster.yml \
  config >/tmp/tutorflow-local-kafka-cluster.yml
```

Expected: all three commands exit 0; production renders Prometheus, Grafana,
Kafka Exporter, and bounded logging.

- [ ] **Step 5: Run the targeted static tests**

Run the Task 1 pytest command again.

Expected: layout-related tests pass; automation-path test may remain red until
Task 5.

- [ ] **Step 6: Commit the Compose move**

Stage only the moved Compose files and the three adjusted existing tests:

```bash
git add \
  deploy/compose \
  tests/test_docker_deploy_layout.py \
  tests/test_userver_toolchain.py \
  tests/test_file_s3_standardization.py \
  tests/test_realtime_redis_standardization.py \
  docker-compose.prod.yml \
  docker-compose.prod.logging.yml \
  docker-compose.prod.observability.yml \
  docker-compose.prod.local-build.yml \
  docker-compose.scale.yml
git commit -m "refactor: consolidate docker compose layouts"
```

---

### Task 3: Build a minimal backend runtime image

**Files:**

- Modify: `tests/test_userver_toolchain.py`
- Modify: `docker/service.Dockerfile`

**Interfaces:**

- Consumes: `SERVICE`, `BUILD_JOBS`, and the pinned userver build image.
- Produces: the same `/app/service --config /app/configs/static_config.yaml` runtime interface with no build tree.

- [ ] **Step 1: Add failing runtime-boundary assertions**

Add:

```python
def test_shared_dockerfile_has_minimal_runtime_stage() -> None:
    dockerfile = read(SHARED_DOCKERFILE)
    assert " AS builder" in dockerfile
    assert "ARG RUNTIME_IMAGE=ubuntu:22.04@" in dockerfile
    assert "FROM ${RUNTIME_IMAGE} AS runtime" in dockerfile
    assert "ldd " in dockerfile
    assert "COPY --from=builder /runtime-root/lib/ /usr/lib/" in dockerfile
    assert "COPY --from=builder /runtime-root/usr/lib/ /usr/lib/" in dockerfile
    assert "COPY --from=builder /runtime/service /app/service" in dockerfile
    assert "COPY --from=builder /runtime/configs /app/configs" in dockerfile
    runtime = dockerfile.split("FROM ${RUNTIME_IMAGE} AS runtime", 1)[1]
    assert "COPY libs " not in runtime
    assert "cmake --build" not in runtime
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m pytest tests/test_userver_toolchain.py::test_shared_dockerfile_has_minimal_runtime_stage -q
```

Expected: failure because the Dockerfile is single-stage.

- [ ] **Step 3: Implement builder packaging**

Declare both base-image arguments before the first `FROM`, then refactor the
first stage to `AS builder`:

```dockerfile
ARG USERVER_IMAGE=ghcr.io/userver-framework/ubuntu-22.04-userver:v3.1@sha256:c08af6bf58f07a472376ed0bb74165e3d96fb5c8f4e07a3f0b5e11d5d0183f5b
ARG RUNTIME_IMAGE=ubuntu:22.04@sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982

FROM ${USERVER_IMAGE} AS builder
```

After the Release build:

```dockerfile
RUN set -eux; \
    binary="/src/build/services/${SERVICE}/${SERVICE}"; \
    test -x "${binary}"; \
    install -D -m 0755 "${binary}" /runtime/service; \
    cp -a "/src/services/${SERVICE}/configs" /runtime/configs; \
    mkdir -p /runtime-root/lib /runtime-root/usr/lib /runtime-root/usr/local/lib; \
    ldd "${binary}" \
      | awk '/=> \\// {print $3} /^\\// {print $1}' \
      | sort -u \
      | xargs -r -I '{}' cp -L --parents '{}' /runtime-root; \
    ! ldd "${binary}" | grep -q 'not found'
```

- [ ] **Step 4: Implement the runtime stage**

Use a pinned Ubuntu 22.04 image:

```dockerfile
FROM ${RUNTIME_IMAGE} AS runtime

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
# Ubuntu's /lib is a symlink to /usr/lib, so copy packaged libraries into the
# real runtime directories instead of trying to replace that symlink.
COPY --from=builder /runtime-root/lib/ /usr/lib/
COPY --from=builder /runtime-root/usr/lib/ /usr/lib/
COPY --from=builder /runtime-root/usr/local/lib/ /usr/local/lib/
COPY --from=builder /runtime/service /app/service
COPY --from=builder /runtime/configs /app/configs

ENTRYPOINT ["/app/service", "--config", "/app/configs/static_config.yaml"]
```

The digest above is the manifest-list digest returned on 2026-07-23 by:

```bash
docker buildx imagetools inspect ubuntu:22.04
```

Recheck it before implementation. If the mutable tag has changed, use the new
manifest-list digest reported for `ubuntu:22.04`; do not copy an
architecture-specific digest.

- [ ] **Step 5: Run static tests and build two dependency variants**

Run:

```bash
python3 -m pytest tests/test_userver_toolchain.py -q

docker build \
  --platform linux/amd64 \
  -f docker/service.Dockerfile \
  --build-arg SERVICE=lesson-service \
  --build-arg BUILD_JOBS=1 \
  -t tutorflow/lesson-service:runtime-check .

docker build \
  --platform linux/amd64 \
  -f docker/service.Dockerfile \
  --build-arg SERVICE=realtime-service \
  --build-arg BUILD_JOBS=1 \
  -t tutorflow/realtime-service:runtime-check .
```

Expected: both builds exit 0.

- [ ] **Step 6: Inspect runtime contents and dependencies**

Run:

```bash
for image in \
  tutorflow/lesson-service:runtime-check \
  tutorflow/realtime-service:runtime-check
do
  docker run --rm --entrypoint /bin/sh "$image" -ec '
    test ! -e /src
    ! command -v cmake
    ! command -v c++
    command -v curl
    ! ldd /app/service | grep -q "not found"
    test -r /etc/ssl/certs/ca-certificates.crt
  '
  docker image inspect "$image" --format '{{.RepoTags}} {{.Size}}'
done
```

Expected: both inspection containers exit 0 and each final image is materially
smaller than the current approximately 2.5 GB image.

- [ ] **Step 7: Commit the runtime image change**

```bash
git add docker/service.Dockerfile tests/test_userver_toolchain.py
git commit -m "build: ship minimal userver runtime images"
```

---

### Task 4: Implement release retention test-first

**Files:**

- Create: `tests/test_image_retention.py`
- Create: `deploy/scripts/retain-images.sh`

**Interfaces:**

- Consumes:
  - `remember --namespace <registry/path> --state-file <path>`
  - `prune --namespace <registry/path> --current-tag <tag> --state-file <path>`
  - optional `DOCKER_BIN`, defaulting to `docker`.
- Produces: a two-line state file with validated `current=` and `previous=` values and explicit `docker image rm` calls for old TutorFlow refs.

- [ ] **Step 1: Build a fake Docker test harness**

Create a pytest helper that writes an executable `docker` program into
`tmp_path`. The fake reads:

```text
FAKE_RUNNING_IMAGES
FAKE_IMAGE_REFS
FAKE_USED_IMAGES
FAKE_DOCKER_LOG
```

and implements only:

```text
docker ps --format {{.Image}}
docker ps -a --format {{.Image}}
docker image ls --format {{.Repository}}:{{.Tag}}
docker image rm <ref>
docker image prune -f
```

Each mutating command appends its full argument vector to `FAKE_DOCKER_LOG`.

- [ ] **Step 2: Add failing behavior tests**

Use a `run_retention` helper with this interface:

```python
def run_retention(
    tmp_path: Path,
    *arguments: str,
    running_images: tuple[str, ...] = (),
    container_images: tuple[str, ...] = (),
    image_refs: tuple[str, ...] = (),
    initial_state: str | None = None,
) -> tuple[subprocess.CompletedProcess[str], str, list[str]]:
    """Return process, final state text, and mutating fake-Docker calls."""
```

Cover the required state transitions and exact deletions:

```python
NAMESPACE = "ghcr.io/arthast"


def test_first_remember_records_running_release_without_previous(tmp_path: Path) -> None:
    result, state, mutations = run_retention(
        tmp_path,
        "remember",
        "--namespace",
        NAMESPACE,
        running_images=(f"{NAMESPACE}/tutorflow-api-gateway:aaa",),
    )
    assert result.returncode == 0
    assert state == "current=aaa\nprevious=\n"
    assert mutations == []


def test_new_successful_release_keeps_current_and_previous(tmp_path: Path) -> None:
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
    assert result.returncode == 0
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
    assert result.returncode == 0
    assert state == "current=bbb\nprevious=aaa\n"
    assert mutations == ["image prune -f"]


def test_prune_never_removes_image_used_by_any_container(tmp_path: Path) -> None:
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
    assert result.returncode == 0
    assert f"image rm {used}" not in mutations


def test_prune_removes_only_older_tutorflow_refs(tmp_path: Path) -> None:
    old_app = f"{NAMESPACE}/tutorflow-lesson-service:old"
    unrelated = "postgres:16-alpine"
    result, _, mutations = run_retention(
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
    assert result.returncode == 0
    assert mutations == [f"image rm {old_app}", "image prune -f"]


def test_invalid_namespace_or_tag_performs_no_docker_mutation(tmp_path: Path) -> None:
    namespace_result, _, namespace_mutations = run_retention(
        tmp_path,
        "prune",
        "--namespace",
        "../bad",
        "--current-tag",
        "bbb",
    )
    tag_result, _, tag_mutations = run_retention(
        tmp_path,
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
```

Assertions must verify exact retained state and exact `image rm` arguments, not
only the exit code.

- [ ] **Step 3: Run retention tests and verify RED**

Run:

```bash
python3 -m pytest tests/test_image_retention.py -q
```

Expected: failure because `deploy/scripts/retain-images.sh` does not exist.

- [ ] **Step 4: Implement strict input and state parsing**

The script starts with:

```bash
#!/usr/bin/env bash
set -euo pipefail

docker_bin="${DOCKER_BIN:-docker}"

valid_namespace() {
  [[ "$1" =~ ^[a-zA-Z0-9.-]+(:[0-9]+)?(/[a-zA-Z0-9._-]+)*$ ]]
}

valid_tag() {
  [[ "$1" =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$ ]]
}
```

Parse the state file line-by-line; never `source` it. Reject duplicate,
unknown, or invalid keys before invoking Docker.

- [ ] **Step 5: Implement remember**

Use the running API gateway reference:

```bash
"$docker_bin" ps --format '{{.Image}}'
```

Select exactly:

```text
${namespace}/tutorflow-api-gateway:<tag>
```

Initialize `current` when no state exists. If running differs from recorded
current, set `previous` to the recorded current and `current` to running. If it
matches, preserve the existing previous value. Write state atomically through
a sibling temporary file and `mv`.

- [ ] **Step 6: Implement prune**

After validating and loading state:

1. promote `current-tag` only when it differs;
2. collect all container image references from `docker ps -a`;
3. enumerate `docker image ls`;
4. keep current, previous, and every used reference;
5. remove only refs matching `${namespace}/tutorflow-*:*`;
6. run `docker image prune -f`;
7. atomically persist the successful state.

Do not use `docker system prune`, `docker image prune -a`, globs passed to
Docker, or volume cleanup.

- [ ] **Step 7: Run tests and shell syntax verification**

Run:

```bash
bash -n deploy/scripts/retain-images.sh
python3 -m pytest tests/test_image_retention.py -q
```

Expected: all tests pass.

- [ ] **Step 8: Commit retention**

```bash
git add deploy/scripts/retain-images.sh tests/test_image_retention.py
git commit -m "ops: retain two tutorflow image releases"
```

---

### Task 5: Wire CI and deployment to the new layout

**Files:**

- Modify: `.github/workflows/ci.yml`
- Modify: `.github/workflows/deploy.yml`
- Modify: `tests/test_docker_deploy_layout.py`

**Interfaces:**

- Consumes: new Compose path and retention CLI from Tasks 2 and 4.
- Produces: validated build/deploy flow with post-health cleanup.

- [ ] **Step 1: Extend the failing workflow assertions**

Assert:

```python
def test_deploy_remembers_before_up_and_prunes_after_health() -> None:
    workflow = read(ROOT / ".github/workflows/deploy.yml")
    remember = workflow.index("retain-images.sh remember")
    compose_up = workflow.index("up -d --remove-orphans")
    health = workflow.index("Verify public production health")
    prune = workflow.index("retain-images.sh prune")
    assert remember < compose_up < health < prune
    assert "docker image prune -a" not in workflow
    assert "docker system prune" not in workflow
```

- [ ] **Step 2: Run the workflow tests and verify RED**

Run:

```bash
python3 -m pytest tests/test_docker_deploy_layout.py -q
```

Expected: failures on old paths and missing retention phases.

- [ ] **Step 3: Update CI Compose validation**

Render:

```bash
docker compose --project-directory . \
  -f deploy/compose/production.yml config
docker compose --project-directory . \
  -f deploy/compose/production.yml \
  -f deploy/compose/production.local-build.yml config
docker compose \
  -f docker-compose.yml \
  -f deploy/compose/local.kafka-cluster.yml config
```

Use the existing CI environment plus the Grafana credentials.

- [ ] **Step 4: Update the deploy bundle and Compose commands**

The bundle no longer lists root production files. It includes:

```bash
tar -czf /tmp/tutorflow-deploy-bundle.tgz \
  deploy \
  migrations \
  docker/postgres
```

Every production invocation uses:

```bash
docker compose \
  --project-directory . \
  --env-file .env \
  -f deploy/compose/production.yml
```

- [ ] **Step 5: Add the pre-deploy remember phase**

Before pull/up:

```bash
deploy/scripts/retain-images.sh remember \
  --namespace "$IMAGE_NAMESPACE" \
  --state-file /opt/tutorflow/.image-retention-state
```

This must run after the bundle is extracted and before existing containers are
replaced.

- [ ] **Step 6: Move cleanup after public health**

Add a new SSH step after `Verify public production health`:

```bash
deploy/scripts/retain-images.sh prune \
  --namespace "$IMAGE_NAMESPACE" \
  --current-tag "$IMAGE_TAG" \
  --state-file /opt/tutorflow/.image-retention-state
```

Print `docker system df` and production Compose `ps` afterward. Remove the old
pre-health `docker image prune -f`.

- [ ] **Step 7: Run workflow validation**

Run:

```bash
python3 -m pytest tests/test_docker_deploy_layout.py -q
docker run --rm \
  --volume "$PWD:/repo:ro" \
  --workdir /repo \
  rhysd/actionlint:1.7.7@sha256:887a259a5a534f3c4f36cb02dca341673c6089431057242cdc931e9f133147e9
```

Expected: pytest and actionlint exit 0.

- [ ] **Step 8: Commit workflow wiring**

```bash
git add .github/workflows/ci.yml .github/workflows/deploy.yml tests/test_docker_deploy_layout.py
git commit -m "ci: deploy consolidated compose with image retention"
```

---

### Task 6: Update active documentation

**Files:**

- Modify: `README.md`
- Modify: `docs/deploy.md`
- Modify: `docs/adr/0003-service-replicas-and-kafka-scaling.md`
- Modify: `docs/roadmap.md`

**Interfaces:**

- Consumes: final paths and commands from Tasks 2 and 5.
- Produces: no active instructions pointing at removed root files.

- [ ] **Step 1: Replace live production commands**

Use:

```bash
docker compose \
  --project-directory . \
  --env-file .env \
  -f deploy/compose/production.yml
```

throughout active deploy, rollback, log, backup, and troubleshooting commands.

- [ ] **Step 2: Replace optional overlay references**

Use:

```text
deploy/compose/production.local-build.yml
deploy/compose/local.kafka-cluster.yml
```

and explain that production logging and monitoring are now built into
`production.yml`.

- [ ] **Step 3: Document retention state and recovery**

Document:

```text
/opt/tutorflow/.image-retention-state
```

and explain that local retention keeps two successful releases, while an older
tag can be pulled from GHCR for rollback. Explicitly retain warnings against
`docker compose down -v`, `docker system prune --volumes`, and manual volume
deletion.

- [ ] **Step 4: Verify no active old-path references remain**

Run:

```bash
rg -n \
  'docker-compose\\.prod|docker-compose\\.scale' \
  README.md docs/deploy.md docs/adr/0003-service-replicas-and-kafka-scaling.md \
  .github/workflows/ci.yml .github/workflows/deploy.yml
```

Expected: no matches.

- [ ] **Step 5: Run layout tests**

```bash
python3 -m pytest tests/test_docker_deploy_layout.py -q
```

Expected: pass.

- [ ] **Step 6: Commit documentation**

Force-add ignored but intentional tracked documentation paths without staging
other ignored docs:

```bash
git add README.md
git add -f docs/deploy.md docs/adr/0003-service-replicas-and-kafka-scaling.md docs/roadmap.md
git commit -m "docs: describe consolidated docker operations"
```

---

### Task 7: Full local verification and handoff

**Files:**

- Verify all task files.
- Do not modify unrelated dirty files.

**Interfaces:**

- Consumes: all previous tasks.
- Produces: evidence suitable for the existing Manual Tests and production deploy gates.

- [ ] **Step 1: Run focused static and retention tests**

```bash
python3 -m pytest \
  tests/test_docker_deploy_layout.py \
  tests/test_image_retention.py \
  tests/test_userver_toolchain.py \
  tests/test_file_s3_standardization.py \
  tests/test_realtime_redis_standardization.py -q
```

Expected: all selected tests pass.

- [ ] **Step 2: Collect the complete Python suite**

```bash
python3 -m pytest --collect-only -q tests
```

Expected: collection succeeds with no import errors.

- [ ] **Step 3: Render every Compose mode**

```bash
docker compose config >/tmp/tutorflow-local.yml
docker compose \
  -f docker-compose.yml \
  -f deploy/compose/local.kafka-cluster.yml \
  config >/tmp/tutorflow-local-kafka-cluster.yml
docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  config >/tmp/tutorflow-production.yml
docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  -f deploy/compose/production.local-build.yml \
  config >/tmp/tutorflow-production-local-build.yml
```

Expected: all commands exit 0.

- [ ] **Step 4: Re-run image runtime verification**

Run both builds and inspection loops from Task 3 with fresh output. Record:

```bash
docker image inspect \
  tutorflow/lesson-service:runtime-check \
  tutorflow/realtime-service:runtime-check \
  --format '{{index .RepoTags 0}} {{.Size}}'
```

Expected: no unresolved libraries, no build tools, no `/src`, and materially
smaller images.

- [ ] **Step 5: Validate workflow syntax and diff quality**

```bash
docker run --rm \
  --volume "$PWD:/repo:ro" \
  --workdir /repo \
  rhysd/actionlint:1.7.7@sha256:887a259a5a534f3c4f36cb02dca341673c6089431057242cdc931e9f133147e9
git diff --check
git status --short
```

Expected: actionlint and diff check exit 0; status contains unrelated user
changes plus only intentional task changes/commits.

- [ ] **Step 6: Review production rollout boundary**

Do not push, trigger GitHub Actions, or deploy without the required repository
workflow and user authorization. Report:

- exact commits created;
- old and new image sizes;
- verification commands and results;
- files intentionally not touched;
- that production still runs the previous image until the exact implementation
  commit passes Manual Tests and the deploy workflow is run.
