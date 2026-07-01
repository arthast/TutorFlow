# TutorFlow Deploy

## Target

- Server: `51.250.117.185`
- User: `ilyaytrewq`
- Domain: `netwatch-arsen-demo.ru`
- `www.netwatch-arsen-demo.ru` redirects to `netwatch-arsen-demo.ru`
- App root on server: `/opt/tutorflow`

Frontend and backend are served from one domain. Caddy routes:

- `/ws*` -> `realtime-service:8089`
- API prefixes (`/auth`, `/students`, `/lessons`, `/assignments`, `/payments`,
  `/notifications`, `/dashboard`, `/files`, `/chats`, `/health`) -> `api-gateway:8080`
- everything else -> frontend SPA

WebSocket is only a push channel for chat, read markers, presence, and
notifications. Domain commands still go through REST -> gateway.

## Server bootstrap

Docker and swap are required before deployment:

```bash
sudo fallocate -l 6G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab

sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
  | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg
. /etc/os-release
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu ${VERSION_CODENAME} stable" \
  | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo usermod -aG docker "$USER"
sudo systemctl enable --now docker
```

## First manual deploy

Create `/opt/tutorflow/.env` from `deploy/.env.prod.example` and replace all
`change_me_*` values.

Until DNS is pointed to the server, Caddy cannot issue a public TLS certificate.
For the final HTTPS deploy, create this DNS record:

```text
A netwatch-arsen-demo.ru -> 51.250.117.185
A www.netwatch-arsen-demo.ru -> 51.250.117.185
```

Copy deploy files to `/opt/tutorflow`:

```bash
mkdir -p /opt/tutorflow
cp docker-compose.prod.yml /opt/tutorflow/
cp -R deploy /opt/tutorflow/
cp -R migrations /opt/tutorflow/
cp -R docker/postgres /opt/tutorflow/docker/postgres
```

Run using already published images:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml pull
docker compose --env-file .env -f docker-compose.prod.yml up -d
docker compose --env-file .env -f docker-compose.prod.yml ps
curl -fsS https://netwatch-arsen-demo.ru/health
```

Local-build fallback, useful before GHCR images exist:

```bash
docker compose --env-file .env \
  -f docker-compose.prod.yml \
  -f docker-compose.prod.local-build.yml \
  build
docker compose --env-file .env \
  -f docker-compose.prod.yml \
  -f docker-compose.prod.local-build.yml \
  up -d
```

## CI/CD

`GITHUB_TOKEN` is enough for GHCR if the workflow has `packages: write`.
Use a separate `GHCR_TOKEN` only if GitHub package permissions block the built-in
token.

Required GitHub secrets:

```text
DEPLOY_HOST=51.250.117.185
DEPLOY_USER=ilyaytrewq
DEPLOY_SSH_KEY=<private ssh key>
```

Optional:

```text
GHCR_TOKEN=<PAT with packages read/write>
```

## Production deploy runbook

This is the normal production deploy path after server bootstrap is done.

Current production facts:

- Server: `51.250.117.185`
- SSH user: `ilyaytrewq`
- App root: `/opt/tutorflow`
- Public URL: `https://netwatch-arsen-demo.ru`
- Compose project: `tutorflow-prod`
- Compose file: `/opt/tutorflow/docker-compose.prod.yml`
- Runtime env file: `/opt/tutorflow/.env`
- Image namespace: `ghcr.io/arthast`
- Image tag: commit SHA by default; `latest` is also published but should not be
  used for controlled rollback.

Normal deploy is started from GitHub Actions:

1. Open the `Deploy` workflow.
2. Run it manually. Leave `image_tag` empty to deploy the current commit SHA, or
   pass an explicit commit SHA to redeploy a known image.
3. Wait for `Build and push images` and `Deploy to server` to finish.
4. Run the post-deploy checks below.

