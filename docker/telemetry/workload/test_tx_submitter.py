#!/usr/bin/env python3
"""Tests for tx_submitter.py's account-funding confirmation.

Run with plain python3 -- there is no pytest in the harness requirements, and
this file needs nothing but the standard library:

    python3 docker/telemetry/workload/test_tx_submitter.py

What is under test is how setup decides an account is usable. The old code
submitted the funding Payment, slept a flat 10 seconds, and read each sequence
once. Under the txq-burst and mixed-peak phases the open-ledger fee is
escalated on purpose, so the funding transactions sit in the TxQ, every account
reads Sequence 0, and the phase aborts with "only 0 of 8 created accounts were
funded". Every way of getting the wait wrong is expensive rather than silent:
the phase sends no traffic and the whole run reddens.

Both halves are covered: that a late confirmation is still seen, and that a
confirmation which never arrives ends at a deadline instead of hanging.
"""

import asyncio
import itertools
import json
import logging
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent))

import tx_submitter as tx  # noqa: E402

# The functions under test log per account per poll, and the deadline tests
# poll until they time out. At INFO that buries the PASS/FAIL lines in hundreds
# of kilobytes of expected output.
tx.logger.setLevel(logging.CRITICAL)


class FakeWs:
    """Minimal stand-in for a rippled WebSocket connection.

    Speaks enough of the native protocol for ws_request to work: it echoes the
    request id and wraps the canned payload in ``result``. Driving the real
    ws_request rather than stubbing it keeps the id-matching logic in the test
    path, since that is where a reply can be mis-attributed.

    Args:
        sequences: Per-account iterables of the Sequence values account_info
                   should report on successive calls. A value of 0 means the
                   account root does not exist yet.
        fee_result: Payload for the ``fee`` command.
    """

    def __init__(
        self,
        sequences: dict[str, Any] | None = None,
        fee_result: dict[str, Any] | None = None,
    ) -> None:
        self._sequences = {k: iter(v) for k, v in (sequences or {}).items()}
        self._fee_result = fee_result
        self._outbox: list[str] = []
        self.commands: list[str] = []
        self.account_info_calls = 0

    async def send(self, payload: str) -> None:
        request = json.loads(payload)
        command = request["command"]
        self.commands.append(command)

        if command == "account_info":
            self.account_info_calls += 1
            account = request["account"]
            try:
                seq = next(self._sequences[account])
            except StopIteration:
                seq = 0
            # rippled omits account_data entirely for an account that does not
            # exist, which is what get_account_sequence turns into 0.
            result: dict[str, Any] = (
                {"account_data": {"Sequence": seq}} if seq else {"error": "actNotFound"}
            )
        elif command == "fee":
            result = self._fee_result if self._fee_result is not None else {}
        else:
            result = {"engine_result": "tesSUCCESS"}

        self._outbox.append(
            json.dumps({"id": request["id"], "status": "success", "result": result})
        )

    async def recv(self) -> str:
        return self._outbox.pop(0)


def _accounts(*names: str) -> list[tx.Account]:
    """Accounts whose addresses are their names, so fakes can key on them."""
    return [tx.Account(name=n, account=n, seed=f"seed-{n}") for n in names]


def test_late_confirmation_is_still_seen() -> None:
    """A sequence that only appears on a later poll must still confirm.

    This is the whole defect: the funding transaction was queued, so the first
    reads report nothing. Confirming late is the normal case under load, not an
    error.

    The production change that makes this fail: reading each sequence once, or
    going back to a fixed sleep followed by a single read.
    """
    accts = _accounts("alice", "bob")
    ws = FakeWs(sequences={"alice": [0, 0, 7], "bob": [0, 0, 9]})

    confirmed = asyncio.run(
        tx.wait_for_funding(ws, accts, timeout_sec=5.0, poll_sec=0.0)
    )

    assert confirmed == 2, confirmed
    assert [a.funded for a in accts] == [True, True]
    assert [a.sequence for a in accts] == [7, 9]


def test_returns_as_soon_as_everything_confirms() -> None:
    """A healthy cluster must not pay any waiting cost.

    The old code slept 10 seconds unconditionally, on every transaction phase.
    One pass over the accounts is enough when they are already funded.

    The production change that makes this fail: polling to the deadline
    regardless, or sleeping before the first read instead of after a miss.
    """
    accts = _accounts("alice", "bob", "carol")
    ws = FakeWs(sequences={"alice": [3], "bob": [4], "carol": [5]})

    confirmed = asyncio.run(
        tx.wait_for_funding(ws, accts, timeout_sec=30.0, poll_sec=10.0)
    )

    assert confirmed == 3, confirmed
    # Exactly one account_info per account: a second round would mean it kept
    # polling after everything had already confirmed.
    assert ws.account_info_calls == 3, ws.account_info_calls


