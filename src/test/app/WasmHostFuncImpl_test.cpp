//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>

#include <xrpld/app/misc/WasmHostFuncImpl.h>

namespace ripple {
namespace test {

static std::array<std::uint8_t, 2>
toBytes(std::uint16_t value)
{
    std::array<std::uint8_t, 2> bytes = {
        static_cast<std::uint8_t>(value & 0xFF),
        static_cast<std::uint8_t>(value >> 8)};
    return bytes;
}

static std::array<std::uint8_t, 4>
toBytes(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        static_cast<std::uint8_t>((value >> 24) & 0xFF)};
}

static ApplyContext
createApplyContext(
    test::jtx::Env& env,
    OpenView& ov,
    STTx const& tx = STTx(ttESCROW_FINISH, [](STObject&) {}))
{
    test::StreamSink sink{beast::severities::kWarning};
    beast::Journal jlog{sink};
    ApplyContext ac{
        env.app(),
        ov,
        tx,
        tesSUCCESS,
        env.current()->fees().base,
        tapNONE,
        jlog};
    return ac;
}

struct WasmHostFuncImpl_test : public beast::unit_test::suite
{
    void
    testCacheLedgerObj()
    {
        testcase("cacheLedgerObj");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, 2);
        auto const accountKeylet = keylet::account(env.master);
        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, -1).error() ==
                HostFunctionError::SLOT_OUT_RANGE);
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 257).error() ==
                HostFunctionError::SLOT_OUT_RANGE);
            BEAST_EXPECT(
                hfs.cacheLedgerObj(dummyEscrow.key, 0).error() ==
                HostFunctionError::LEDGER_OBJ_NOT_FOUND);
            BEAST_EXPECT(hfs.cacheLedgerObj(accountKeylet.key, 0).value() == 1);

            for (int i = 1; i <= 256; ++i)
            {
                auto const result = hfs.cacheLedgerObj(accountKeylet.key, i);
                BEAST_EXPECT(result.has_value() && result.value() == i);
            }
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 0).error() ==
                HostFunctionError::SLOTS_FULL);
        }

        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            for (int i = 1; i <= 256; ++i)
            {
                auto const result = hfs.cacheLedgerObj(accountKeylet.key, 0);
                BEAST_EXPECT(result.has_value() && result.value() == i);
            }
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 0).error() ==
                HostFunctionError::SLOTS_FULL);
        }
    }

    void
    testGetTxField()
    {
        testcase("getTxField");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            obj.setAccountID(sfOwner, env.master.id());
            obj.setFieldU32(sfOfferSequence, env.seq(env.master));
            obj.setFieldU32(sfComputationAllowance, 1000);
            obj.setFieldArray(sfMemos, STArray{});
        });
        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);
            auto const account = hfs.getTxField(sfAccount);
            BEAST_EXPECT(
                account.has_value() &&
                std::equal(
                    account.value().begin(),
                    account.value().end(),
                    env.master.id().data()));
            auto const owner = hfs.getTxField(sfOwner);
            BEAST_EXPECT(
                owner.has_value() &&
                std::equal(
                    owner.value().begin(),
                    owner.value().end(),
                    env.master.id().data()));
            auto const txType = hfs.getTxField(sfTransactionType);
            BEAST_EXPECT(
                txType.has_value() &&
                std::equal(
                    txType.value().begin(),
                    txType.value().end(),
                    toBytes(ttESCROW_FINISH).begin()));
            auto const offerSeq = hfs.getTxField(sfOfferSequence);
            BEAST_EXPECT(
                offerSeq.has_value() &&
                std::equal(
                    offerSeq.value().begin(),
                    offerSeq.value().end(),
                    toBytes(env.seq(env.master)).begin()));
            auto const compAllowance = hfs.getTxField(sfComputationAllowance);
            std::uint32_t const expectedAllowance = 1000;
            BEAST_EXPECT(
                compAllowance.has_value() &&
                std::equal(
                    compAllowance.value().begin(),
                    compAllowance.value().end(),
                    toBytes(expectedAllowance).begin()));

            auto const notPresent = hfs.getTxField(sfDestination);
            if (BEAST_EXPECT(!notPresent.has_value()))
                BEAST_EXPECT(
                    notPresent.error() == HostFunctionError::FIELD_NOT_FOUND);

            auto const memos = hfs.getTxField(sfMemos);
            if (BEAST_EXPECT(!memos.has_value()))
                BEAST_EXPECT(
                    memos.error() == HostFunctionError::NOT_LEAF_FIELD);
        }
    }

    void
    testGetCurrentLedgerObjField()
    {
        testcase("getCurrentLedgerObjField");
        using namespace test::jtx;
        using namespace std::chrono;

        Env env{*this};

        // Fund the account and create an escrow so the ledger object exists
        env(escrow::create(env.master, env.master, XRP(100)),
            escrow::finish_time(env.now() + 1s));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        // Find the escrow ledger object
        auto const escrowKeylet =
            keylet::escrow(env.master, env.seq(env.master) - 1);
        BEAST_EXPECT(env.le(escrowKeylet));

        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        // Should return the Account field from the escrow ledger object
        auto const account = hfs.getCurrentLedgerObjField(sfAccount);
        if (BEAST_EXPECTS(
                account.has_value(),
                std::to_string(static_cast<int>(account.error()))))
            BEAST_EXPECT(std::equal(
                account.value().begin(),
                account.value().end(),
                env.master.id().data()));

        // Should return the Amount field from the escrow ledger object
        // TODO: improve this check once there's full issue/amount support
        auto const amountField = hfs.getCurrentLedgerObjField(sfAmount);
        BEAST_EXPECT(amountField.has_value());

        // Should return nullopt for a field not present
        auto const notPresent = hfs.getCurrentLedgerObjField(sfOwner);
        BEAST_EXPECT(
            !notPresent.has_value() &&
            notPresent.error() == HostFunctionError::FIELD_NOT_FOUND);
    }

    void
    testGetLedgerObjField()
    {
        testcase("getLedgerObjField");
        using namespace test::jtx;
        using namespace std::chrono;

        Env env{*this};
        // Fund the account and create an escrow so the ledger object exists
        env(escrow::create(env.master, env.master, XRP(100)),
            escrow::finish_time(env.now() + 1s));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const accountKeylet = keylet::account(env.master.id());
        WasmHostFunctionsImpl hfs(ac, accountKeylet);

        // Cache the escrow ledger object in slot 1
        auto cacheResult = hfs.cacheLedgerObj(accountKeylet.key, 1);
        BEAST_EXPECT(cacheResult.has_value() && cacheResult.value() == 1);

        // Should return the Account field from the cached ledger object
        auto const account = hfs.getLedgerObjField(1, sfAccount);
        if (BEAST_EXPECTS(
                account.has_value(),
                std::to_string(static_cast<int>(account.error()))))
            BEAST_EXPECT(std::equal(
                account.value().begin(),
                account.value().end(),
                env.master.id().data()));

        // Should return the Balance field from the cached ledger object
        // TODO: improve this check once there's full issue/amount support
        auto const balanceField = hfs.getLedgerObjField(1, sfBalance);
        BEAST_EXPECT(balanceField.has_value());

        // Should return error for slot out of range
        auto const outOfRange = hfs.getLedgerObjField(0, sfAccount);
        BEAST_EXPECT(
            !outOfRange.has_value() &&
            outOfRange.error() == HostFunctionError::SLOT_OUT_RANGE);

        auto const tooHigh = hfs.getLedgerObjField(257, sfAccount);
        BEAST_EXPECT(
            !tooHigh.has_value() &&
            tooHigh.error() == HostFunctionError::SLOT_OUT_RANGE);

        // Should return error for empty slot
        auto const emptySlot = hfs.getLedgerObjField(2, sfAccount);
        BEAST_EXPECT(
            !emptySlot.has_value() &&
            emptySlot.error() == HostFunctionError::EMPTY_SLOT);

        // Should return error for field not present
        auto const notPresent = hfs.getLedgerObjField(1, sfOwner);
        BEAST_EXPECT(
            !notPresent.has_value() &&
            notPresent.error() == HostFunctionError::FIELD_NOT_FOUND);
    }

    void
    testGetTxNestedField()
    {
        testcase("getTxNestedField");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};

        // Create a transaction with a nested array field
        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            STArray memos;
            STObject memoObj(sfMemo);
            memoObj.setFieldVL(sfMemoData, Slice("hello", 5));
            memos.push_back(memoObj);
            obj.setFieldArray(sfMemos, memos);
        });

        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            // Locator for sfMemos[0].sfMemo.sfMemoData
            // Locator is a sequence of int32_t codes:
            // [sfMemos.fieldCode, 0, sfMemoData.fieldCode]
            std::vector<int32_t> locatorVec = {
                sfMemos.fieldCode, 0, sfMemoData.fieldCode};
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));

            auto const result = hfs.getTxNestedField(locator);
            if (BEAST_EXPECTS(
                    result.has_value(),
                    std::to_string(static_cast<int>(result.error()))))
            {
                std::string memoData(
                    result.value().begin(), result.value().end());
                BEAST_EXPECT(memoData == "hello");
            }
        }

        {
            // can use the nested locator for base fields too
            std::vector<int32_t> locatorVec = {sfAccount.fieldCode};
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));

            auto const account = hfs.getTxNestedField(locator);
            if (BEAST_EXPECTS(
                    account.has_value(),
                    std::to_string(static_cast<int>(account.error()))))
            {
                BEAST_EXPECT(std::equal(
                    account.value().begin(),
                    account.value().end(),
                    env.master.id().data()));
            }
        }

        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError) {
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result = hfs.getTxNestedField(locator);
            BEAST_EXPECT(
                !result.has_value() && result.error() == expectedError);
        };
        // Locator for non-existent base field
        expectError(
            {sfSigners.fieldCode,  // sfSigners does not exist
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // Locator for non-existent index
        expectError(
            {sfMemos.fieldCode,
             1,  // index 1 does not exist
             sfMemoData.fieldCode},
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // Locator for non-existent nested field
        expectError(
            {sfMemos.fieldCode,
             0,
             sfURI.fieldCode},  // sfURI does not exist in the memo
            HostFunctionError::FIELD_NOT_FOUND);

        // Locator for non-existent base sfield
        expectError(
            {field_code(20000, 20000),  // nonexistent SField code
             0,
             sfAccount.fieldCode},
            HostFunctionError::INVALID_FIELD);

        // Locator for non-existent nested sfield
        expectError(
            {sfMemos.fieldCode,  // nonexistent SField code
             0,
             field_code(20000, 20000)},
            HostFunctionError::INVALID_FIELD);

        // Locator for STArray
        expectError({sfMemos.fieldCode}, HostFunctionError::NOT_LEAF_FIELD);

        // Locator for nesting into non-array/object field
        expectError(
            {sfAccount.fieldCode,  // sfAccount is not an array or object
             0,
             sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);

        // Locator for empty locator
        expectError({}, HostFunctionError::LOCATOR_MALFORMED);

        // Locator for malformed locator (not multiple of 4)
        {
            std::vector<int32_t> locatorVec = {sfMemos.fieldCode};
            Slice malformedLocator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()), 3);
            auto const malformedResult = hfs.getTxNestedField(malformedLocator);
            BEAST_EXPECT(
                !malformedResult.has_value() &&
                malformedResult.error() ==
                    HostFunctionError::LOCATOR_MALFORMED);
        }
    }

    void
    testGetCurrentLedgerObjNestedField()
    {
        testcase("getCurrentLedgerObjNestedField");
        using namespace test::jtx;

        Env env{*this};

        env(escrow::create(env.master, env.master, XRP(100)),
            escrow::finish_time(env.now() + std::chrono::seconds(1)));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        // Find the escrow ledger object
        auto const escrowKeylet =
            keylet::escrow(env.master, env.seq(env.master) - 1);
        BEAST_EXPECT(env.le(escrowKeylet));

        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        // Note: no nested fields on Escrow objects, so just basic tests here

        // Locator for base field
        std::vector<int32_t> baseLocator = {sfAccount.fieldCode};
        Slice baseLocatorSlice(
            reinterpret_cast<uint8_t const*>(baseLocator.data()),
            baseLocator.size() * sizeof(int32_t));
        auto const account =
            hfs.getCurrentLedgerObjNestedField(baseLocatorSlice);
        if (BEAST_EXPECTS(
                account.has_value(),
                std::to_string(static_cast<int>(account.error()))))
        {
            BEAST_EXPECT(std::equal(
                account.value().begin(),
                account.value().end(),
                env.master.id().data()));
        }

        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError) {
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result = hfs.getCurrentLedgerObjNestedField(locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == expectedError,
                    std::to_string(static_cast<int>(result.error())));
        };
        // Locator for non-existent base field
        expectError(
            {sfSigners.fieldCode,  // sfSigners does not exist
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);
        // Locator for nesting into non-array/object field
        expectError(
            {sfAccount.fieldCode,  // sfAccount is not an array or object
             0,
             sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);

        // Locator for empty locator
        Slice emptyLocator(nullptr, 0);
        auto const emptyResult =
            hfs.getCurrentLedgerObjNestedField(emptyLocator);
        BEAST_EXPECT(
            !emptyResult.has_value() &&
            emptyResult.error() == HostFunctionError::LOCATOR_MALFORMED);

        // Locator for malformed locator (not multiple of 4)
        std::vector<int32_t> malformedLocatorVec = {sfMemos.fieldCode};
        Slice malformedLocator(
            reinterpret_cast<uint8_t const*>(malformedLocatorVec.data()), 3);
        auto const malformedResult =
            hfs.getCurrentLedgerObjNestedField(malformedLocator);
        BEAST_EXPECT(
            !malformedResult.has_value() &&
            malformedResult.error() == HostFunctionError::LOCATOR_MALFORMED);
    }

    void
    run() override
    {
        testCacheLedgerObj();
        testGetTxField();
        testGetCurrentLedgerObjField();
        testGetLedgerObjField();
        testGetTxNestedField();
        testGetCurrentLedgerObjNestedField();
    }
};

BEAST_DEFINE_TESTSUITE(WasmHostFuncImpl, app, ripple);

}  // namespace test
}  // namespace ripple
