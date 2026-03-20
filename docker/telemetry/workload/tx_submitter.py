#!/usr/bin/env python3
"""Transaction Submitter for rippled telemetry validation.

Generates diverse transaction types against a rippled cluster to exercise
the full span and metric surface: tx.process, tx.apply, ledger.build,
consensus.*, and all associated attributes.

Pre-funds test accounts from the genesis account, then submits a
configurable mix of transaction types at a target TPS.

Supported transaction types:
  - Payment (XRP and issued currencies)
  - OfferCreate / OfferCancel (DEX activity)
  - TrustSet (trust line creation)
  - NFTokenMint / NFTokenCreateOffer / NFTokenAcceptOffer
  - EscrowCreate / EscrowFinish
  - AMMCreate / AMMDeposit / AMMWithdraw (if amendment enabled)

Usage:
    python3 tx_submitter.py --endpoint ws://localhost:6006 --tps 5 --duration 120

    # Custom transaction mix:
    python3 tx_submitter.py --endpoint ws://localhost:6006 \\
        --weights '{"Payment":50,"OfferCreate":20,"TrustSet":10,"NFTokenMint":10,"EscrowCreate":10}'
"""

import argparse
import asyncio
import json
import logging
import random
import sys
import time
from dataclasses import dataclass, field
from typing import Any

import websockets

logger = logging.getLogger("tx_submitter")

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

GENESIS_ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
GENESIS_SEED = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb"

# Amount to fund each test account (100,000 XRP in drops).
FUND_AMOUNT = "100000000000"

# Default transaction mix weights (relative).
DEFAULT_TX_WEIGHTS: dict[str, int] = {
    "Payment": 40,
    "OfferCreate": 15,
    "OfferCancel": 5,
    "TrustSet": 10,
    "NFTokenMint": 10,
    "NFTokenCreateOffer": 5,
    "EscrowCreate": 5,
    "EscrowFinish": 5,
    "AMMCreate": 3,
    "AMMDeposit": 2,
}

# Number of test accounts to create.
NUM_TEST_ACCOUNTS = 8


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class Account:
    """Represents a funded XRPL test account.

    Attributes:
        name:      Human-readable name (e.g., "alice").
        account:   Classic address (rXXX...).
        seed:      Secret seed for signing.
        sequence:  Next available sequence number.
    """

    name: str
    account: str
    seed: str
    sequence: int = 0


@dataclass
class TxStats:
    """Tracks transaction submission results.

    Attributes:
        total_submitted: Total transactions sent to the network.
        total_success:   Transactions that returned tesSUCCESS or terQUEUED.
        total_errors:    Transactions that returned an error engine_result.
        by_type:         Per-transaction-type count of submissions.
        errors_by_type:  Per-transaction-type count of errors.
    """

    total_submitted: int = 0
    total_success: int = 0
    total_errors: int = 0
    by_type: dict[str, int] = field(default_factory=dict)
    errors_by_type: dict[str, int] = field(default_factory=dict)

    def record(self, tx_type: str, success: bool) -> None:
        """Record the result of a transaction submission."""
        self.total_submitted += 1
        self.by_type[tx_type] = self.by_type.get(tx_type, 0) + 1
        if success:
            self.total_success += 1
        else:
            self.total_errors += 1
            self.errors_by_type[tx_type] = self.errors_by_type.get(tx_type, 0) + 1

    def summary(self) -> dict[str, Any]:
        """Return a summary dict suitable for JSON serialization."""
        return {
            "total_submitted": self.total_submitted,
            "total_success": self.total_success,
            "total_errors": self.total_errors,
            "success_rate_pct": (
                round(self.total_success / self.total_submitted * 100, 2)
                if self.total_submitted
                else 0
            ),
            "by_type": self.by_type,
            "errors_by_type": self.errors_by_type,
        }


# ---------------------------------------------------------------------------
# WebSocket RPC helpers
# ---------------------------------------------------------------------------


