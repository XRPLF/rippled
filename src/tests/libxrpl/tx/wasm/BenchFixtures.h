#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/NFTFixture.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <string_view>
#include <utility>

// Shared ledger state for the `*.bench.cpp` files.
//
// Each host function gets its own `<TestName>.bench.cpp`, mirroring its test one-for-one, which
// means ~61 translation units that would otherwise each build their own ledger, fund their own
// accounts and mint their own tokens. Everything here is `inline`, so a function-local static
// inside it is one object shared by the whole binary: the ledger is built once, each account is
// funded once, and a benchmark file is left holding only the call it measures.
//
// That sharing is safe because every helper does its setup inside its own `static`, so it
// happens exactly once no matter how many files ask for it. It is also why the accounts have
// bench-specific names — two files funding "owner" against one ledger would be a duplicate
// account, not a fresh one.
//
// Nothing here is timed. Benchmarks call these to get a host, then measure only the host call.

namespace xrpl::test::bench {

// The one ledger every benchmark runs against.
inline BenchFixture&
benchLedger()
{
    static BenchFixture value;
    return value;
}

// Two funded accounts, enough for every keylet shape and every object below.
inline Account const&
benchAlice()
{
    static auto const kValue = benchLedger().fund("benchAlice");
    return kValue;
}

inline Account const&
benchBob()
{
    static auto const kValue = benchLedger().fund("benchBob");
    return kValue;
}

// A sequence number for the keylets that take one. Arbitrary — a keylet hashes whatever it is
// given, so the value cannot change the cost.
inline constexpr std::uint32_t kBenchSeq = 42;

// ---------------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------------

// An EscrowFinish carrying a two-element memo array: something for the nested-field getters to
// walk to and the array-length getters to count.
inline TxAssembler
benchMemoTx()
{
    auto assembler = escrowFinishTx(benchLedger().ledger, benchAlice());
    assembler.build = [inner = std::move(assembler.build)](STObject& obj) {
        inner(obj);
        auto memos = STArray{};
        memos.push_back(makeMemo(RealHostFixture::toBytes("hello")));
        memos.push_back(makeMemo(RealHostFixture::toBytes("world")));
        obj.setFieldArray(sfMemos, memos);
    };
    return assembler;
}

// `sfMemos[0].sfMemoData` — a two-step locator path, the shape the nested getters are priced for.
inline FieldLocator
benchMemoLocator()
{
    return FieldLocator{{sfMemos.getCode(), 0, sfMemoData.getCode()}};
}

// ---------------------------------------------------------------------------
// Hosts
// ---------------------------------------------------------------------------

// The default: transaction carries the memo array, current object is Alice's account root.
inline WasmHost
benchHost()
{
    auto assembler = benchMemoTx();
    return benchLedger().makeHost(
        keylet::account(benchAlice().id()), assembler.type, std::move(assembler.build));
}

// The same, with Alice's account root pinned to slot 1, for the `le_*` getters that read
// through a cache slot rather than the current object.
inline WasmHost
benchCachedHost()
{
    auto host = benchHost();
    (void)host->cacheLedgerObj(keylet::account(benchAlice().id()).key, 1);
    return host;
}

// An account root has no arrays, so the array-length getters that read a *ledger object* need a
// different one. A signer list has `sfSignerEntries`; without it those calls would answer
// `FieldNotFound` and the benchmark would time the rejection instead of the work.
inline Account const&
benchSignerListOwner()
{
    static auto const kValue = [] {
        auto const acct = benchLedger().fund("benchSigners");
        benchLedger().makeSignerList(acct, 2, {{benchAlice(), 1}, {benchBob(), 1}});
        return acct;
    }();
    return kValue;
}

// Current object is the signer list.
inline WasmHost
benchSignerListHost()
{
    auto assembler = bareTx();
    return benchLedger().makeHost(
        keylet::signerList(benchSignerListOwner().id()),
        assembler.type,
        std::move(assembler.build));
}

// Signer list pinned to slot 1.
inline WasmHost
benchCachedSignerListHost()
{
    auto assembler = bareTx();
    auto host = benchLedger().makeHost(
        keylet::account(AccountID{}), assembler.type, std::move(assembler.build));
    (void)host->cacheLedgerObj(keylet::signerList(benchSignerListOwner().id()).key, 1);
    return host;
}

// A real escrow, created through the real transactor — the current object for `home_le_field`,
// which is the one getter whose cost depends on the object it reads rather than its arguments.
inline Keylet const&
benchEscrow()
{
    static auto const kValue = [] {
        auto const ownerSeq = benchLedger().ledger.getAccountRoot(benchAlice().id()).getSequence();
        benchLedger().ledger.submit(
            transactions::EscrowCreateBuilder{benchAlice().id(), benchBob().id(), XRP(100)}
                .setFinishAfter(900'000'000),
            benchAlice());
        benchLedger().ledger.close();
        return keylet::escrow(benchAlice().id(), SeqProxy::rawSequence(ownerSeq));
    }();
    return kValue;
}

inline WasmHost
benchEscrowHost()
{
    return benchLedger().makeHost(benchEscrow());
}

// ---------------------------------------------------------------------------
// Inputs that need building
// ---------------------------------------------------------------------------

// Canonical float operands. Zeroed bytes decode as a non-canonical float and would be refused
// before any arithmetic ran, so the whole family shares these two known-good values.
inline Slice
benchFloatX()
{
    return FloatTest::slice(FloatTest::kPi);
}

inline Slice
benchFloatY()
{
    return FloatTest::slice(FloatTest::kTwo);
}

// Rounding mode 0 throughout the float family: modes select a tie-breaking rule, not a different
// algorithm, so they do not move the cost, and pinning one keeps the fourteen comparable.
inline constexpr std::int32_t kBenchMode = 0;

// A signed message for `check_sig`, produced once: signing is far more expensive than the
// verification being measured, so it must not happen inside the timed loop.
inline SignedMessage const&
benchSignedMessage()
{
    static auto const kValue = signMessage("the quick brown fox jumps over the lazy dog");
    return kValue;
}

// A well-formed NFToken id with the fixture's known taxon, flags, fee and sequence baked in, so
// the id-extractor getters have real fields to pull out rather than zeros.
inline uint256 const&
benchNftId()
{
    // `makeNftId` is a static member, so no fixture instance is needed to reach it.
    static auto const kValue = NFTTest::makeNftId(benchAlice().id());
    return kValue;
}

}  // namespace xrpl::test::bench
