#!/usr/bin/env python3
"""RPC Load Generator for rippled telemetry validation.

Connects to one or more rippled WebSocket endpoints and fires all traced
RPC commands at configurable rates with realistic production-like
distribution.

Command distribution (default weights, summing to 100):
  40%  Health checks:   server_info, fee
  30%  Wallet queries:  account_info, account_lines, account_objects
  15%  Explorer:        ledger, ledger_data
  10%  TX lookups:      tx, account_tx
   5%  DEX queries:     book_offers, amm_info

Path-finding RPC is deliberately absent — see "Pathfinding is not exercised"
in README.md for why, and for how to put it back.

Usage:
    python3 rpc_load_generator.py --endpoints ws://localhost:6006 --rate 50 --duration 120

    # Multiple endpoints (round-robin):
    python3 rpc_load_generator.py \\
        --endpoints ws://localhost:6006 ws://localhost:6007 \\
        --rate 100 --duration 300

    # Custom weights:
    python3 rpc_load_generator.py --endpoints ws://localhost:6006 \\
        --weights '{"server_info":60,"account_info":30,"ledger":10}'
"""

import argparse
import asyncio
import itertools
import json
import logging
import math
import random
import sys
import time
import uuid
from dataclasses import dataclass, field
from typing import Any

import websockets

# websockets loads its submodules lazily, so websockets.exceptions is not
# reachable through the package until something imports it. REQUEST_FAILURES
# is built at import time and needs it now.
import websockets.exceptions

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Default command distribution matching realistic production ratios.
# Keys are RPC command names; values are relative weights.
DEFAULT_WEIGHTS: dict[str, int] = {
    # 40% health checks
    "server_info": 25,
    "fee": 15,
    # 30% wallet queries
    "account_info": 15,
    "account_lines": 8,
    "account_objects": 7,
    # 15% explorer
    "ledger": 10,
    "ledger_data": 5,
    # 10% tx lookups
    "tx": 5,
    "account_tx": 5,
    # 5% DEX queries
    "book_offers": 3,
    "amm_info": 2,
    # No path-finding command on purpose: every harness node disables
    # pathfinding, so those calls only ever produced errors. README.md,
    # "Pathfinding is not exercised", has the reason and the way back.
}

# Well-known genesis account for queries that require an account parameter.
GENESIS_ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"

# How long a single request waits for its reply.
RECV_TIMEOUT_S = 10.0

# Teardown budget for requests still in flight. Only the at-most-one request
# per connection that already holds the gate can still finish, and its worst
# case is the full receive timeout, so that plus a small grace is the whole
# useful wait. Requests still queued behind the gate would need
# queue_depth x round-trip, which is unbounded; they are cancelled instead.
DRAIN_TIMEOUT_S = RECV_TIMEOUT_S + 2.0

# Requests allowed to own one connection's recv() at a time. websockets
# rejects a second concurrent recv() on the same socket, so this must stay 1
# until a single reader task per connection demultiplexes replies to their
# waiters; correlating by id, which send_rpc now does, is necessary for that
# but not sufficient on its own. Concurrency comes from spreading requests
# round-robin over the endpoints instead.
MAX_INFLIGHT_PER_CONNECTION = 1

# Source of the ``id`` sent with every request. xrpld echoes a request's id at
# the top level of the reply, which is what lets send_rpc tell its own reply
# from one that an earlier, timed-out request left in the receive buffer.
_request_ids = itertools.count(1)

# Fraction of dispatched requests that must reach the server for a run to
# count as a measurement. The gate is one connection deep, so the ceiling is
# len(connections) / round-trip requests per second; asking for more than
# that silently drops the excess instead of erroring, which would report a
# perfect score on a run that generated a fraction of its intended load.
# Fifty percent mirrors the error-rate limit below, on the same reasoning: a
# run in which most requests did not happen is not a valid baseline.
MIN_DELIVERY_PCT = 50.0