async def ws_request(
    ws: websockets.WebSocketClientProtocol,
    command: str,
    params: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Send a native WebSocket command and return the result payload.

    Uses rippled's native WebSocket format (``command`` key with flat
    parameters).  The response has ``status`` at the top level and the
    actual data payload inside ``result``.  This helper unwraps the
    ``result`` dict so callers can read fields directly.

    Args:
        ws:      Open WebSocket connection.
        command: RPC command name (e.g., ``account_info``, ``submit``).
        params:  Optional flat parameter dict merged into the request.

    Returns:
        The inner ``result`` dict from the response.

    Raises:
        RuntimeError: If the request fails or times out.
    """
    request: dict[str, Any] = {"command": command}
    if params:
        request.update(params)
    await ws.send(json.dumps(request))
    raw = await asyncio.wait_for(ws.recv(), timeout=30.0)
    resp = json.loads(raw)

    # WS command format: {"status": "success", "result": {...}, "type": "response"}
    # On error: {"status": "error", "error": "...", "error_message": "..."}
    if resp.get("status") == "error":
        logger.warning(
            "%s error: %s — %s",
            command,
            resp.get("error", "unknown"),
            resp.get("error_message", ""),
        )
    return resp.get("result", resp)


async def create_account(ws: websockets.WebSocketClientProtocol, name: str) -> Account:
    """Create a new account via wallet_propose RPC.

    Args:
        ws:   Open WebSocket connection.
        name: Human-readable name for the account.

    Returns:
        An Account instance with the generated keypair.
    """
    result = await ws_request(ws, "wallet_propose")
    if "account_id" not in result:
        raise RuntimeError(
            f"wallet_propose failed: {json.dumps(result, indent=None)[:300]}"
        )
    return Account(
        name=name,
        account=result["account_id"],
        seed=result["master_seed"],
    )


async def fund_account(
    ws: websockets.WebSocketClientProtocol,
    dest: Account,
    genesis_seq: int,
) -> tuple[bool, int]:
    """Fund a test account from genesis.

    Args:
        ws:          Open WebSocket connection.
        dest:        Destination account to fund.
        genesis_seq: Current genesis account sequence number.

    Returns:
        Tuple of (success: bool, next_sequence: int).
    """
    resp = await ws_request(
        ws,
        "submit",
        {
            "secret": GENESIS_SEED,
            "tx_json": {
                "TransactionType": "Payment",
                "Account": GENESIS_ACCOUNT,
                "Destination": dest.account,
                "Amount": FUND_AMOUNT,
                "Sequence": genesis_seq,
            },
        },
    )
    engine_result = resp.get("engine_result", "unknown")
    success = engine_result in ("tesSUCCESS", "terQUEUED")
    if not success:
        # Log the full response to help diagnose submit failures in CI.
        logger.warning(
            "Fund %s failed: engine_result=%s, full response: %s",
            dest.name,
            engine_result,
            json.dumps(resp, indent=None)[:500],
        )
    return success, genesis_seq + 1


async def get_account_sequence(
    ws: websockets.WebSocketClientProtocol, account: str
) -> int:
    """Get the current sequence number for an account.

    Args:
        ws:      Open WebSocket connection.
        account: Classic address.

    Returns:
        Current sequence number.
    """
    resp = await ws_request(ws, "account_info", {"account": account})
    if "account_data" not in resp:
        # Log full response to diagnose WS API format issues.
        logger.warning(
            "account_info for %s: no account_data, full response: %s",
            account[:12],
            json.dumps(resp, indent=None)[:500],
        )
        return 0
    return resp["account_data"].get("Sequence", 0)


# ---------------------------------------------------------------------------
# Transaction builders
# ---------------------------------------------------------------------------


def build_payment(sender: Account, receiver: Account) -> dict[str, Any]:
    """Build an XRP Payment transaction.

    Args:
        sender:   Source account.
        receiver: Destination account.

    Returns:
        Transaction JSON and signing secret.
    """
    amount = str(random.randint(1000, 1000000))  # 0.001 - 1 XRP
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "Payment",
            "Account": sender.account,
            "Destination": receiver.account,
            "Amount": amount,
            "Sequence": sender.sequence,
        },
    }


def build_offer_create(sender: Account) -> dict[str, Any]:
    """Build an OfferCreate transaction (XRP/USD pair).

    Args:
        sender: Account placing the offer.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "OfferCreate",
            "Account": sender.account,
            "TakerPays": str(random.randint(100000, 10000000)),
            "TakerGets": {
                "currency": "USD",
                "issuer": GENESIS_ACCOUNT,
                "value": str(round(random.uniform(0.1, 100.0), 2)),
            },
            "Sequence": sender.sequence,
        },
    }


