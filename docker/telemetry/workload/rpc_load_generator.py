#!/usr/bin/env python3
"""RPC Load Generator for rippled telemetry validation.

Connects to one or more rippled WebSocket endpoints and fires all traced
RPC commands at configurable rates with realistic production-like
distribution.

Command distribution (default weights):
  40%  Health checks:   server_info, fee
  30%  Wallet queries:  account_info, account_lines, account_objects
  15%  Explorer:        ledger, ledger_data
  10%  TX lookups:      tx, account_tx
   5%  DEX queries:     book_offers, amm_info

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
import json
import logging
import random
import sys
import time
import uuid
from dataclasses import dataclass, field
from typing import Any

import websockets

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
}

# Well-known genesis account for queries that require an account parameter.
GENESIS_ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"

logger = logging.getLogger("rpc_load_generator")


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class LoadStats:
    """Tracks request counts and latencies during a load run.

    Attributes:
        total_sent:     Total RPC requests dispatched.
        total_success:  Requests that returned a valid result.
        total_errors:   Requests that returned an error or timed out.
        latencies:      Per-command list of round-trip times in seconds.
        command_counts: Per-command request count.
    """

    total_sent: int = 0
    total_success: int = 0
    total_errors: int = 0
    latencies: dict[str, list[float]] = field(default_factory=dict)
    command_counts: dict[str, int] = field(default_factory=dict)

    def record(self, command: str, latency: float, success: bool) -> None:
        """Record the outcome of a single RPC call."""
        self.total_sent += 1
        if success:
            self.total_success += 1
        else:
            self.total_errors += 1
        self.latencies.setdefault(command, []).append(latency)
        self.command_counts[command] = self.command_counts.get(command, 0) + 1

    def summary(self) -> dict[str, Any]:
        """Return a summary dict suitable for JSON serialization."""
        per_command: dict[str, Any] = {}
        for cmd, lats in self.latencies.items():
            sorted_lats = sorted(lats)
            n = len(sorted_lats)
            per_command[cmd] = {
                "count": self.command_counts.get(cmd, 0),
                "p50_ms": round(sorted_lats[n // 2] * 1000, 2) if n else 0,
                "p95_ms": (round(sorted_lats[int(n * 0.95)] * 1000, 2) if n else 0),
                "p99_ms": (round(sorted_lats[int(n * 0.99)] * 1000, 2) if n else 0),
            }
        return {
            "total_sent": self.total_sent,
            "total_success": self.total_success,
            "total_errors": self.total_errors,
            "error_rate_pct": (
                round(self.total_errors / self.total_sent * 100, 2)
                if self.total_sent
                else 0
            ),
            "per_command": per_command,
        }


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
        # the full RPC span pipeline (rpc.request -> rpc.process -> rpc.command.tx).
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


async def send_rpc(
    ws: websockets.WebSocketClientProtocol,
    command: str,
    stats: LoadStats,
    inject_traceparent: bool = True,
) -> None:
    """Send a single RPC request over WebSocket and record the result.

    Args:
        ws:                  Open WebSocket connection.
        command:             RPC command name.
        stats:               LoadStats instance to record results.
        inject_traceparent:  If True, add a W3C traceparent header field
                             to the request for context propagation testing.
    """
    request = build_rpc_request(command)

    # Inject W3C traceparent for context propagation testing.
    # The rippled WebSocket handler extracts this from the JSON body
    # when present (Phase 2 context propagation).
    if inject_traceparent:
        trace_id = uuid.uuid4().hex
        span_id = uuid.uuid4().hex[:16]
        request["traceparent"] = f"00-{trace_id}-{span_id}-01"

    t0 = time.monotonic()
    try:
        await ws.send(json.dumps(request))
        raw = await asyncio.wait_for(ws.recv(), timeout=10.0)
        latency = time.monotonic() - t0
        response = json.loads(raw)
        # Native WS responses have {"status": "success", "result": {...}}
        # or {"status": "error", "error": "...", "error_message": "..."}.
        success = response.get("status") == "success"
        stats.record(command, latency, success)
    except (asyncio.TimeoutError, websockets.exceptions.WebSocketException) as exc:
        latency = time.monotonic() - t0
        stats.record(command, latency, False)
        logger.debug("RPC %s failed: %s", command, exc)


async def run_load(
    endpoints: list[str],
    rate: float,
    duration: float,
    weights: dict[str, int],
    inject_traceparent: bool,
) -> LoadStats:
    """Run the RPC load generator against the given endpoints.

    Distributes requests round-robin across endpoints at the specified
    rate (requests per second) for the given duration.

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
    interval = 1.0 / rate if rate > 0 else 0.1

    # Open persistent connections to all endpoints.
    connections: list[websockets.WebSocketClientProtocol] = []
    for ep in endpoints:
        try:
            ws = await websockets.connect(ep, ping_interval=20, ping_timeout=10)
            connections.append(ws)
            logger.info("Connected to %s", ep)
        except Exception as exc:
            logger.error("Failed to connect to %s: %s", ep, exc)

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
    conn_idx = 0

    try:
        while (time.monotonic() - start) < duration:
            command = choose_command(weights)
            ws = connections[conn_idx % len(connections)]
            conn_idx += 1

            # Fire-and-forget style with bounded concurrency via sleep.
            asyncio.create_task(send_rpc(ws, command, stats, inject_traceparent))
            await asyncio.sleep(interval)

            # Periodic progress log.
            elapsed = time.monotonic() - start
            if stats.total_sent % 100 == 0 and stats.total_sent > 0:
                actual_rps = stats.total_sent / elapsed if elapsed > 0 else 0
                logger.info(
                    "Progress: %d sent, %d errors, %.1f RPS (%.0fs elapsed)",
                    stats.total_sent,
                    stats.total_errors,
                    actual_rps,
                    elapsed,
                )
    except asyncio.CancelledError:
        logger.info("Load generation cancelled.")
    finally:
        # Allow in-flight requests to complete.
        await asyncio.sleep(2)
        for ws in connections:
            await ws.close()

    elapsed = time.monotonic() - start
    logger.info(
        "Load complete: %d sent, %d success, %d errors in %.1fs (%.1f RPS)",
        stats.total_sent,
        stats.total_success,
        stats.total_errors,
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

    # Exit with error if error rate exceeds 50%.
    if summary["error_rate_pct"] > 50:
        logger.error("High error rate: %.1f%%", summary["error_rate_pct"])
        sys.exit(1)


if __name__ == "__main__":
    main()