# Error rate above which the run is treated as a failure.
MAX_ERROR_RATE_PCT = 50.0

# Failures that mean "this request failed", not "the generator is broken":
#   asyncio.TimeoutError - no reply within RECV_TIMEOUT_S. Same class as the
#                          builtin TimeoutError on Python 3.11+.
#   WebSocketException   - transport failure, including the connection being
#                          closed while a receive was outstanding, and the
#                          ConcurrencyError raised for a rejected concurrent
#                          recv() (it subclasses WebSocketException).
#   json.JSONDecodeError - reply body was not valid JSON.
#   AttributeError       - reply parsed to something with no .get(), e.g. a
#                          JSON array.
REQUEST_FAILURES: tuple[type[BaseException], ...] = (
    asyncio.TimeoutError,
    websockets.exceptions.WebSocketException,
    json.JSONDecodeError,
    AttributeError,
)

logger = logging.getLogger("rpc_load_generator")


# ---------------------------------------------------------------------------
# Latency helpers
# ---------------------------------------------------------------------------


def _percentile(sorted_values: list[float], quantile: float) -> float:
    """Return the nearest-rank percentile of an ascending list of values.

    The nearest-rank index is ``ceil(n * q) - 1``, clamped to the last
    element. Plain ``int(n * q)`` truncation selects one rank too high
    whenever ``n * q`` is a whole number — at n=100 it picks index 99, the
    maximum, so the reported p99 was really p100 (n=20 for p95).

    Args:
        sorted_values: Values sorted ascending.
        quantile:      Quantile to select, in (0, 1] — e.g. 0.99.

    Returns:
        The selected value, or 0.0 when the list is empty.
    """
    n = len(sorted_values)
    if n == 0:
        return 0.0
    idx = min(math.ceil(n * quantile) - 1, n - 1)
    return sorted_values[max(idx, 0)]


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class LoadStats:
    """Tracks request counts and latencies during a load run.

    ``total_dispatched`` counts intent and ``total_sent`` counts outcome, so
    the difference is the load that never happened. They diverge whenever the
    requested rate exceeds what the connections can carry: the dispatch loop
    keeps pace, the requests queue behind the per-connection gate, and
    teardown cancels whatever never got its turn. Without the two extra
    counters that shortfall shows up as nothing at all -- no error, no
    warning, and an error_rate_pct of 0 over a fraction of the traffic.

    Attributes:
        total_dispatched: Requests the dispatch loop created a task for.
        total_sent:       Requests that completed and were recorded.
        total_success:    Requests that returned a valid result.
        total_errors:     Requests that returned an error or timed out.
        total_cancelled:  Requests cancelled at teardown, never recorded.
        latencies:        Per-command round-trip times in seconds, for the
                          requests that got a reply. Requests that never got
                          one contribute no sample -- see record().
        command_counts:   Per-command request count, replied or not.
    """

    total_dispatched: int = 0
    total_sent: int = 0
    total_success: int = 0
    total_errors: int = 0
    total_cancelled: int = 0
    latencies: dict[str, list[float]] = field(default_factory=dict)
    command_counts: dict[str, int] = field(default_factory=dict)

    def record(self, command: str, latency: float | None, success: bool) -> None:
        """Record the outcome of a single RPC call.

        Pass ``latency=None`` when no reply arrived, i.e. a timeout or a
        transport failure. Such a request still counts as an error, but it
        contributes no latency sample: time-to-failure is not a round-trip
        time, and a timeout would inject RECV_TIMEOUT_S into the distribution
        and dominate the percentiles.

        A reply carrying ``status: error`` is the opposite case. The round
        trip completed and was timely, so its latency is a real measurement
        and is kept even though the request is counted as an error.
        """
        self.total_sent += 1
        if success:
            self.total_success += 1
        else:
            self.total_errors += 1
        self.command_counts[command] = self.command_counts.get(command, 0) + 1
        if latency is not None:
            self.latencies.setdefault(command, []).append(latency)

    def summary(self) -> dict[str, Any]:
        """Return a summary dict suitable for JSON serialization.

        ``total_sent``, ``total_success``, ``total_errors``,
        ``error_rate_pct`` and ``per_command`` keep their names and meanings;
        workload_orchestrator.py reads the first and third of those. The three
        delivery keys are additions. ``delivery_pct`` is 0.0 when nothing was
        dispatched at all -- a run that opened no connection delivered none of
        its load, and reporting 100% for it would be the same blind spot the
        key exists to close.
        """
        # Keyed off command_counts, not latencies: a command whose every
        # request timed out has a count but no samples, and dropping it from
        # the report would hide the command that failed worst.
        per_command: dict[str, Any] = {}
        for cmd in sorted(self.command_counts):
            sorted_lats = sorted(self.latencies.get(cmd, []))
            per_command[cmd] = {
                "count": self.command_counts[cmd],
                "latency_samples": len(sorted_lats),
                "p50_ms": round(_percentile(sorted_lats, 0.50) * 1000, 2),
                "p95_ms": round(_percentile(sorted_lats, 0.95) * 1000, 2),
                "p99_ms": round(_percentile(sorted_lats, 0.99) * 1000, 2),
            }
        return {
            "total_dispatched": self.total_dispatched,
            "total_sent": self.total_sent,
            "total_success": self.total_success,
            "total_errors": self.total_errors,
            "total_cancelled": self.total_cancelled,
            "error_rate_pct": (
                round(self.total_errors / self.total_sent * 100, 2)
                if self.total_sent
                else 0
            ),
            "delivery_pct": (
                round(self.total_sent / self.total_dispatched * 100, 2)
                if self.total_dispatched
                else 0.0
            ),
            "per_command": per_command,
        }


