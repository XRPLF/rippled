#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>

namespace xrpl::test::bench {

// The ledger and canned inputs every `*.bench.cpp` measures against.
class Fixtures
{
public:
    // The one set of fixtures every benchmark shares, built on first use.
    static Fixtures&
    instance();

    // A sequence number for the keylets that take one. Arbitrary — a keylet hashes whatever it
    // is given, so the value cannot change the cost.
    static constexpr std::uint32_t kSeq = 42;

    // Rounding mode 0 throughout the float family: modes select a tie-breaking rule, not a
    // different algorithm, so they do not move the cost, and pinning one keeps the fourteen
    // comparable.
    static constexpr std::int32_t kRoundingMode = 0;

    // Two funded accounts, enough for every keylet shape and every object below.
    [[nodiscard]] Account const&
    alice() const;
    [[nodiscard]] Account const&
    bob() const;

    // The default host: its transaction carries a two-element memo array (something for the
    // nested getters to walk to and the array-length getters to count) and its current object is
    // Alice's account root.
    [[nodiscard]] WasmHost
    host();

    // The same, with Alice's account root pinned to slot 1, for the `le_*` getters that read
    // through a cache slot rather than the current object.
    [[nodiscard]] WasmHost
    cachedHost();

    // An account root has no arrays, so the array-length getters that read a *ledger object*
    // need a different one. A signer list has `sfSignerEntries`; without it those calls would
    // answer `FieldNotFound` and the benchmark would time the rejection instead of the work.
    [[nodiscard]] WasmHost
    signerListHost();
    [[nodiscard]] WasmHost
    cachedSignerListHost();

    // A host whose `trace` output is captured rather than dropped, so the log-enabled path can
    // be measured against the log-disabled one that `host()` gives.
    [[nodiscard]] WasmHost
    tracingHost();

    // A real escrow, created through the real transactor — the current object for
    // `home_le_field`, the one getter whose cost depends on the object it reads rather than on
    // its arguments.
    [[nodiscard]] Keylet const&
    escrow() const;
    [[nodiscard]] WasmHost
    escrowHost();

    // `sfMemos[0].sfMemoData` — a two-step locator path, the shape the nested getters are priced
    // for.
    [[nodiscard]] static FieldLocator
    memoLocator();

    // Canonical float operands. Zeroed bytes decode as a non-canonical float and would be
    // refused before any arithmetic ran, so the whole family shares these two known-good values.
    [[nodiscard]] static Slice
    floatX();
    [[nodiscard]] static Slice
    floatY();

    // A signed message for `check_sig`. Signing is far more expensive than the verification
    // being measured, so it happens once here rather than inside a timed loop.
    [[nodiscard]] SignedMessage const&
    signedMessage() const;

    // A well-formed NFToken id with the fixture's known taxon, flags, fee and sequence baked in,
    // so the id-extractor getters have real fields to pull out rather than zeros.
    [[nodiscard]] uint256 const&
    nftId() const;

private:
    // Order matters, and that is the reason this is a constructor rather than a pile of lazy
    // statics: the accounts have to be funded before the signer list and the escrow can be built
    // on them.
    Fixtures();

    // The transaction the default host runs, carrying the memo array.
    [[nodiscard]] TxAssembler
    memoTx();

    BenchFixture ledger_;
    Account alice_;
    Account bob_;
    Account signerListOwner_;
    Keylet escrow_;
    SignedMessage signedMessage_;
    uint256 nftId_;
};

}  // namespace xrpl::test::bench