def build_offer_cancel(sender: Account) -> dict[str, Any]:
    """Build an OfferCancel transaction.

    Uses a non-existent offer sequence — will fail gracefully but still
    exercises the tx.process span pipeline.

    Args:
        sender: Account cancelling the offer.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "OfferCancel",
            "Account": sender.account,
            "OfferSequence": max(1, sender.sequence - 1),
            "Sequence": sender.sequence,
        },
    }


def build_trust_set(sender: Account) -> dict[str, Any]:
    """Build a TrustSet transaction for a USD trust line.

    Args:
        sender: Account setting the trust line.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "TrustSet",
            "Account": sender.account,
            "LimitAmount": {
                "currency": "USD",
                "issuer": GENESIS_ACCOUNT,
                "value": "1000000",
            },
            "Sequence": sender.sequence,
        },
    }


def build_nftoken_mint(sender: Account) -> dict[str, Any]:
    """Build an NFTokenMint transaction.

    Args:
        sender: Account minting the NFT.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "NFTokenMint",
            "Account": sender.account,
            "NFTokenTaxon": random.randint(0, 100),
            "Flags": 8,  # tfTransferable
            "Sequence": sender.sequence,
        },
    }


def build_nftoken_create_offer(sender: Account) -> dict[str, Any]:
    """Build an NFTokenCreateOffer transaction.

    Uses a dummy NFTokenID — will fail but exercises the span pipeline.

    Args:
        sender: Account creating the NFT offer.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "NFTokenCreateOffer",
            "Account": sender.account,
            "NFTokenID": "0" * 64,
            "Amount": str(random.randint(100000, 1000000)),
            "Flags": 1,  # tfSellNFToken
            "Sequence": sender.sequence,
        },
    }


def build_escrow_create(sender: Account, receiver: Account) -> dict[str, Any]:
    """Build an EscrowCreate transaction.

    Creates a time-based escrow that finishes 10 seconds from now.

    Args:
        sender:   Account creating the escrow.
        receiver: Destination account for escrow funds.

    Returns:
        Transaction JSON and signing secret.
    """
    # Ripple epoch offset: 946684800 seconds from Unix epoch
    ripple_time = int(time.time()) - 946684800
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "EscrowCreate",
            "Account": sender.account,
            "Destination": receiver.account,
            "Amount": str(random.randint(100000, 1000000)),
            "FinishAfter": ripple_time + 10,
            "Sequence": sender.sequence,
        },
    }


def build_escrow_finish(sender: Account, owner: Account) -> dict[str, Any]:
    """Build an EscrowFinish transaction.

    Uses a dummy offer sequence — will likely fail but exercises spans.

    Args:
        sender: Account finishing the escrow.
        owner:  Account that created the escrow.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "EscrowFinish",
            "Account": sender.account,
            "Owner": owner.account,
            "OfferSequence": max(1, owner.sequence - 2),
            "Sequence": sender.sequence,
        },
    }


def build_amm_create(sender: Account) -> dict[str, Any]:
    """Build an AMMCreate transaction (XRP/USD pool).

    Requires the AMM amendment to be enabled on the network.

    Args:
        sender: Account creating the AMM pool.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "AMMCreate",
            "Account": sender.account,
            "Amount": str(random.randint(10000000, 100000000)),
            "Amount2": {
                "currency": "USD",
                "issuer": GENESIS_ACCOUNT,
                "value": str(round(random.uniform(10.0, 1000.0), 2)),
            },
            "TradingFee": 500,  # 0.5%
            "Sequence": sender.sequence,
        },
    }


def build_amm_deposit(sender: Account) -> dict[str, Any]:
    """Build an AMMDeposit transaction.

    Args:
        sender: Account depositing into the AMM pool.

    Returns:
        Transaction JSON and signing secret.
    """
    return {
        "secret": sender.seed,
        "tx_json": {
            "TransactionType": "AMMDeposit",
            "Account": sender.account,
            "Asset": {"currency": "XRP"},
            "Asset2": {
                "currency": "USD",
                "issuer": GENESIS_ACCOUNT,
            },
            "Amount": str(random.randint(1000000, 10000000)),
            "Flags": 0x00080000,  # tfSingleAsset
            "Sequence": sender.sequence,
        },
    }