@dataclass
class Connection:
    """One open WebSocket endpoint together with its request gate.

    websockets raises rather than mis-delivering when two coroutines call
    ``recv()`` on the same socket, so every request holds ``gate`` across its
    send and its matching receive. Parallelism comes from the round-robin
    spread over endpoints: N endpoints allow N requests in flight.

        run_load ──round-robin──> Connection[0] ─gate─> send_rpc (1 at a time)
                              └─> Connection[1] ─gate─> send_rpc (1 at a time)

    Attributes:
        url:  WebSocket URL this connection was opened against, for logging.
        ws:   The open connection.
        gate: Limits concurrent send/receive pairs on ``ws`` to
              MAX_INFLIGHT_PER_CONNECTION.
    """

    url: str
    ws: websockets.ClientConnection
    gate: asyncio.Semaphore = field(
        default_factory=lambda: asyncio.Semaphore(MAX_INFLIGHT_PER_CONNECTION)
    )


# ---------------------------------------------------------------------------
# RPC command builders
# ---------------------------------------------------------------------------


def build_rpc_request(command: str) -> dict[str, Any]:
    """Build a native WebSocket command request for the given command.

    Uses rippled's native WS format (``{"command": ...}``) with flat
    parameters, NOT the JSON-RPC format (``{"method": ..., "params": [...]}``).

    Args:
        command: The rippled RPC command name.

    Returns:
        A dict representing the native WebSocket request body.
    """
    req: dict[str, Any] = {"command": command}

    if command in ("server_info", "fee"):
        pass  # No params needed.
    elif command == "account_info":
        req["account"] = GENESIS_ACCOUNT
    elif command == "account_lines":
        req["account"] = GENESIS_ACCOUNT
    elif command == "account_objects":
        req["account"] = GENESIS_ACCOUNT
        req["limit"] = 10
    elif command == "ledger":
        req["ledger_index"] = "validated"
    elif command == "ledger_data":
        req["ledger_index"] = "validated"
        req["limit"] = 5
    elif command == "tx":
        # Use a dummy hash — returns "txnNotFound" error but still exercises
        # the full RPC span pipeline for this transport (rpc.ws_message ->
        # rpc.command.tx). rpc.process is not in that chain: it is created
        # only on the HTTP/JSON-RPC path, which this client never uses.
        req["transaction"] = "0" * 64
        req["binary"] = False
    elif command == "account_tx":
        req["account"] = GENESIS_ACCOUNT
        req["ledger_index_min"] = -1
        req["ledger_index_max"] = -1
        req["limit"] = 5
    elif command == "book_offers":
        req["taker_pays"] = {"currency": "XRP"}
        req["taker_gets"] = {
            "currency": "USD",
            "issuer": GENESIS_ACCOUNT,
        }
        req["limit"] = 5
    elif command == "amm_info":
        # AMM may not exist — the span is still created on the server side.
        req["asset"] = {"currency": "XRP"}
        req["asset2"] = {
            "currency": "USD",
            "issuer": GENESIS_ACCOUNT,
        }

    return req


