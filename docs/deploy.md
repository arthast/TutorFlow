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