The workflow does three server-side operations:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml pull
docker compose --env-file .env -f docker-compose.prod.yml up -d --remove-orphans
docker compose --env-file .env -f docker-compose.prod.yml ps
```

With the actual workflow, `IMAGE_TAG` and `TUTORFLOW_IMAGE_NAMESPACE` are passed
as environment variables for the `pull` and `up` commands.

## Post-deploy checks

Run these checks after every production deploy.

From the server:

```bash
ssh -l ilyaytrewq 51.250.117.185
cd /opt/tutorflow

docker compose --env-file .env -f docker-compose.prod.yml ps
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=80 caddy
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=80 api-gateway
```

From the local machine:

```bash
curl -fsS https://netwatch-arsen-demo.ru/health
GATEWAY_URL=https://netwatch-arsen-demo.ru python3 scripts/smoke_mvp.py
```

Expected health response:

```json
{"status":"ok"}
```

For WebSocket, use any local WebSocket client with a fresh JWT:

```text
wss://netwatch-arsen-demo.ru/ws?token=<access_token>
```

Send:

```json
{"type":"ping"}
```

Expected response:

```json
{"type":"pong","payload":null}
```

If `health` is green but smoke fails, inspect the domain service logs first:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 identity-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 lesson-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 assignment-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 finance-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 file-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 notification-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 report-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 chat-service
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 realtime-service
```

## Rollback runbook

Rollback means starting the production compose stack with the previous known-good
image tag. In this project, the tag is normally a Git commit SHA published to
GHCR by the `Deploy` workflow.

Use rollback when a new deploy causes one of these symptoms:

- `https://netwatch-arsen-demo.ru/health` fails;
- one or more app containers are restarting;
- smoke test fails after deploy;
- WebSocket no longer opens or no longer answers ping/pong;
- the frontend is served but API calls fail because gateway/domain services are
  broken.

Do not use container rollback as the only recovery step when the deploy applied
non-backward-compatible database migrations or corrupted data. Container rollback
does not restore PostgreSQL, Kafka, Redis, MinIO, or Caddy volumes. Use the
backup/restore runbook for data recovery.

### 1. Find the previous image tag

Preferred source: the previous successful GitHub Actions `Deploy` run commit SHA.

On the server, the currently running images can be inspected with:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml ps
docker compose --env-file .env -f docker-compose.prod.yml images
```

If the previous deploy was successful, record its commit SHA as:

```bash
PREVIOUS_TAG=<previous_successful_commit_sha>
```

### 2. Pull and start the previous tag

Run on the server:

```bash
cd /opt/tutorflow

PREVIOUS_TAG=<previous_successful_commit_sha>

IMAGE_TAG="$PREVIOUS_TAG" \
TUTORFLOW_IMAGE_NAMESPACE=ghcr.io/arthast \
docker compose --env-file .env -f docker-compose.prod.yml pull

IMAGE_TAG="$PREVIOUS_TAG" \
TUTORFLOW_IMAGE_NAMESPACE=ghcr.io/arthast \
docker compose --env-file .env -f docker-compose.prod.yml up -d --remove-orphans
```

### 3. Verify rollback

Run:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml ps
curl -fsS https://netwatch-arsen-demo.ru/health
```

Then from the local repo:

```bash
GATEWAY_URL=https://netwatch-arsen-demo.ru python3 scripts/smoke_mvp.py
```

If rollback is successful, leave the broken deploy tag unused and fix forward in
a new commit. Do not redeploy `latest` blindly; deploy a concrete SHA.

## Backup/restore runbook

This runbook covers production data owned by TutorFlow:

- PostgreSQL databases: `identity_db`, `lesson_db`, `assignment_db`,
  `finance_db`, `file_db`, `notification_db`, `report_db`, `chat_db`;
- MinIO bucket: `${FILE_S3_BUCKET:-tutorflow-files}` with uploaded files,
  receipts, lesson materials, and chat attachments.