def choose_command(weights: dict[str, int]) -> str:
    """Select a random RPC command based on configured weights.

    Args:
        weights: Mapping of command name to relative weight.

    Returns:
        A command name string.
    """
    commands = list(weights.keys())
    w = [weights[c] for c in commands]
    return random.choices(commands, weights=w, k=1)[0]


# ---------------------------------------------------------------------------
# WebSocket RPC client
# ---------------------------------------------------------------------------


async def _recv_matching_reply(
    conn: Connection, request_id: int, command: str
) -> dict[str, Any]:
    """Read replies on ``conn`` until the one for ``request_id`` arrives.

    Cancelling a ``recv()`` does not discard the reply it was waiting for: the
    library queues incoming messages independently of any reader, so a request
    that hit RECV_TIMEOUT_S leaves its reply in the buffer. The next request on
    that connection used to read it and time its own round trip against
    somebody else's reply -- a near-zero latency, and a ``status`` belonging to
    a different command. The skew was permanent, because every later request on
    the connection stayed one reply behind for the rest of the run, so a single
    timeout quietly invalidated that connection's whole latency distribution.
    Discarding replies by id puts the stream back in step.

    The caller holds ``conn.gate``, so no other request can consume a reply
    while this loop runs. The deadline covers the whole exchange rather than
    each message, so a run of buffered replies cannot extend the wait without
    bound.

    Args:
        conn:       Connection to read from, with its gate already held.
        request_id: The ``id`` sent with this request.
        command:    RPC command name, for logging.

    Returns:
        The parsed reply whose ``id`` matches, or that carries no ``id``.

    Raises:
        asyncio.TimeoutError: If no matching reply arrived within
                              RECV_TIMEOUT_S.
    """
    deadline = time.monotonic() + RECV_TIMEOUT_S
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise asyncio.TimeoutError(f"{command} (id {request_id}) got no reply")
        raw = await asyncio.wait_for(conn.ws.recv(), timeout=remaining)
        reply = json.loads(raw)
        # A reply with no id counts as this request's: a few xrpld error paths
        # answer before the id is parsed, and treating those as stale would
        # turn a reported error into a timeout.
        reply_id = reply.get("id")
        if reply_id is None or reply_id == request_id:
            return reply
        logger.debug(
            "Discarded a reply for id %s while awaiting id %s (%s)",
            reply_id,
            request_id,
            command,
        )


