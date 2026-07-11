# TutorFlow README Architecture Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce an interviewer-friendly root README and ten developer-focused service READMEs that accurately explain the current TutorFlow architecture and implementation.

**Architecture:** The root README is a progressive-disclosure entry point: fast project overview first, complete architecture and operational detail later. Service READMEs link from the root and explain code-level ownership without duplicating canonical OpenAPI, protobuf, event-schema, finance, ADR, or deploy documentation.

**Tech Stack:** Markdown, Mermaid, C++20/userver repository sources, OpenAPI YAML, protobuf, SQL migrations, Docker Compose, Kubernetes/kustomize, Prometheus/Grafana.

## Global Constraints

- Preserve and integrate the user's existing uncommitted README additions.
- Change documentation only; do not modify code, contracts, migrations, infrastructure, or runtime behavior.
- Write in clear Russian for a technical interviewer who is also a developer.
- Keep every technical claim traceable to the current checkout.
- Mark destructive local-volume commands explicitly.
- Do not describe TutorFlow as a fully hardened commercial production system.
- Do not duplicate canonical contracts where a precise link is better.

---

### Task 1: Document the public boundary and foundational services

**Files:**
- Create: `services/api-gateway/README.md`
- Create: `services/identity-service/README.md`
- Create: `services/file-service/README.md`

**Interfaces:**
- Consumes: `docs/api-contracts/gateway.openapi.yaml`, `libs/proto/tutorflow/identity.proto`, gateway handler registrations, file HTTP handlers, service `main.cpp`, migrations, static configs.
- Produces: stable service-level links and descriptions used by the root README service map.

- [ ] **Step 1: Write the api-gateway README**

Describe its public REST role, JWT boundary, trusted context, CORS, thin-handler rule, gRPC clients, multipart file proxy, readiness, configuration, and verification links.

- [ ] **Step 2: Write the identity-service README**

Describe authentication, profiles and teacher-student relationships, PBKDF2/JWT behavior, canonical access check, owned tables, RPCs, lack of Kafka state, and verification links.

- [ ] **Step 3: Write the file-service README**

Describe metadata ownership, multipart flow, identity access checks, `IFileStorage`, local/S3 selection, MinIO/SigV4 behavior, readiness, size limits, and verification links.

- [ ] **Step 4: Verify facts and Markdown formatting**

Run:

```bash
rg -n '^service |^  rpc ' libs/proto/tutorflow/identity.proto
rg -n 'Append<.*Handler|Append<.*Client' services/api-gateway/src/main.cpp services/file-service/src/main.cpp
rg -n 'CREATE TABLE' migrations/identity migrations/file
git diff --check -- services/api-gateway/README.md services/identity-service/README.md services/file-service/README.md
```

Expected: documented RPCs, handlers, and tables match current sources; `git diff --check` prints nothing.

### Task 2: Document the core business services

**Files:**
- Create: `services/lesson-service/README.md`
- Create: `services/assignment-service/README.md`
- Create: `services/finance-service/README.md`

**Interfaces:**
- Consumes: lesson/assignment/finance protobufs, domain services, repositories, migrations, outbox/consumer code, `docs/EVENTS.md`, and `docs/FINANCE_MODEL.md`.
- Produces: service-level explanations for scheduling, homework, and append-only finance flows.

- [ ] **Step 1: Write the lesson-service README**

Explain availability, lesson lifecycle, access checks, state transitions, overlap prevention, materials as `file_id`, transactional outbox, produced lesson events, and the rule that lesson never creates charges directly.

- [ ] **Step 2: Write the assignment-service README**

Explain assignment/submission/review/comment flows, file references, deadline worker, statuses, access checks, outbox events, and idempotent deadline processing.

- [ ] **Step 3: Write the finance-service README**

Explain charge/payment/correction/refund ledger semantics, receipt workflow, balance formula, lesson-event consumer, inbox patterns, emitted finance events, access checks, and why report is not the money source of truth.

- [ ] **Step 4: Verify facts and Markdown formatting**

Run:

