#pragma once

// Base for the "impl" wasm tests: a *real* `WasmHostFunctionsImpl` over a *real* ledger,
// built with no Application / jtx / beast::unit_test::Suite. The ledger is a `TxTest`
// (genesis ledger + OpenView + real transactor dispatch); the host is constructed from
// its `ServiceRegistry` and `OpenView` through an `ApplyContext`.
//
// This is the counterpart to `HostContextFixture` (mock host, interop): here the host
// really computes, so a test asserts a value against the ledger's own source of truth
// (`keylet::escrow`, a real field's bytes, `wasm_float`), rather than what a mock was
// asked. Pure-computation host functions (keylets, floats, check_sig, sha512_half, nft
// decoders) ignore the ledger; the reading getters read the object `leKey` points at.

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>  // TapNone
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>  // keylet::account
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/TxTest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl::test {

static Bytes
toBytes(std::uint8_t value)
{
    return {value};
}

static Bytes
toBytes(std::uint16_t value)
{
    auto const* b = reinterpret_cast<uint8_t const*>(&value);
    auto const* e = reinterpret_cast<uint8_t const*>(&value + 1);
    return Bytes{b, e};
}

static Bytes
toBytes(std::uint32_t value)
{
    auto const* b = reinterpret_cast<uint8_t const*>(&value);
    auto const* e = reinterpret_cast<uint8_t const*>(&value + 1);
    return Bytes{b, e};
}

static Bytes
toBytes(uint256 const& value)
{
    return Bytes{value.begin(), value.end()};
}

static Bytes
toBytes(Issue const& issue)
{
    Serializer s;
    s.addBitString(issue.currency);
    if (!isXRP(issue.currency))
        s.addBitString(issue.account);
    auto const data = s.getData();
    return data;
}

static Bytes
toBytes(Asset const& asset)
{
    if (asset.holds<Issue>())
        return toBytes(asset.get<Issue>());

    auto const& mptIssue = asset.get<MPTIssue>();
    auto const& mptID = mptIssue.getMptID();
    return Bytes{mptID.cbegin(), mptID.cend()};
}

static Bytes
toBytes(STAmount const& amount)
{
    Serializer msg;
    amount.add(msg);
    auto const data = msg.getData();

    return data;
}

static Bytes
toBytes(STNumber const& number)
{
    Serializer msg;
    number.add(msg);
    auto const data = msg.getData();

    return data;
}

class WasmImplTest : public testing::Test
{
public:
    // The real ledger. Tests populate it with `ledger.createAccount()` / `submit()` /
    // `close()` before reading through the host.
    TxTest ledger;

    // A real host bound to `leKey` — the "current"/home object the `*_field` and
    // `*_arr_len` getters read. Defaults to a throwaway keylet for the many functions
    // (keylets, floats, sig, hash, nft decoders) that never touch the current object.
    //
    // Returns a reference into a fixture-owned host so its `ApplyContext&` outlives it;
    // call once per test.
    WasmHostFunctionsImpl&
    host(Keylet const& leKey = keylet::account(AccountID{}))
    {
        // The finish tx the host runs under. Its contents are irrelevant to the
        // functions these tests exercise; it only has to be a well-formed shell.
        finishTx_ = std::make_shared<STTx>(ttESCROW_FINISH, [](STObject&) {});
        context_.emplace(
            ledger.getServiceRegistry(),
            ledger.getOpenLedger(),
            *finishTx_,
            tesSUCCESS,
            ledger.getOpenLedger().fees().base,
            TapNone,
            beast::Journal{beast::Journal::getNullSink()});
        host_.emplace(*context_, leKey);
        return *host_;
    }

private:
    std::shared_ptr<STTx const> finishTx_;
    std::optional<ApplyContext> context_;
    std::optional<WasmHostFunctionsImpl> host_;
};

}  // namespace xrpl::test