async def send_rpc(
    conn: Connection,
    command: str,
    stats: LoadStats,
    inject_traceparent: bool = True,
) -> None:
    """Send a single RPC request over WebSocket and record the result.

    Holds ``conn.gate`` across the send and the matching receive so only one
    request at a time owns the connection's ``recv()``. The latency clock
    starts after the gate is acquired, so time spent queued behind a busy
    connection is not charged to the server.

    Every outcome is recorded, including failures, so error_rate_pct in the
    summary reflects every request that was actually sent.

    Args:
        conn:                Target connection and its request gate.
        command:             RPC command name.
        stats:               LoadStats instance to record results.
        inject_traceparent:  If True, add a W3C traceparent header field
                             to the request for context propagation testing.
    """
    request = build_rpc_request(command)
    request_id = next(_request_ids)
    request["id"] = request_id

    # Inject W3C traceparent for context propagation testing.
    # The rippled WebSocket handler extracts this from the JSON body
    # when present.
    if inject_traceparent:
        trace_id = uuid.uuid4().hex
        span_id = uuid.uuid4().hex[:16]
        request["traceparent"] = f"00-{trace_id}-{span_id}-01"

    async with conn.gate:
        t0 = time.monotonic()
        # The try covers the I/O and the parse only. Recording sits outside it
        # so a bug in record() surfaces as the task failure it is, instead of
        # being counted as one more failed request.
        try:
            await conn.ws.send(json.dumps(request))
            reply = await _recv_matching_reply(conn, request_id, command)
            latency = time.monotonic() - t0
            # Native WS responses have {"status": "success", "result": {...}}
            # or {"status": "error", "error": "...", "error_message": "..."}.
            success = reply.get("status") == "success"
        except REQUEST_FAILURES as exc:
            logger.debug("RPC %s failed: %s", command, exc)
            # No reply, so no latency sample -- see LoadStats.record().
            stats.record(command, None, False)
            return
        stats.record(command, latency, success)


async def open_connections(endpoints: list[str]) -> list[Connection]:
    """Open one persistent WebSocket connection per endpoint.

    Endpoints that refuse the connection are logged and skipped, so a partly
    reachable cluster still produces load.

    Args:
        endpoints: List of WebSocket URLs (ws://host:port).

    Returns:
        The connections that were established, possibly empty.
    """
    connections: list[Connection] = []
    for ep in endpoints:
        try:
            ws = await websockets.connect(ep, ping_interval=20, ping_timeout=10)
            connections.append(Connection(url=ep, ws=ws))
            logger.info("Connected to %s", ep)
        except Exception as exc:
            logger.error("Failed to connect to %s: %s", ep, exc)
    return connections


async def drain_requests(inflight: set[asyncio.Task[None]]) -> int:
    """Let in-flight requests finish, then cancel whatever is still stuck.

    A request may wait up to RECV_TIMEOUT_S for its reply, so closing the
    connections straight away would turn late replies into errors. Anything
    still unfinished after DRAIN_TIMEOUT_S is cancelled, and the count is
    returned so the caller can put the shortfall in the summary instead of
    losing it to a log line.

    Args:
        inflight: Tasks still tracked as unfinished. Finished tasks remove
                  themselves, so this is the outstanding set.

    Returns:
        Number of requests cancelled without being recorded.
    """
    pending = {task for task in inflight if not task.done()}
    if not pending:
        return 0

    logger.info(
        "Draining %d in-flight request(s), up to %.0fs...",
        len(pending),
        DRAIN_TIMEOUT_S,
    )
    _, stuck = await asyncio.wait(pending, timeout=DRAIN_TIMEOUT_S)
    if not stuck:
        logger.info("All in-flight requests completed.")
        return 0

    for task in stuck:
        task.cancel()
    await asyncio.gather(*stuck, return_exceptions=True)
    # Most of these never left the client: they were still waiting for the
    # per-connection gate. Up to one per connection had already been sent and
    # was waiting on recv() when it was cancelled, so the server may have
    # handled it. Either way CancelledError is not a REQUEST_FAILURES member,
    # so none of them reached stats.record and none are in total_sent.
    logger.warning(
        "Cancelled %d request(s) unfinished after %.0fs — not counted in "
        "total_sent; see total_cancelled and delivery_pct",
        len(stuck),
        DRAIN_TIMEOUT_S,
    )
    return len(stuck)