# Transaction type -> builder function mapping.
# Each builder takes (accounts: list[Account]) and returns submit params.
TX_BUILDERS: dict[str, Any] = {
    "Payment": lambda accts: build_payment(accts[0], accts[1]),
    "OfferCreate": lambda accts: build_offer_create(accts[0]),
    "OfferCancel": lambda accts: build_offer_cancel(accts[0]),
    "TrustSet": lambda accts: build_trust_set(accts[2]),
    "NFTokenMint": lambda accts: build_nftoken_mint(accts[3]),
    "NFTokenCreateOffer": lambda accts: build_nftoken_create_offer(accts[3]),
    "EscrowCreate": lambda accts: build_escrow_create(accts[4], accts[1]),
    "EscrowFinish": lambda accts: build_escrow_finish(accts[4], accts[4]),
    "AMMCreate": lambda accts: build_amm_create(accts[5]),
    "AMMDeposit": lambda accts: build_amm_deposit(accts[5]),
}


# ---------------------------------------------------------------------------
# Main submission loop
# ---------------------------------------------------------------------------


async def setup_accounts(
    ws: websockets.WebSocketClientProtocol,
) -> list[Account]:
    """Create and fund test accounts from genesis.

    Generates NUM_TEST_ACCOUNTS accounts via wallet_propose, then funds
    each with FUND_AMOUNT XRP from genesis.

    Args:
        ws: Open WebSocket connection to a rippled node.

    Returns:
        List of funded Account instances.
    """
    account_names = ["alice", "bob", "carol", "dave", "eve", "frank", "grace", "heidi"]

    logger.info("Creating %d test accounts...", NUM_TEST_ACCOUNTS)
    accounts: list[Account] = []
    for name in account_names[:NUM_TEST_ACCOUNTS]:
        acct = await create_account(ws, name)
        accounts.append(acct)
        logger.info("  Created %s: %s", name, acct.account)

    # Get genesis sequence.
    genesis_seq = await get_account_sequence(ws, GENESIS_ACCOUNT)
    logger.info("Genesis sequence: %d", genesis_seq)

    # Fund all accounts.
    logger.info("Funding test accounts...")
    for acct in accounts:
        success, genesis_seq = await fund_account(ws, acct, genesis_seq)
        if success:
            logger.info("  Funded %s", acct.name)
        else:
            logger.warning("  Failed to fund %s", acct.name)

    # Wait for funding transactions to be validated.
    logger.info("Waiting 10s for funding transactions to validate...")
    await asyncio.sleep(10)

    # Refresh sequence numbers for all accounts.
    for acct in accounts:
        try:
            acct.sequence = await get_account_sequence(ws, acct.account)
            logger.info("  %s sequence: %d", acct.name, acct.sequence)
        except Exception as exc:
            logger.warning("  Failed to get sequence for %s: %s", acct.name, exc)

    return accounts


async def submit_transaction(
    ws: websockets.WebSocketClientProtocol,
    tx_type: str,
    accounts: list[Account],
    stats: TxStats,
) -> None:
    """Submit a single transaction of the given type.

    Selects the appropriate builder, constructs the transaction, submits
    it via the submit RPC, and records the result.

    Args:
        ws:       Open WebSocket connection.
        tx_type:  Transaction type name (e.g., "Payment").
        accounts: List of funded test accounts.
        stats:    TxStats instance to record results.
    """
    builder = TX_BUILDERS.get(tx_type)
    if not builder:
        logger.warning("Unknown transaction type: %s", tx_type)
        return

    try:
        params = builder(accounts)
        # Identify which account is the sender to bump its sequence.
        sender_addr = params["tx_json"]["Account"]
        sender = next((a for a in accounts if a.account == sender_addr), None)

        resp = await ws_request(ws, "submit", params)
        engine_result = resp.get("engine_result", "unknown")
        success = engine_result in (
            "tesSUCCESS",
            "terQUEUED",
            "tecUNFUNDED_OFFER",
            "tecNO_DST_INSUF_XRP",
        )
        stats.record(tx_type, success)

        if sender:
            sender.sequence += 1

        if not success:
            logger.debug(
                "%s result: %s (%s)",
                tx_type,
                engine_result,
                resp.get("engine_result_message", ""),
            )
    except Exception as exc:
        stats.record(tx_type, False)
        logger.debug("%s error: %s", tx_type, exc)