PostgreSQL and MinIO should be backed up together under the same timestamp.
`file_db` stores file metadata, while MinIO stores object bytes. Restoring only
one side can leave dangling file records or orphaned objects.

This runbook does not back up Kafka, Redis, Caddy certificates, or Docker image
cache. Kafka/Redis are treated as runtime infrastructure for the current demo
deployment; Caddy can re-issue certificates from DNS and the Caddy volume if
needed.

### 1. Create a backup

Run on the server:

```bash
ssh -l ilyaytrewq 51.250.117.185
cd /opt/tutorflow

TS="$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_ROOT="/opt/tutorflow/backups/$TS"
mkdir -p "$BACKUP_ROOT/postgres" "$BACKUP_ROOT/minio"

DBS="identity_db lesson_db assignment_db finance_db file_db notification_db report_db chat_db"

for db in $DBS; do
  docker compose --env-file .env -f docker-compose.prod.yml exec -T postgres \
    sh -ec 'PGPASSWORD="$POSTGRES_PASSWORD" pg_dump -U "$POSTGRES_USER" -d "$1" -Fc' \
    sh "$db" > "$BACKUP_ROOT/postgres/$db.dump"
done
```

Back up MinIO using a temporary `minio/mc` container on the production compose
network:

```bash
cd /opt/tutorflow

NETWORK="${COMPOSE_PROJECT_NAME:-tutorflow-prod}_tutorflow"
BUCKET="$(grep '^FILE_S3_BUCKET=' .env | cut -d= -f2-)"
BUCKET="${BUCKET:-tutorflow-files}"

docker run --rm \
  --network "$NETWORK" \
  --env-file .env \
  -e FILE_S3_BUCKET="$BUCKET" \
  -v "$BACKUP_ROOT/minio:/backup" \
  minio/mc:latest \
  sh -ec 'mc alias set prod http://minio:9000 "$MINIO_ROOT_USER" "$MINIO_ROOT_PASSWORD" &&
          mc mirror "prod/$FILE_S3_BUCKET" "/backup/$FILE_S3_BUCKET"'
```

Create checksums and a single archive:

```bash
cd "$BACKUP_ROOT"
find . -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS

cd /opt/tutorflow/backups
tar -czf "$TS.tgz" "$TS"
sha256sum "$TS.tgz" > "$TS.tgz.sha256"
```

Copy the archive off the server. A backup stored only on the same VM does not
protect against disk loss:

```bash
scp ilyaytrewq@51.250.117.185:/opt/tutorflow/backups/$TS.tgz .
scp ilyaytrewq@51.250.117.185:/opt/tutorflow/backups/$TS.tgz.sha256 .
```

### 2. Restore PostgreSQL

Use restore only after choosing the exact backup timestamp. Prefer restoring all
application databases from the same backup set.

Stop app services that can write to PostgreSQL, but keep infrastructure running:

```bash
cd /opt/tutorflow

docker compose --env-file .env -f docker-compose.prod.yml stop \
  api-gateway \
  identity-service \
  lesson-service \
  assignment-service \
  finance-service \
  file-service \
  notification-service \
  report-service \
  chat-service \
  realtime-service
```

Restore one database:

```bash
cd /opt/tutorflow

BACKUP_ROOT="/opt/tutorflow/backups/<timestamp>"
DB="finance_db"

docker compose --env-file .env -f docker-compose.prod.yml exec -T postgres \
  sh -ec 'dropdb --if-exists -U "$POSTGRES_USER" "$1" &&
          createdb -U "$POSTGRES_USER" "$1"' \
  sh "$DB"

docker compose --env-file .env -f docker-compose.prod.yml exec -T postgres \
  sh -ec 'PGPASSWORD="$POSTGRES_PASSWORD" pg_restore -U "$POSTGRES_USER" -d "$1" --no-owner' \
  sh "$DB" < "$BACKUP_ROOT/postgres/$DB.dump"
```

Restore all application databases:

```bash
cd /opt/tutorflow

BACKUP_ROOT="/opt/tutorflow/backups/<timestamp>"
DBS="identity_db lesson_db assignment_db finance_db file_db notification_db report_db chat_db"

for db in $DBS; do
  docker compose --env-file .env -f docker-compose.prod.yml exec -T postgres \
    sh -ec 'dropdb --if-exists -U "$POSTGRES_USER" "$1" &&
            createdb -U "$POSTGRES_USER" "$1"' \
    sh "$db"

  docker compose --env-file .env -f docker-compose.prod.yml exec -T postgres \
    sh -ec 'PGPASSWORD="$POSTGRES_PASSWORD" pg_restore -U "$POSTGRES_USER" -d "$1" --no-owner' \
    sh "$db" < "$BACKUP_ROOT/postgres/$db.dump"
done
```

### 3. Restore MinIO

Restore MinIO from the same timestamp as `file_db`:

```bash
cd /opt/tutorflow

BACKUP_ROOT="/opt/tutorflow/backups/<timestamp>"
NETWORK="${COMPOSE_PROJECT_NAME:-tutorflow-prod}_tutorflow"
BUCKET="$(grep '^FILE_S3_BUCKET=' .env | cut -d= -f2-)"
BUCKET="${BUCKET:-tutorflow-files}"

docker run --rm \
  --network "$NETWORK" \
  --env-file .env \
  -e FILE_S3_BUCKET="$BUCKET" \
  -v "$BACKUP_ROOT/minio:/backup:ro" \
  minio/mc:latest \
  sh -ec 'mc alias set prod http://minio:9000 "$MINIO_ROOT_USER" "$MINIO_ROOT_PASSWORD" &&
          mc mb --ignore-existing "prod/$FILE_S3_BUCKET" &&
          mc mirror "/backup/$FILE_S3_BUCKET" "prod/$FILE_S3_BUCKET"'
```

The command above adds or overwrites objects from backup. It does not delete
objects that exist in production but are absent in backup. For a destructive
exact restore, add `--remove` to `mc mirror` only after confirming that deleting
newer production objects is intended.

### 4. Start services and verify restore

Start the stack:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml up -d
docker compose --env-file .env -f docker-compose.prod.yml ps
```

Run checks:

```bash
curl -fsS https://netwatch-arsen-demo.ru/health
GATEWAY_URL=https://netwatch-arsen-demo.ru python3 scripts/smoke_mvp.py
```

Also verify one existing uploaded file through the public gateway:

```bash
curl -fSL \
  -H "Authorization: Bearer <access_token>" \
  "https://netwatch-arsen-demo.ru/files/<file_id>/download" \
  -o /tmp/tutorflow-restore-file-check
```

### 5. Safety rules

- Do not run `docker compose down -v` on production unless the goal is to delete
  production data.
- Do not remove Docker volumes manually during routine backup or restore.
- Do not restore `file_db` without restoring the matching MinIO backup when file
  consistency matters.
- Do not restore a newer app image onto older data without checking migrations
  and compatibility.

## Troubleshooting

Check container state:

```bash
cd /opt/tutorflow
docker compose --env-file .env -f docker-compose.prod.yml ps
```

Check Caddy routing/TLS:

```bash
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 caddy
curl -I https://netwatch-arsen-demo.ru
curl -I https://www.netwatch-arsen-demo.ru
```

Check gateway:

```bash
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 api-gateway
curl -fsS https://netwatch-arsen-demo.ru/health
```

Check infra dependencies:

```bash
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 postgres
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 kafka
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 redis
docker compose --env-file .env -f docker-compose.prod.yml logs --tail=120 minio
```

Check disk and memory pressure:

```bash
df -h
free -h
docker system df
```

Free unused dangling images only:

```bash
docker image prune -f
```

Do not run destructive cleanup commands such as `docker compose down -v`,
`docker volume rm`, or broad `docker system prune --volumes` on production unless
the goal is to intentionally delete production data.