def log_progress(stats: LoadStats, elapsed: float) -> None:
    """Log throughput every 100 recorded requests.

    Args:
        stats:   Live counters.
        elapsed: Seconds since the run started.
    """
    if stats.total_sent % 100 != 0 or stats.total_sent == 0:
        return
    logger.info(
        "Progress: %d sent, %d errors, %.1f RPS (%.0fs elapsed)",
        stats.total_sent,
        stats.total_errors,
        stats.total_sent / elapsed if elapsed > 0 else 0,
        elapsed,
    )


async def dispatch_requests(
    connections: list[Connection],
    rate: float,
    duration: float,
    weights: dict[str, int],
    stats: LoadStats,
    inject_traceparent: bool,
) -> None:
    """Fire requests round-robin at the target rate, then drain them.

    Each request runs as its own task so the dispatch loop keeps its pace
    regardless of reply latency. Tasks are tracked, not forgotten, so
    teardown can drain them and so an exception can never escape unseen.

    Every task created counts towards ``stats.total_dispatched`` and every one
    cancelled at teardown towards ``stats.total_cancelled``, which is what
    makes an under-delivering run visible in the summary.

    Args:
        connections:        Open connections to spread requests over.
        rate:               Target requests per second.
        duration:           Total run time in seconds.
        weights:            Command distribution weights.
        stats:              LoadStats instance to record results in.
        inject_traceparent: Whether to inject W3C traceparent headers.
    """
    interval = 1.0 / rate if rate > 0 else 0.1
    start = time.monotonic()
    conn_idx = 0
    inflight: set[asyncio.Task[None]] = set()

    def reap(task: asyncio.Task[None]) -> None:
        """Untrack a finished request and report anything that escaped it."""
        inflight.discard(task)
        if not task.cancelled() and task.exception() is not None:
            logger.error("RPC task failed unexpectedly: %s", task.exception())

    try:
        while (time.monotonic() - start) < duration:
            conn = connections[conn_idx % len(connections)]
            conn_idx += 1
            task = asyncio.create_task(
                send_rpc(conn, choose_command(weights), stats, inject_traceparent)
            )
            inflight.add(task)
            task.add_done_callback(reap)
            stats.total_dispatched += 1

            await asyncio.sleep(interval)
            log_progress(stats, time.monotonic() - start)
    except asyncio.CancelledError:
        logger.info("Load generation cancelled.")
    finally:
        stats.total_cancelled += await drain_requests(inflight)