def test_deadline_is_honoured_when_nothing_confirms() -> None:
    """A cluster that never funds must end at the deadline, not hang.

    The generator runs under a phase budget, so an unbounded wait would be
    killed by the orchestrator and report as a timeout rather than as a funding
    failure, which points at the wrong component.

    The production change that makes this fail: looping while any account is
    unconfirmed without checking elapsed time.
    """
    accts = _accounts("alice", "bob")
    ws = FakeWs(sequences={"alice": itertools.repeat(0), "bob": itertools.repeat(0)})

    confirmed = asyncio.run(
        tx.wait_for_funding(ws, accts, timeout_sec=0.05, poll_sec=0.01)
    )

    assert confirmed == 0, confirmed
    assert [a.funded for a in accts] == [False, False]


def test_only_confirmed_accounts_are_marked_funded() -> None:
    """Partial funding must leave the unconfirmed account unusable.

    The builders address accounts by position, so an unfunded account left in
    the usable list fails every transaction it is picked for.

    The production change that makes this fail: setting funded for the whole
    list once any account confirms, or leaving funded at its submit-time value.
    """
    accts = _accounts("alice", "bob")
    ws = FakeWs(sequences={"alice": [0, 11], "bob": itertools.repeat(0)})

    confirmed = asyncio.run(
        tx.wait_for_funding(ws, accts, timeout_sec=0.05, poll_sec=0.01)
    )

    assert confirmed == 1, confirmed
    assert accts[0].funded is True and accts[0].sequence == 11
    assert accts[1].funded is False


def test_open_ledger_fee_is_read_from_the_fee_rpc() -> None:
    """The funding fee must come from current load, not a constant.

    Escalation is deliberate in the txq-burst phase, so a funding Payment at
    the base fee is exactly the one that gets queued.

    The production change that makes this fail: returning the fallback
    unconditionally, or reading base_fee instead of open_ledger_fee.
    """
    ws = FakeWs(fee_result={"drops": {"base_fee": "10", "open_ledger_fee": "5320"}})

    assert asyncio.run(tx.get_open_ledger_fee(ws)) == 5320


def test_open_ledger_fee_falls_back_when_absent() -> None:
    """A missing fee field must not abort account setup.

    Fee lookup is an optimisation for the funding path. If the field is absent
    or unparseable, funding should still be attempted at a sane fee.

    The production change that makes this fail: indexing the response directly,
    or letting a ValueError from int() escape.
    """
    assert asyncio.run(tx.get_open_ledger_fee(FakeWs(fee_result={}))) == (
        tx.FUNDING_FEE_FALLBACK_DROPS
    )
    ws = FakeWs(fee_result={"drops": {"open_ledger_fee": "not-a-number"}})
    assert asyncio.run(tx.get_open_ledger_fee(ws)) == tx.FUNDING_FEE_FALLBACK_DROPS


def test_funding_wait_fits_inside_the_orchestrator_budget() -> None:
    """The funding wait must not outlast the grace the orchestrator allows.

    Setup can wait twice, once after the first submit and once after the retry.
    If that exceeds SUBPROCESS_GRACE_SEC the generator is killed mid-wait and the
    phase records a timeout, which points at the orchestrator instead of at the
    funding that actually failed. These are two constants in two files with
    nothing but this test tying them together.

    The production change that makes this fail: raising
    FUNDING_CONFIRM_TIMEOUT_SEC without raising SUBPROCESS_GRACE_SEC.
    """
    import workload_orchestrator as wo

    worst_case = 2 * tx.FUNDING_CONFIRM_TIMEOUT_SEC
    assert worst_case < wo.SUBPROCESS_GRACE_SEC, (
        f"funding can wait {worst_case}s but the grace is "
        f"{wo.SUBPROCESS_GRACE_SEC}s"
    )


def main() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    # Collecting nothing is a failure, not a pass -- the same silent-green trap
    # the tests themselves guard against, reproduced in the runner.
    if not tests:
        print("FAIL: no tests were collected")
        return 1
    failed = 0
    for test in tests:
        try:
            test()
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {test.__name__}: {exc}")
        except SystemExit as exc:
            failed += 1
            print(f"ERROR {test.__name__}: SystemExit({exc.code})")
        except Exception as exc:  # noqa: BLE001 - report any error as a failure
            failed += 1
            print(f"ERROR {test.__name__}: {type(exc).__name__}: {exc}")
        else:
            print(f"PASS {test.__name__}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
