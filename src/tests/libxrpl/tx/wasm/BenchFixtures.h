#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <string_view>

// Shared ledger state for the `*.bench.cpp` files.
//
// Each host function gets its own `<TestName>.bench.cpp`, mirroring its test one-for-one, which
// means ~61 translation units that would otherwise each build their own ledger, fund their own
// accounts and mint their own tokens. Declared here and defined once in `BenchFixtures.cpp`, so
// the function-local statics behind these accessors are single objects shared by the whole
// binary: the ledger is built once, each account is funded once, and a benchmark file is left
// holding only the call it measures.
//
// That sharing is safe because every helper does its setup inside its own `static`, so it
// happens exactly once no matter how many files ask for it. It is also why the accounts have
// bench-specific names — two files funding "owner" against one ledger would be a duplicate
// account, not a fresh one.
//
// Nothing here is timed. Benchmarks call these to get a host, then measure only the host call.

namespace xrpl::test::bench {

// Fail a fixture step loudly.
//
// A benchmark has no assertions — nothing here can `EXPECT_EQ` — and that makes a quietly
// failed setup step the most dangerous thing in this file. If the escrow is never created or
// the slot never cached, the host call still runs; it just takes the not-found path, which is
// cheap, plausible-looking, and completely wrong as a price. The `result >= 0` guard in
// `benchmarkThroughVm` catches that for VM cases, but an `Impl` case calls the host directly
// and has no such check.
//
// So every setup step whose failure would change what is being measured is checked here, and
// throws rather than returning. Google Benchmark reports the message and stops, which is the
// outcome you want: no number at all beats a confident wrong one.
[[noreturn]] void
benchSetupFailed(std::string_view what);

// The one ledger every benchmark runs against.
BenchFixture&
benchLedger();

// Two funded accounts, enough for every keylet shape and every object below.
Account const&
benchAlice();

Account const&
benchBob();

// A sequence number for the keylets that take one. Arbitrary — a keylet hashes whatever it is
// given, so the value cannot change the cost.
inline constexpr std::uint32_t kBenchSeq = 42;

// ---------------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------------

// An EscrowFinish carrying a two-element memo array: something for the nested-field getters to
// walk to and the array-length getters to count.
TxAssembler
benchMemoTx();

// `sfMemos[0].sfMemoData` — a two-step locator path, the shape the nested getters are priced for.
FieldLocator
benchMemoLocator();

// ---------------------------------------------------------------------------
// Hosts
// ---------------------------------------------------------------------------

// The default: transaction carries the memo array, current object is Alice's account root.
WasmHost
benchHost();

// The same, with Alice's account root pinned to slot 1, for the `le_*` getters that read
// through a cache slot rather than the current object.
WasmHost
benchCachedHost();

// An account root has no arrays, so the array-length getters that read a *ledger object* need a
// different one. A signer list has `sfSignerEntries`; without it those calls would answer
// `FieldNotFound` and the benchmark would time the rejection instead of the work.
Account const&
benchSignerListOwner();

// Current object is the signer list.
WasmHost
benchSignerListHost();

// Signer list pinned to slot 1.
WasmHost
benchCachedSignerListHost();

// A real escrow, created through the real transactor — the current object for `home_le_field`,
// which is the one getter whose cost depends on the object it reads rather than its arguments.
Keylet const&
benchEscrow();

WasmHost
benchEscrowHost();

// ---------------------------------------------------------------------------
// Inputs that need building
// ---------------------------------------------------------------------------

// Canonical float operands. Zeroed bytes decode as a non-canonical float and would be refused
// before any arithmetic ran, so the whole family shares these two known-good values.
Slice
benchFloatX();

Slice
benchFloatY();

// Rounding mode 0 throughout the float family: modes select a tie-breaking rule, not a different
// algorithm, so they do not move the cost, and pinning one keeps the fourteen comparable.
inline constexpr std::int32_t kBenchMode = 0;

// A signed message for `check_sig`, produced once: signing is far more expensive than the
// verification being measured, so it must not happen inside the timed loop.
SignedMessage const&
benchSignedMessage();

// A well-formed NFToken id with the fixture's known taxon, flags, fee and sequence baked in, so
// the id-extractor getters have real fields to pull out rather than zeros.
uint256 const&
benchNftId();

}  // namespace xrpl::test::bench