async def run_submitter(
    endpoint: str,
    tps: float,
    duration: float,
    weights: dict[str, int],
) -> TxStats:
    """Run the transaction submitter against a single endpoint.

    Args:
        endpoint: WebSocket URL (ws://host:port).
        tps:      Target transactions per second.
        duration: Total run time in seconds.
        weights:  Transaction type distribution weights.

    Returns:
        TxStats with aggregated results.
    """
    stats = TxStats()
    interval = 1.0 / tps if tps > 0 else 0.5

    ws = await websockets.connect(endpoint, ping_interval=20, ping_timeout=10)
    logger.info("Connected to %s", endpoint)

    try:
        # Setup test accounts.
        accounts = await setup_accounts(ws)
        if len(accounts) < 6:
            logger.error("Need at least 6 funded accounts, got %d", len(accounts))
            return stats

        # Build weighted command list.
        tx_types = list(weights.keys())
        tx_weights = [weights[t] for t in tx_types]

        logger.info(
            "Starting TX submission: tps=%s, duration=%ss, types=%d",
            tps,
            duration,
            len(tx_types),
        )

        start = time.monotonic()
        while (time.monotonic() - start) < duration:
            tx_type = random.choices(tx_types, weights=tx_weights, k=1)[0]
            await submit_transaction(ws, tx_type, accounts, stats)
            await asyncio.sleep(interval)

            # Progress logging every 50 transactions.
            if stats.total_submitted % 50 == 0 and stats.total_submitted > 0:
                elapsed = time.monotonic() - start
                actual_tps = stats.total_submitted / elapsed if elapsed > 0 else 0
                logger.info(
                    "Progress: %d submitted, %d success, %d errors, "
                    "%.1f TPS (%.0fs elapsed)",
                    stats.total_submitted,
                    stats.total_success,
                    stats.total_errors,
                    actual_tps,
                    elapsed,
                )

    finally:
        await ws.close()

    elapsed = time.monotonic() - start
    logger.info(
        "Submission complete: %d submitted, %d success, %d errors "
        "in %.1fs (%.1f TPS)",
        stats.total_submitted,
        stats.total_success,
        stats.total_errors,
        elapsed,
        stats.total_submitted / elapsed if elapsed > 0 else 0,
    )

    return stats


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Transaction Submitter for rippled telemetry validation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage (5 TPS for 2 minutes):
  python3 tx_submitter.py --endpoint ws://localhost:6006 --tps 5 --duration 120

  # Custom transaction mix:
  python3 tx_submitter.py --endpoint ws://localhost:6006 \\
      --weights '{"Payment": 60, "OfferCreate": 20, "TrustSet": 20}'
        """,
    )
    parser.add_argument(
        "--endpoint",
        type=str,
        default="ws://localhost:6006",
        help="WebSocket endpoint (default: ws://localhost:6006)",
    )
    parser.add_argument(
        "--tps",
        type=float,
        default=5.0,
        help="Target transactions per second (default: 5)",
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
        help="JSON string of transaction type weights (overrides defaults)",
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
    """Main entry point for the transaction submitter."""
    args = parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
    )

    # Parse custom weights if provided.
    weights = DEFAULT_TX_WEIGHTS.copy()
    if args.weights:
        try:
            custom = json.loads(args.weights)
            weights = {k: int(v) for k, v in custom.items()}
            logger.info("Using custom weights: %s", weights)
        except (json.JSONDecodeError, ValueError) as exc:
            logger.error("Invalid --weights JSON: %s", exc)
            sys.exit(1)

    # Run the submitter.
    stats = asyncio.run(
        run_submitter(
            endpoint=args.endpoint,
            tps=args.tps,
            duration=args.duration,
            weights=weights,
        )
    )

    summary = stats.summary()
    print(json.dumps(summary, indent=2))

    if args.output:
        with open(args.output, "w") as f:
            json.dump(summary, f, indent=2)
        logger.info("Summary written to %s", args.output)


if __name__ == "__main__":
    main()
