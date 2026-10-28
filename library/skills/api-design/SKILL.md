---
name: api-design
description: Design or review an HTTP/REST API, including resource names, methods, status codes, error format, pagination, versioning, auth and an OpenAPI sketch. Use when adding endpoints or reviewing an API's shape.
---

# API design

## Steps

1. **Start from the use cases.** List who calls the API and the three to five things they need to do. Design for those, not for the database tables.
2. **Resources and methods.** Plural nouns (`/orders`, `/orders/{id}/items`). `GET` reads (safe, cacheable), `POST` creates, `PUT` replaces, `PATCH` partially updates, `DELETE` removes. Keep actions that are not CRUD as sub-resources (`POST /orders/{id}/cancel`).
3. **Status codes.** 200/201 (with `Location`)/204, 400 validation, 401 not signed in, 403 not allowed, 404 not found, 409 conflict, 422 semantic error if the project uses it, 429 rate limited, 5xx only for server faults.
4. **One error format** everywhere, for example `{"error": {"code": "order_not_found", "message": "...", "details": [...]}}` or RFC 9457 problem details.
5. **Collections.** Cursor pagination for large or changing lists (`?limit=50&cursor=...` returning `next_cursor`), filtering and sorting with explicit allowed fields.
6. **Safety.** Idempotency keys for create and payment calls, optimistic concurrency (`ETag`/`If-Match`) for updates, rate limits, and authorization on every object.
7. **Evolution.** Additive changes only within a version; a version prefix or header for breaking changes; deprecation notes with dates.
8. **Write an OpenAPI 3.1 sketch** for the endpoints discussed, with example requests and responses.

## Rules

- Follow the conventions the existing API already uses (casing, envelope, auth) unless the user wants to change them.
- Never expose internal IDs, stack traces or other users' data in errors.
