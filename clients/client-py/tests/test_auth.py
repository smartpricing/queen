"""
Tests for server-stamped producer identity (issue #23, feature A).

Same semantics as client-js/test-v2/auth.js:

  - SP-level tests (always run, talk to Postgres directly) verify the schema +
    stored-procedure contract independently of the HTTP layer.
  - HTTP-level tests exercise the anti-impersonation invariant when JWT auth
    is enabled on the server.

Tests gate themselves on the ``JWT_SECRET`` env var so this suite can run
against a server in either configuration. Run with a JWT-enabled server as:

    JWT_SECRET=<server-secret> python -m pytest clients/client-py/tests/test_auth.py
"""

import asyncio
import base64
import hashlib
import hmac
import json
import os
import time
from typing import Any, Dict, List, Optional

import asyncpg
import httpx
import pytest

from queen import Queen


SERVER_URL = os.environ.get("QUEEN_URL", "http://localhost:6632")
JWT_SECRET = os.environ.get("JWT_SECRET", "")
JWT_ENABLED = len(JWT_SECRET) > 0


# ---------------------------------------------------------------------------
# Minimal HS256 JWT signer (avoids pulling a dependency just for tests).
# ---------------------------------------------------------------------------
def _b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def _sign_hs256_jwt(payload: Dict[str, Any], secret: str) -> str:
    header = {"alg": "HS256", "typ": "JWT"}
    enc_header = _b64url(json.dumps(header, separators=(",", ":")).encode())
    enc_payload = _b64url(json.dumps(payload, separators=(",", ":")).encode())
    signing_input = f"{enc_header}.{enc_payload}".encode()
    sig = hmac.new(secret.encode(), signing_input, hashlib.sha256).digest()
    return f"{enc_header}.{enc_payload}.{_b64url(sig)}"


def _make_token(sub: str, role: str = "read-write") -> str:
    now = int(time.time())
    return _sign_hs256_jwt(
        {"sub": sub, "username": sub, "role": role, "iat": now, "exp": now + 3600},
        JWT_SECRET,
    )


# ---------------------------------------------------------------------------
# DB helpers
# ---------------------------------------------------------------------------
async def _get_stored_producer_sub(
    pool: asyncpg.Pool, queue_name: str, transaction_id: str
) -> Optional[str]:
    row = await pool.fetchrow(
        """SELECT m.producer_sub
           FROM queen.messages m
           JOIN queen.partitions p ON p.id = m.partition_id
           JOIN queen.queues q ON q.id = p.queue_id
           WHERE q.name = $1 AND m.transaction_id = $2""",
        queue_name,
        transaction_id,
    )
    return row["producer_sub"] if row else None


async def _call_push_messages_v2(pool: asyncpg.Pool, items: List[Dict[str, Any]]) -> None:
    await pool.execute(
        "SELECT queen.push_messages_v2($1::jsonb)", json.dumps(items)
    )


async def _call_pop_specific_batch(
    pool: asyncpg.Pool, queue_name: str
) -> List[Dict[str, Any]]:
    reqs = [{
        "idx": 0,
        "queue_name": queue_name,
        "partition_name": "Default",
        "consumer_group": "__QUEUE_MODE__",
        "lease_seconds": 60,
        "worker_id": f"test-worker-{int(time.time() * 1000)}",
        "batch_size": 10,
        "sub_mode": "all",
        "sub_from": "",
    }]
    raw = await pool.fetchval(
        "SELECT queen.pop_specific_batch($1::jsonb)", json.dumps(reqs)
    )
    out = json.loads(raw) if isinstance(raw, str) else raw
    return (out[0] or {}).get("result", {}).get("messages", [])