async def run_load(
    endpoints: list[str],
    rate: float,
    duration: float,
    weights: dict[str, int],
    inject_traceparent: bool,
) -> LoadStats:
    """Run the RPC load generator against the given endpoints.

    Distributes requests round-robin across endpoints at the specified
    rate (requests per second) for the given duration. Each connection
    serves one request at a time, so the ceiling per connection is
    1 / round-trip-latency requests per second; add endpoints to raise it.

    Args:
        endpoints:          List of WebSocket URLs (ws://host:port).
        rate:               Target requests per second.
        duration:           Total run time in seconds.
        weights:            Command distribution weights.
        inject_traceparent: Whether to inject W3C traceparent headers.

    Returns:
        LoadStats with aggregated results.
    """
    stats = LoadStats()

    connections = await open_connections(endpoints)
    if not connections:
        logger.error("No connections established. Aborting.")
        return stats

    logger.info(
        "Starting load: rate=%s RPS, duration=%ss, endpoints=%d",
        rate,
        duration,
        len(connections),
    )

    start = time.monotonic()
    try:
        await dispatch_requests(
            connections, rate, duration, weights, stats, inject_traceparent
        )
    finally:
        # Only reached once dispatch_requests has drained: closing a
        # connection under an outstanding receive raises ConnectionClosed,
        # which would be recorded as a failed request.
        for conn in connections:
            await conn.ws.close()

    elapsed = time.monotonic() - start
    logger.info(
        "Load complete: %d of %d dispatched sent, %d success, %d errors, "
        "%d cancelled in %.1fs (%.1f RPS)",
        stats.total_sent,
        stats.total_dispatched,
        stats.total_success,
        stats.total_errors,
        stats.total_cancelled,
        elapsed,
        stats.total_sent / elapsed if elapsed > 0 else 0,
    )

    return stats


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RPC Load Generator for rippled telemetry validation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage (50 RPS for 2 minutes):
  python3 rpc_load_generator.py --endpoints ws://localhost:6006 --rate 50 --duration 120

  # Multiple endpoints with custom weights:
  python3 rpc_load_generator.py \\
      --endpoints ws://localhost:6006 ws://localhost:6007 \\
      --rate 100 --duration 300 \\
      --weights '{"server_info": 80, "account_info": 20}'
        """,
    )
    parser.add_argument(
        "--endpoints",
        nargs="+",
        default=["ws://localhost:6006"],
        help="WebSocket endpoints (default: ws://localhost:6006)",
    )
    parser.add_argument(
        "--rate",
        type=float,
        default=50.0,
        help="Target requests per second (default: 50)",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=120.0,
        help="Run duration in seconds (default: 120)",
    )
    parser.add_argument(
        "--weights",
        type=str,
        default=None,
        help="JSON string of command weights (overrides defaults)",
    )
    parser.add_argument(
        "--no-traceparent",
        action="store_true",
        help="Disable W3C traceparent injection",
    )
    parser.add_argument(
        "--output",
        type=str,
        default=None,
        help="Write JSON summary to this file path",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    return parser.parse_args()


def main() -> None:
    """Main entry point for the RPC load generator."""
    args = parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
    )

    # Parse custom weights if provided.
    weights = DEFAULT_WEIGHTS.copy()
    if args.weights:
        try:
            custom = json.loads(args.weights)
            weights = {k: int(v) for k, v in custom.items()}
            logger.info("Using custom weights: %s", weights)
        except (json.JSONDecodeError, ValueError) as exc:
            logger.error("Invalid --weights JSON: %s", exc)
            sys.exit(1)

    # Run the load generator.
    stats = asyncio.run(
        run_load(
            endpoints=args.endpoints,
            rate=args.rate,
            duration=args.duration,
            weights=weights,
            inject_traceparent=not args.no_traceparent,
        )
    )

    summary = stats.summary()
    print(json.dumps(summary, indent=2))

    if args.output:
        with open(args.output, "w") as f:
            json.dump(summary, f, indent=2)
        logger.info("Summary written to %s", args.output)

    # Both gates are evaluated and reported before exiting, and the summary is
    # already on disk, so the caller sees every reason plus the numbers behind
    # it. A run that under-delivers has to fail as loudly as one that errors:
    # every downstream span and metric assertion would otherwise be checked
    # against a fraction of the intended traffic and still look healthy.
    failures: list[str] = []
    if summary["error_rate_pct"] > MAX_ERROR_RATE_PCT:
        failures.append(
            "error rate %.2f%% exceeds %.0f%% (%d of %d requests failed)"
            % (
                summary["error_rate_pct"],
                MAX_ERROR_RATE_PCT,
                summary["total_errors"],
                summary["total_sent"],
            )
        )
    if summary["delivery_pct"] < MIN_DELIVERY_PCT:
        failures.append(
            "delivered %.2f%% of dispatched requests, below %.0f%% "
            "(%d sent, %d cancelled, of %d dispatched) — lower --rate or add "
            "endpoints"
            % (
                summary["delivery_pct"],
                MIN_DELIVERY_PCT,
                summary["total_sent"],
                summary["total_cancelled"],
                summary["total_dispatched"],
            )
        )
    for reason in failures:
        logger.error("%s", reason)
    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
