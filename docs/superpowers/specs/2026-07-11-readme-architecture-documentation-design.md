# TutorFlow README and Service Documentation Design

**Date:** 2026-07-11

**Status:** approved in conversation

## Goal

Rewrite TutorFlow documentation so a technical interviewer can understand the
product, scale, architecture, and strongest engineering decisions in the first
two or three minutes, while a developer can continue reading and verify how the
system is implemented.

The documentation change consists of one detailed root `README.md` and one
focused README for each backend service.

## Audience

The primary reader is a technical interviewer or employer who is also a
developer. The text must therefore combine two qualities:

- a fast portfolio-level explanation of what was built and why it matters;
- enough implementation detail, links, flows, commands, and limitations to
  make the claims technically verifiable.

The language is Russian, simple where possible, and precise where technical
terms are necessary. Each important term is explained when it first appears.

## Documentation Set

### Root README

`README.md` is the entry point and the complete architecture overview. It must
answer, in order:

1. What TutorFlow is and which user problems it solves.
2. What is implemented now and what is intentionally outside the project.
3. Which technologies and architectural patterns are demonstrated.
4. Which services exist and how they communicate.
5. How the main business flows travel through the system.
6. How consistency, idempotency, scaling, observability, and deployment work.
7. How to run, inspect, and test the project.
8. Where to continue reading for contracts and service-level details.

The root README links every backend service name to that service's README.

### Service READMEs

Create these files:

- `services/api-gateway/README.md`
- `services/identity-service/README.md`
- `services/lesson-service/README.md`
- `services/assignment-service/README.md`
- `services/finance-service/README.md`
- `services/file-service/README.md`
- `services/notification-service/README.md`
- `services/report-service/README.md`
- `services/chat-service/README.md`
- `services/realtime-service/README.md`

Each service README is intentionally smaller than the root README. It should
let a developer understand the service before opening implementation files.
All service READMEs use the same basic structure, omitting sections that do not
apply:

1. Purpose and source-of-truth responsibility.
2. What the service does and explicitly does not do.
3. Incoming API or protocol.
4. Outgoing dependencies.
5. Internal component and code layout.
6. Database ownership and important tables, or stateless state ownership.
7. Produced and consumed Kafka events.
8. One or two important end-to-end flows.
9. Idempotency, access-control, or consistency rules relevant to the service.
10. Configuration and runtime dependencies.
11. How to build, run, inspect, and test the service.
12. Links back to the root README and canonical contracts.

The service READMEs must not invent public APIs. REST facts come from the
gateway OpenAPI contract and gateway handlers, internal synchronous APIs come
from protobuf definitions, events come from `docs/EVENTS.md` and
`docs/event-contracts/`, and data ownership comes from migrations plus current
repository code.

## Root README Structure

### Fast Overview

The first screen contains:

- a short product description;
- current implementation status;
- production demo link;
- stack and component count;
- a compact request/event path;
- a table of the strongest engineering decisions and why each exists.

The wording must distinguish a complete educational/demo MVP from a hardened
commercial production platform.

### Product Capabilities

Describe teacher and student workflows in plain language. Explicitly state that
real payment processing, email, Telegram, mobile push, and Google Calendar are
not implemented.

### Architecture and Communication

Include a Mermaid component diagram and a compact fallback text description.
Explain REST, HTTP multipart, gRPC, Kafka, WebSocket, and Redis by purpose, not
just by technology name.

The diagram must preserve these boundaries:

- external domain commands enter through `api-gateway`;
- `realtime-service` is a public push-only WebSocket endpoint;
- internal synchronous calls use gRPC, except file multipart traffic;
- each stateful service owns its database;
- Kafka carries facts that already happened, not commands or access checks.

### Service Map and Details

Provide one compact service table with links to service READMEs, then concise
root-level explanations of every service. Root descriptions focus on the role
in the whole system; service READMEs contain code-level detail.

### End-to-End Flows

Trace at least these flows:

- authentication and trusted user context;
- lesson completion to charge, dashboard, notification, and realtime push;
- receipt upload and manual confirmation;
- assignment creation, submission, and review;
- file upload/download;
- chat message to notification and WebSocket delivery.

### Cross-Cutting Architecture

Explain:

- database-per-service boundaries;
- transactional outbox and consumer inbox;
- at-least-once delivery and idempotency;
- aggregate Kafka keys and per-partition ordering;
- source-of-truth services versus derived read models;
- append-only finance ledger;
- local/S3 file-storage abstraction;
- two-shard chat routing, deterministic dialog UUID, and scatter-gather;
- liveness/readiness separation;
- service replicas, Kafka consumer groups, and outbox leader lock;
- Prometheus/Grafana observability;
- Compose production deployment and local kind/Kubernetes deployment.

Every advanced technique must include its trade-off or current limitation.

### Developer Guide

Include repository layout, common service code path, environment preparation,
Compose startup, frontend startup, migrations, smoke tests, pytest collection
and execution, frontend build, observability profile, Kafka scaling overlay,
kind startup, and production compose validation.

Commands that delete data, especially `docker compose down -v`, must be marked
as destructive for local volumes.

### Sources of Truth

Link the root README to:

- `docs/PLAN.md` for domain rules;
- `docs/api-contracts/gateway.openapi.yaml` for public REST;
- `libs/proto/tutorflow/*.proto` for internal gRPC;
- `docs/EVENTS.md` and `docs/event-contracts/` for Kafka;
- `docs/FINANCE_MODEL.md` for finance;
- `docs/adr/` for decisions;
- `docs/deploy.md` for operations;
- `tests/` and `scripts/smoke_mvp.py` for executable behavior.

## Existing User Changes

The current uncommitted README additions about architecture demonstrations,
Kafka broker failure, consumer lag, Kubernetes self-healing, chat sharding, and
outbox leader locking are user-owned content. Preserve their intent, verify the
claims against the current branch, and integrate them into the new hierarchy.
Do not discard the additions by replacing the file with an older revision.

## Verification

Before completion:

1. Confirm every service link resolves to an existing README.
2. Check every referenced repository path exists.
3. Compare service RPC names against `libs/proto/tutorflow/*.proto`.
4. Compare REST paths against `docs/api-contracts/gateway.openapi.yaml` and
   gateway registrations.
5. Compare event claims against `docs/EVENTS.md`, contracts, producers, and
   consumers.
6. Compare database claims against migrations and repository code.
7. Validate all relative Markdown links.
8. Run formatting checks for trailing whitespace and broken fenced blocks.
9. Run lightweight project verification that does not require rebuilding the
   full Docker stack; report separately what was and was not runtime-tested.

## Out of Scope

- No application code, API contract, migration, infrastructure, or domain
  behavior changes.
- No new features or dependencies.
- No attempt to turn the README into a replacement for OpenAPI, protobuf, event
  schemas, ADRs, or the deploy runbook.
- No claims that the current demo is a fully hardened production system.