async def _http_push(
    queue: str,
    transaction_id: str,
    data: Dict[str, Any],
    bearer_token: Optional[str] = None,
    extra_item_fields: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    headers = {"Content-Type": "application/json"}
    if bearer_token:
        headers["Authorization"] = f"Bearer {bearer_token}"

    item = {
        "queue": queue,
        "partition": "Default",
        "transactionId": transaction_id,
        "payload": data,
    }
    if extra_item_fields:
        item.update(extra_item_fields)

    async with httpx.AsyncClient(timeout=10.0) as http:
        res = await http.post(
            f"{SERVER_URL}/api/v1/push",
            headers=headers,
            json={"items": [item]},
        )
    res.raise_for_status()
    return res.json()


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------
@pytest.fixture
async def pool():
    p = await asyncpg.create_pool(
        host=os.environ.get("PG_HOST", "localhost"),
        port=int(os.environ.get("PG_PORT", 5432)),
        database=os.environ.get("PG_DB", "postgres"),
        user=os.environ.get("PG_USER", "postgres"),
        password=os.environ.get("PG_PASSWORD", "postgres"),
    )
    try:
        yield p
    finally:
        await p.close()


# ===========================================================================
# SP-LEVEL TESTS (always run, independent of HTTP / JWT)
# ===========================================================================
@pytest.mark.asyncio
async def test_producer_sub_round_trip_via_stored_procedure(pool):
    """producer_sub persisted through push_messages_v2 and surfaced by pop."""
    q = "test-auth-sp-roundtrip-py"
    tx = f"tx-sp-py-{int(time.time() * 1000)}"

    await _call_push_messages_v2(pool, [{
        "queue": q,
        "partition": "Default",
        "transactionId": tx,
        "payload": {"x": 1},
        "producerSub": "sp-test-sub",
    }])

    stored = await _get_stored_producer_sub(pool, q, tx)
    assert stored == "sp-test-sub", f"expected 'sp-test-sub', got {stored!r}"

    popped = await _call_pop_specific_batch(pool, q)
    target = next((m for m in popped if m.get("transactionId") == tx), None)
    assert target is not None, f"did not find tx {tx} in pop result"
    assert target["producerSub"] == "sp-test-sub"


@pytest.mark.asyncio
async def test_producer_sub_null_when_not_provided(pool):
    """Omitting producerSub yields NULL in DB and null in pop response."""
    q = "test-auth-sp-null-py"
    tx = f"tx-sp-null-py-{int(time.time() * 1000)}"

    await _call_push_messages_v2(pool, [{
        "queue": q,
        "partition": "Default",
        "transactionId": tx,
        "payload": {"x": 1},
    }])

    stored = await _get_stored_producer_sub(pool, q, tx)
    assert stored is None, f"expected NULL, got {stored!r}"

    popped = await _call_pop_specific_batch(pool, q)
    target = next((m for m in popped if m.get("transactionId") == tx), None)
    assert target is not None
    assert target.get("producerSub") is None


@pytest.mark.asyncio
async def test_producer_sub_empty_string_stored_as_null(pool):
    """Empty-string producerSub must be stored as NULL (NULLIF invariant)."""
    q = "test-auth-sp-empty-py"
    tx = f"tx-sp-empty-py-{int(time.time() * 1000)}"

    await _call_push_messages_v2(pool, [{
        "queue": q,
        "partition": "Default",
        "transactionId": tx,
        "payload": {"x": 1},
        "producerSub": "",
    }])

    stored = await _get_stored_producer_sub(pool, q, tx)
    assert stored is None, f"expected NULL for empty-string input, got {stored!r}"


# ===========================================================================
# HTTP-LEVEL TESTS (require a running Queen server)
# ===========================================================================
@pytest.mark.asyncio
@pytest.mark.skipif(JWT_ENABLED, reason="JWT_SECRET set - see HTTP-JWT test")
async def test_producer_sub_ignored_from_body_without_auth(pool):
    """Body-supplied producerSub must be ignored when auth is disabled."""
    q = "test-auth-http-no-jwt-py"
    tx = f"tx-noauth-py-{int(time.time() * 1000)}"

    client = Queen(SERVER_URL)
    try:
        await client.queue(q).create()
    finally:
        await client.close()

    await _http_push(
        queue=q,
        transaction_id=tx,
        data={"hello": "world"},
        extra_item_fields={"producerSub": "attacker-no-jwt"},
    )

    stored = await _get_stored_producer_sub(pool, q, tx)
    assert stored is None, (
        f"expected NULL (auth disabled), got {stored!r} - client was able to set it!"
    )


@pytest.mark.asyncio
@pytest.mark.skipif(not JWT_ENABLED, reason="set JWT_SECRET to run (server must have JWT enabled)")
async def test_producer_sub_stamped_from_jwt(pool):
    """producer_sub must equal the validated JWT sub claim."""
    q = "test-auth-http-jwt-stamp-py"
    tx = f"tx-jwt-py-{int(time.time() * 1000)}"
    token = _make_token("alice-producer")

    # Queue creation requires auth too when JWT is enabled.
    async with httpx.AsyncClient(timeout=10.0) as http:
        await http.post(
            f"{SERVER_URL}/api/v1/configure",
            headers={
                "Content-Type": "application/json",
                "Authorization": f"Bearer {token}",
            },
            json={"queue": q, "options": {}},
        )

    await _http_push(
        queue=q,
        transaction_id=tx,
        data={"hello": "world"},
        bearer_token=token,
    )

    stored = await _get_stored_producer_sub(pool, q, tx)
    assert stored == "alice-producer"


@pytest.mark.asyncio
@pytest.mark.skipif(not JWT_ENABLED, reason="set JWT_SECRET to run (server must have JWT enabled)")
async def test_producer_sub_spoofing_ignored_with_jwt(pool):
    """Body-supplied producerSub must be ignored even with a valid JWT."""
    q = "test-auth-http-jwt-spoof-py"
    tx = f"tx-spoof-py-{int(time.time() * 1000)}"
    token = _make_token("legit-producer")

    async with httpx.AsyncClient(timeout=10.0) as http:
        await http.post(
            f"{SERVER_URL}/api/v1/configure",
            headers={
                "Content-Type": "application/json",
                "Authorization": f"Bearer {token}",
            },
            json={"queue": q, "options": {}},
        )

    await _http_push(
        queue=q,
        transaction_id=tx,
        data={"hello": "world"},
        bearer_token=token,
        extra_item_fields={"producerSub": "attacker"},
    )

    stored = await _get_stored_producer_sub(pool, q, tx)
    assert stored == "legit-producer", (
        f"impersonation not prevented: stored={stored!r}, expected 'legit-producer'"
    )