```bash
rg -n '^service |^  rpc ' libs/proto/tutorflow/{lesson,assignment,finance}.proto
rg -n 'CREATE TABLE|CREATE UNIQUE INDEX' migrations/{lesson,assignment,finance}
rg -n '^\| `?(lesson|assignment|submission|charge|payment|balance)' docs/EVENTS.md
git diff --check -- services/lesson-service/README.md services/assignment-service/README.md services/finance-service/README.md
```

Expected: methods, data ownership, event direction, and idempotency rules match current sources; formatting check prints nothing.

### Task 3: Document projections, chat, and realtime delivery

**Files:**
- Create: `services/notification-service/README.md`
- Create: `services/report-service/README.md`
- Create: `services/chat-service/README.md`
- Create: `services/realtime-service/README.md`

**Interfaces:**
- Consumes: notification/report/chat protobufs, event consumers and producers, migrations, Redis/WebSocket code, shard router, ADR 0002, and current runtime configs.
- Produces: service-level explanations for derived state, sharded chat storage, and push delivery.

- [ ] **Step 1: Write notification and report READMEs**

Explain that both are projections, list consumed events, describe inbox/upsert behavior, owned read models, query RPCs, and replay/rebuild implications.

- [ ] **Step 2: Write the chat-service README**

Explain dialogs/messages/read markers, access checks, deterministic UUIDv5 dialog IDs, FNV-1a shard routing, scatter-gather, per-shard outbox, Kafka keys, and current resharding limitations.

- [ ] **Step 3: Write the realtime-service README**

Explain `/ws?token=...`, JWT validation, in-process connections, Kafka consumption, Redis fan-out/presence, supported push event categories, reconnect behavior, and push-only boundaries.

- [ ] **Step 4: Verify facts and Markdown formatting**

Run:

```bash
rg -n '^service |^  rpc ' libs/proto/tutorflow/{notification,report,chat}.proto
rg -n 'ShardRouter|Uuid|FNV|ListDialogs' services/chat-service/src
rg -n 'message\.|notification\.created|presence|Redis|redis' services/realtime-service/src services/realtime-service/configs
git diff --check -- services/notification-service/README.md services/report-service/README.md services/chat-service/README.md services/realtime-service/README.md
```

Expected: projection ownership, shard mechanics, and realtime boundaries match code; formatting check prints nothing.

### Task 4: Rewrite the root README

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: all ten service READMEs, current user additions, canonical docs, Compose/Kubernetes/observability files, workflows, tests, and scripts.
- Produces: the main portfolio and developer entry point for TutorFlow.

- [ ] **Step 1: Write the fast overview and product scope**

Start with the product, implemented workflows, status, demo, stack, scale, a short data path, and a table of engineering techniques with purpose and trade-offs.

- [ ] **Step 2: Add architecture and service navigation**

Add a Mermaid architecture diagram, transport explanation, database boundary, linked service map, and concise role of every component.

- [ ] **Step 3: Add end-to-end flows and consistency model**

Trace authentication, lesson-to-charge, receipt confirmation, assignments, files, chat, notifications, and dashboards. Explain outbox/inbox, at-least-once delivery, ordering, idempotency, source of truth, and eventual consistency.

- [ ] **Step 4: Integrate scaling, operations, and demo scenarios**

Preserve and refine the user's Kafka failure, consumer lag, Kubernetes self-healing, sharding, outbox leader-lock, and observability content. State Compose/Kubernetes and production/demo limitations accurately.

- [ ] **Step 5: Add developer navigation and commands**

Document repository layout, service code path, local startup, migrations, tests, frontend, observability, scaled Kafka, kind, deployment summary, and canonical source links.

- [ ] **Step 6: Check root README formatting**

Run:

```bash
git diff --check -- README.md
rg -n '^#{1,4} ' README.md
```

Expected: no whitespace errors and a coherent heading hierarchy.

### Task 5: Validate the complete documentation set

**Files:**
- Verify: `README.md`
- Verify: `services/*/README.md`
- Verify: `docs/superpowers/specs/2026-07-11-readme-architecture-documentation-design.md`

**Interfaces:**
- Consumes: the complete documentation change and all linked repository paths.
- Produces: evidence that the documentation is internally consistent and repository-grounded.

- [ ] **Step 1: Validate relative Markdown links**

Run a local script that extracts non-HTTP Markdown links from the eleven README files, resolves them relative to each document, ignores heading anchors, and fails if a target does not exist.

Expected: zero missing local targets.

- [ ] **Step 2: Validate documented inventory**

Run:

```bash
test "$(find services -mindepth 2 -maxdepth 2 -name README.md | wc -l | tr -d ' ')" = 10
python3 -m pytest --collect-only -q tests
cd frontend && npm run build
```

Expected: ten service READMEs, pytest collection succeeds, and the frontend production build succeeds.

- [ ] **Step 3: Run repository documentation checks**

Run:

```bash
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors; only the intended documentation changes plus the user's pre-existing README work are present.

- [ ] **Step 4: Review claims and limitations manually**

Confirm the README does not claim real payments, email/Telegram/push delivery, Google Calendar, production Kubernetes, automatic read-model rebuild, online resharding, or fully hardened production readiness.

- [ ] **Step 5: Commit the completed documentation**

Run:

```bash
git add README.md services/*/README.md docs/superpowers/plans/2026-07-11-readme-architecture-documentation.md
git commit -m "docs: expand TutorFlow architecture guide"
```

Expected: one scoped documentation commit; no application or infrastructure files staged.
