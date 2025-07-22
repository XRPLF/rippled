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
    ApplyContext ac{
        env.app(),
        ov,
        tx,
        tesSUCCESS,
        env.current()->fees().base,
        tapNONE,
        env.journal};
    return ac;
}

struct WasmHostFuncImpl_test : public beast::unit_test::suite
{
    void
    testGetLedgerSqn()
    {
        testcase("getLedgerSqn");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const result = hfs.getLedgerSqn();
        if (BEAST_EXPECT(result.has_value()))
            BEAST_EXPECT(result.value() == env.current()->info().seq);
    }

    void
    testGetParentLedgerTime()
    {
        testcase("getParentLedgerTime");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const result = hfs.getParentLedgerTime();
        if (BEAST_EXPECT(result.has_value()))
            BEAST_EXPECT(
                result.value() ==
                env.current()->parentCloseTime().time_since_epoch().count());
    }

    void
    testGetParentLedgerHash()
    {
        testcase("getParentLedgerHash");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const result = hfs.getParentLedgerHash();
        if (BEAST_EXPECT(result.has_value()))
            BEAST_EXPECT(result.value() == env.current()->info().parentHash);
    }
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

            auto const nonField = hfs.getTxField(sfInvalid);
            if (BEAST_EXPECT(!nonField.has_value()))
                BEAST_EXPECT(
                    nonField.error() == HostFunctionError::FIELD_NOT_FOUND);

            auto const nonField2 = hfs.getTxField(sfGeneric);
            if (BEAST_EXPECT(!nonField2.has_value()))
                BEAST_EXPECT(
                    nonField2.error() == HostFunctionError::FIELD_NOT_FOUND);
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

        {
            auto const dummyEscrow =
                keylet::escrow(env.master, env.seq(env.master) + 5);
            WasmHostFunctionsImpl hfs2(ac, dummyEscrow);
            auto const account = hfs2.getCurrentLedgerObjField(sfAccount);
            if (BEAST_EXPECT(!account.has_value()))
            {
                BEAST_EXPECT(
                    account.error() == HostFunctionError::LEDGER_OBJ_NOT_FOUND);
            }
        }
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
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        // Find the signer ledger object
        auto const signerKeylet = keylet::signers(env.master.id());
        BEAST_EXPECT(env.le(signerKeylet));

        WasmHostFunctionsImpl hfs(ac, signerKeylet);

        // Locator for base field
        std::vector<int32_t> baseLocator = {sfSignerQuorum.fieldCode};
        Slice baseLocatorSlice(
            reinterpret_cast<uint8_t const*>(baseLocator.data()),
            baseLocator.size() * sizeof(int32_t));
        auto const signerQuorum =
            hfs.getCurrentLedgerObjNestedField(baseLocatorSlice);
        if (BEAST_EXPECTS(
                signerQuorum.has_value(),
                std::to_string(static_cast<int>(signerQuorum.error()))))
        {
            BEAST_EXPECT(std::equal(
                signerQuorum.value().begin(),
                signerQuorum.value().end(),
                toBytes(static_cast<std::uint32_t>(2)).begin()));
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
            {sfSignerQuorum
                 .fieldCode,  // sfSignerQuorum is not an array or object
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

        {
            auto const dummyEscrow =
                keylet::escrow(env.master, env.seq(env.master) + 5);
            WasmHostFunctionsImpl dummyHfs(ac, dummyEscrow);
            std::vector<int32_t> const locatorVec = {sfAccount.fieldCode};
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result =
                dummyHfs.getCurrentLedgerObjNestedField(locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == HostFunctionError::LEDGER_OBJ_NOT_FOUND,
                    std::to_string(static_cast<int>(result.error())));
        }
    }

    void
    testGetLedgerObjNestedField()
    {
        testcase("getLedgerObjNestedField");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        // Cache the SignerList ledger object in slot 1
        auto const signerListKeylet = keylet::signers(env.master.id());
        auto cacheResult = hfs.cacheLedgerObj(signerListKeylet.key, 1);
        BEAST_EXPECT(cacheResult.has_value() && cacheResult.value() == 1);

        // Locator for sfSignerEntries[0].sfAccount
        {
            std::vector<int32_t> const locatorVec = {
                sfSignerEntries.fieldCode, 0, sfAccount.fieldCode};
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));

            auto const result = hfs.getLedgerObjNestedField(1, locator);
            if (BEAST_EXPECTS(
                    result.has_value(),
                    std::to_string(static_cast<int>(result.error()))))
            {
                BEAST_EXPECT(std::equal(
                    result.value().begin(),
                    result.value().end(),
                    alice.id().data()));
            }
        }

        // Locator for sfSignerEntries[1].sfAccount
        {
            std::vector<int32_t> const locatorVec = {
                sfSignerEntries.fieldCode, 1, sfAccount.fieldCode};
            Slice const locator = Slice(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result2 = hfs.getLedgerObjNestedField(1, locator);
            if (BEAST_EXPECTS(
                    result2.has_value(),
                    std::to_string(static_cast<int>(result2.error()))))
            {
                BEAST_EXPECT(std::equal(
                    result2.value().begin(),
                    result2.value().end(),
                    becky.id().data()));
            }
        }

        // Locator for sfSignerEntries[0].sfSignerWeight
        {
            std::vector<int32_t> const locatorVec = {
                sfSignerEntries.fieldCode, 0, sfSignerWeight.fieldCode};
            Slice const locator = Slice(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const weightResult = hfs.getLedgerObjNestedField(1, locator);
            if (BEAST_EXPECTS(
                    weightResult.has_value(),
                    std::to_string(static_cast<int>(weightResult.error()))))
            {
                // Should be 1
                std::array<std::uint8_t, 2> expected =
                    toBytes(static_cast<std::uint16_t>(1));
                BEAST_EXPECT(std::equal(
                    weightResult.value().begin(),
                    weightResult.value().end(),
                    expected.begin()));
            }
        }

        // Locator for base field sfSignerQuorum
        {
            std::vector<int32_t> const locatorVec = {sfSignerQuorum.fieldCode};
            Slice const locator = Slice(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const quorumResult = hfs.getLedgerObjNestedField(1, locator);
            if (BEAST_EXPECTS(
                    quorumResult.has_value(),
                    std::to_string(static_cast<int>(quorumResult.error()))))
            {
                std::array<std::uint8_t, 4> expected =
                    toBytes(static_cast<std::uint32_t>(2));
                BEAST_EXPECT(std::equal(
                    quorumResult.value().begin(),
                    quorumResult.value().end(),
                    expected.begin()));
            }
        }

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError,
                               int slot = 1) {
            Slice const locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result = hfs.getLedgerObjNestedField(slot, locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == expectedError,
                    std::to_string(static_cast<int>(result.error())));
        };

        // Error: base field not found
        expectError(
            {sfSigners.fieldCode,  // sfSigners does not exist
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // Error: index out of bounds
        expectError(
            {sfSignerEntries.fieldCode,
             2,  // index 2 does not exist
             sfAccount.fieldCode},
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // Error: nested field not found
        expectError(
            {
                sfSignerEntries.fieldCode,
                0,
                sfDestination.fieldCode  // sfDestination does not exist
            },
            HostFunctionError::FIELD_NOT_FOUND);

        // Error: invalid field code
        expectError(
            {field_code(99999, 99999), 0, sfAccount.fieldCode},
            HostFunctionError::INVALID_FIELD);

        // Error: invalid nested field code
        expectError(
            {sfSignerEntries.fieldCode, 0, field_code(99999, 99999)},
            HostFunctionError::INVALID_FIELD);

        // Error: slot out of range
        expectError(
            {sfSignerQuorum.fieldCode}, HostFunctionError::SLOT_OUT_RANGE, 0);
        expectError(
            {sfSignerQuorum.fieldCode}, HostFunctionError::SLOT_OUT_RANGE, 257);

        // Error: empty slot
        expectError(
            {sfSignerQuorum.fieldCode}, HostFunctionError::EMPTY_SLOT, 2);

        // Error: locator for STArray (not leaf field)
        expectError(
            {sfSignerEntries.fieldCode}, HostFunctionError::NOT_LEAF_FIELD);

        // Error: nesting into non-array/object field
        expectError(
            {sfSignerQuorum.fieldCode, 0, sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);

        // Error: empty locator
        expectError({}, HostFunctionError::LOCATOR_MALFORMED);

        // Error: locator malformed (not multiple of 4)
        std::vector<int32_t> const locatorVec = {sfSignerEntries.fieldCode};
        Slice const locator =
            Slice(reinterpret_cast<uint8_t const*>(locatorVec.data()), 3);
        auto const malformed = hfs.getLedgerObjNestedField(1, locator);
        BEAST_EXPECT(
            !malformed.has_value() &&
            malformed.error() == HostFunctionError::LOCATOR_MALFORMED);
    }

    void
    testGetTxArrayLen()
    {
        testcase("getTxArrayLen");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};

        // Transaction with an array field
        STTx stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            STArray memos;
            {
                STObject memoObj(sfMemo);
                memoObj.setFieldVL(sfMemoData, Slice("hello", 5));
                memos.push_back(memoObj);
            }
            {
                STObject memoObj(sfMemo);
                memoObj.setFieldVL(sfMemoData, Slice("world", 5));
                memos.push_back(memoObj);
            }
            obj.setFieldArray(sfMemos, memos);
        });

        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        // Should return 1 for sfMemos
        auto const memosLen = hfs.getTxArrayLen(sfMemos);
        if (BEAST_EXPECT(memosLen.has_value()))
            BEAST_EXPECT(memosLen.value() == 2);

        // Should return error for non-array field
        auto const notArray = hfs.getTxArrayLen(sfAccount);
        if (BEAST_EXPECT(!notArray.has_value()))
            BEAST_EXPECT(notArray.error() == HostFunctionError::NO_ARRAY);

        // Should return error for missing array field
        auto const missingArray = hfs.getTxArrayLen(sfSigners);
        if (BEAST_EXPECT(!missingArray.has_value()))
            BEAST_EXPECT(
                missingArray.error() == HostFunctionError::FIELD_NOT_FOUND);
    }

    void
    testGetCurrentLedgerObjArrayLen()
    {
        testcase("getCurrentLedgerObjArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const signerKeylet = keylet::signers(env.master.id());
        WasmHostFunctionsImpl hfs(ac, signerKeylet);

        auto const entriesLen =
            hfs.getCurrentLedgerObjArrayLen(sfSignerEntries);
        if (BEAST_EXPECT(entriesLen.has_value()))
            BEAST_EXPECT(entriesLen.value() == 2);

        auto const arrLen = hfs.getCurrentLedgerObjArrayLen(sfMemos);
        if (BEAST_EXPECT(!arrLen.has_value()))
            BEAST_EXPECT(arrLen.error() == HostFunctionError::FIELD_NOT_FOUND);

        // Should return NO_ARRAY for non-array field
        auto const notArray = hfs.getCurrentLedgerObjArrayLen(sfAccount);
        if (BEAST_EXPECT(!notArray.has_value()))
            BEAST_EXPECT(notArray.error() == HostFunctionError::NO_ARRAY);

        {
            auto const dummyEscrow =
                keylet::escrow(env.master, env.seq(env.master) + 5);
            WasmHostFunctionsImpl dummyHfs(ac, dummyEscrow);
            auto const len = dummyHfs.getCurrentLedgerObjArrayLen(sfMemos);
            if (BEAST_EXPECT(!len.has_value()))
                BEAST_EXPECT(
                    len.error() == HostFunctionError::LEDGER_OBJ_NOT_FOUND);
        }
    }

    void
    testGetLedgerObjArrayLen()
    {
        testcase("getLedgerObjArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const signerListKeylet = keylet::signers(env.master.id());
        auto cacheResult = hfs.cacheLedgerObj(signerListKeylet.key, 1);
        BEAST_EXPECT(cacheResult.has_value() && cacheResult.value() == 1);

        {
            auto const arrLen = hfs.getLedgerObjArrayLen(1, sfSignerEntries);
            if (BEAST_EXPECT(arrLen.has_value()))
                // Should return 2 for sfSignerEntries
                BEAST_EXPECT(arrLen.value() == 2);
        }
        {
            auto const arrLen = hfs.getLedgerObjArrayLen(0, sfSignerEntries);
            if (BEAST_EXPECT(!arrLen.has_value()))
                BEAST_EXPECT(
                    arrLen.error() == HostFunctionError::SLOT_OUT_RANGE);
        }

        {
            // Should return error for non-array field
            auto const notArray = hfs.getLedgerObjArrayLen(1, sfAccount);
            if (BEAST_EXPECT(!notArray.has_value()))
                BEAST_EXPECT(notArray.error() == HostFunctionError::NO_ARRAY);
        }

        {
            // Should return error for empty slot
            auto const emptySlot = hfs.getLedgerObjArrayLen(2, sfSignerEntries);
            if (BEAST_EXPECT(!emptySlot.has_value()))
                BEAST_EXPECT(
                    emptySlot.error() == HostFunctionError::EMPTY_SLOT);
        }

        {
            // Should return error for missing array field
            auto const missingArray = hfs.getLedgerObjArrayLen(1, sfMemos);
            if (BEAST_EXPECT(!missingArray.has_value()))
                BEAST_EXPECT(
                    missingArray.error() == HostFunctionError::FIELD_NOT_FOUND);
        }
    }

    void
    testGetTxNestedArrayLen()
    {
        testcase("getTxNestedArrayLen");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};

        STTx stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
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

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError,
                               int slot = 1) {
            Slice const locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result = hfs.getTxNestedArrayLen(locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == expectedError,
                    std::to_string(static_cast<int>(result.error())));
        };

        // Locator for sfMemos
        {
            std::vector<int32_t> locatorVec = {sfMemos.fieldCode};
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const arrLen = hfs.getTxNestedArrayLen(locator);
            BEAST_EXPECT(arrLen.has_value() && arrLen.value() == 1);
        }

        // Error: non-array field
        expectError({sfAccount.fieldCode}, HostFunctionError::NO_ARRAY);

        // Error: missing field
        expectError({sfSigners.fieldCode}, HostFunctionError::FIELD_NOT_FOUND);
    }

    void
    testGetCurrentLedgerObjNestedArrayLen()
    {
        testcase("getCurrentLedgerObjNestedArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const signerKeylet = keylet::signers(env.master.id());
        WasmHostFunctionsImpl hfs(ac, signerKeylet);

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError,
                               int slot = 1) {
            Slice const locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result = hfs.getCurrentLedgerObjNestedArrayLen(locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == expectedError,
                    std::to_string(static_cast<int>(result.error())));
        };

        // Locator for sfSignerEntries
        {
            std::vector<int32_t> locatorVec = {sfSignerEntries.fieldCode};
            Slice locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const arrLen = hfs.getCurrentLedgerObjNestedArrayLen(locator);
            BEAST_EXPECT(arrLen.has_value() && arrLen.value() == 2);
        }

        // Error: non-array field
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::NO_ARRAY);

        // Error: missing field
        expectError({sfSigners.fieldCode}, HostFunctionError::FIELD_NOT_FOUND);

        {
            auto const dummyEscrow =
                keylet::escrow(env.master, env.seq(env.master) + 5);
            WasmHostFunctionsImpl dummyHfs(ac, dummyEscrow);
            std::vector<int32_t> locatorVec = {sfAccount.fieldCode};
            Slice const locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result =
                dummyHfs.getCurrentLedgerObjNestedArrayLen(locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == HostFunctionError::LEDGER_OBJ_NOT_FOUND,
                    std::to_string(static_cast<int>(result.error())));
        }
    }

    void
    testGetLedgerObjNestedArrayLen()
    {
        testcase("getLedgerObjNestedArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const signerListKeylet = keylet::signers(env.master.id());
        auto cacheResult = hfs.cacheLedgerObj(signerListKeylet.key, 1);
        BEAST_EXPECT(cacheResult.has_value() && cacheResult.value() == 1);

        // Locator for sfSignerEntries
        std::vector<int32_t> locatorVec = {sfSignerEntries.fieldCode};
        Slice locator(
            reinterpret_cast<uint8_t const*>(locatorVec.data()),
            locatorVec.size() * sizeof(int32_t));
        auto const arrLen = hfs.getLedgerObjNestedArrayLen(1, locator);
        if (BEAST_EXPECT(arrLen.has_value()))
            BEAST_EXPECT(arrLen.value() == 2);

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError,
                               int slot = 1) {
            Slice const locator(
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));
            auto const result = hfs.getLedgerObjNestedArrayLen(slot, locator);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECTS(
                    result.error() == expectedError,
                    std::to_string(static_cast<int>(result.error())));
        };

        // Error: non-array field
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::NO_ARRAY);

        // Error: missing field
        expectError({sfSigners.fieldCode}, HostFunctionError::FIELD_NOT_FOUND);

        // Slot out of range
        expectError(locatorVec, HostFunctionError::SLOT_OUT_RANGE, 0);
        expectError(locatorVec, HostFunctionError::SLOT_OUT_RANGE, 257);

        // Empty slot
        expectError(locatorVec, HostFunctionError::EMPTY_SLOT, 2);

        // Error: empty locator
        expectError({}, HostFunctionError::LOCATOR_MALFORMED);

        // Error: locator malformed (not multiple of 4)
        Slice malformedLocator(
            reinterpret_cast<uint8_t const*>(locator.data()), 3);
        auto const malformed =
            hfs.getLedgerObjNestedArrayLen(1, malformedLocator);
        BEAST_EXPECT(
            !malformed.has_value() &&
            malformed.error() == HostFunctionError::LOCATOR_MALFORMED);

        // Error: locator for non-STArray field
        expectError(
            {sfSignerQuorum.fieldCode, 0, sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);
    }

    void
    testUpdateData()
    {
        testcase("updateData");
        using namespace test::jtx;

        Env env{*this};
        env(escrow::create(env.master, env.master, XRP(100)),
            escrow::finish_time(env.now() + std::chrono::seconds(1)));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const escrowKeylet =
            keylet::escrow(env.master, env.seq(env.master) - 1);
        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        // Should succeed for small data
        std::vector<uint8_t> data(10, 0x42);
        auto result = hfs.updateData(Slice(data.data(), data.size()));
        BEAST_EXPECT(result.has_value() && result.value() == 0);

        // Should fail for too large data
        std::vector<uint8_t> bigData(
            1024 * 1024 + 1, 0x42);  // > maxWasmDataLength
        auto tooBig = hfs.updateData(Slice(bigData.data(), bigData.size()));
        if (BEAST_EXPECT(!tooBig.has_value()))
            BEAST_EXPECT(
                tooBig.error() == HostFunctionError::DATA_FIELD_TOO_LARGE);

        // Should fail if ledger object not found (use a bogus keylet)
        auto bogusKeylet = keylet::escrow(env.master, 999999);
        WasmHostFunctionsImpl hfs2(ac, bogusKeylet);
        auto notFound = hfs2.updateData(Slice(data.data(), data.size()));
        if (BEAST_EXPECT(!notFound.has_value()))
            BEAST_EXPECT(
                notFound.error() == HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    }

    void
    testComputeSha512HalfHash()
    {
        testcase("computeSha512HalfHash");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        std::string data = "hello world";
        auto result =
            hfs.computeSha512HalfHash(Slice(data.data(), data.size()));
        BEAST_EXPECT(result.has_value());

        // Should match direct call to sha512Half
        auto expected = sha512Half(Slice(data.data(), data.size()));
        BEAST_EXPECT(result.value() == expected);
    }

    void
    testKeyletFunctions()
    {
        testcase("keylet functions");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto compareKeylet = [](std::vector<uint8_t> const& bytes,
                                Keylet const& kl) {
            return bytes.size() == kl.key.size() &&
                std::equal(bytes.begin(), bytes.end(), kl.key.begin());
        };
// Lambda to compare a Bytes (std::vector<uint8_t>) to a keylet
#define COMPARE_KEYLET(hfsFunc, keyletFunc, ...)                   \
    {                                                              \
        auto actual = hfs.hfsFunc(__VA_ARGS__);                    \
        auto expected = keyletFunc(__VA_ARGS__);                   \
        if (BEAST_EXPECT(actual.has_value()))                      \
        {                                                          \
            BEAST_EXPECT(compareKeylet(actual.value(), expected)); \
        }                                                          \
    }
#define COMPARE_KEYLET_FAIL(hfsFunc, keyletFunc, expected, ...)        \
    {                                                                  \
        auto actual = hfs.hfsFunc(__VA_ARGS__);                        \
        if (BEAST_EXPECT(!actual.has_value()))                         \
        {                                                              \
            BEAST_EXPECTS(                                             \
                actual.error() == expected,                            \
                std::to_string(static_cast<int32_t>(actual.error()))); \
        }                                                              \
    }

        // accountKeylet
        COMPARE_KEYLET(accountKeylet, keylet::account, env.master.id());
        COMPARE_KEYLET_FAIL(
            accountKeylet,
            keylet::account,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount());
        COMPARE_KEYLET(checkKeylet, keylet::check, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            checkKeylet,
            keylet::check,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);
        std::string const credType = "test";
        COMPARE_KEYLET(
            credentialKeylet,
            keylet::credential,
            env.master.id(),
            env.master.id(),
            Slice(credType.data(), credType.size()));

        Account const alice("alice");
        constexpr std::string_view longCredType =
            "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
            "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p";
        static_assert(longCredType.size() > maxCredentialTypeLength);
        COMPARE_KEYLET_FAIL(
            credentialKeylet,
            keylet::credential,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            alice.id(),
            Slice(longCredType.data(), longCredType.size()));
        COMPARE_KEYLET_FAIL(
            credentialKeylet,
            keylet::credential,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            alice.id(),
            Slice(credType.data(), credType.size()));
        COMPARE_KEYLET_FAIL(
            credentialKeylet,
            keylet::credential,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount(),
            Slice(credType.data(), credType.size()));

        COMPARE_KEYLET(didKeylet, keylet::did, env.master.id());
        COMPARE_KEYLET_FAIL(
            didKeylet,
            keylet::did,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount());
        COMPARE_KEYLET(
            delegateKeylet, keylet::delegate, env.master.id(), alice.id());
        COMPARE_KEYLET_FAIL(
            delegateKeylet,
            keylet::delegate,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id());
        COMPARE_KEYLET_FAIL(
            delegateKeylet,
            keylet::delegate,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount());
        COMPARE_KEYLET_FAIL(
            delegateKeylet,
            keylet::delegate,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id());

        COMPARE_KEYLET(
            depositPreauthKeylet,
            keylet::depositPreauth,
            env.master.id(),
            alice.id());
        COMPARE_KEYLET_FAIL(
            depositPreauthKeylet,
            keylet::depositPreauth,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id());
        COMPARE_KEYLET_FAIL(
            depositPreauthKeylet,
            keylet::depositPreauth,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount());
        COMPARE_KEYLET_FAIL(
            depositPreauthKeylet,
            keylet::depositPreauth,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id());

        // escrowKeylet
        COMPARE_KEYLET(escrowKeylet, keylet::escrow, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            escrowKeylet,
            keylet::escrow,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);

        // lineKeylet
        Currency usd = to_currency("USD");
        COMPARE_KEYLET(
            lineKeylet, keylet::line, env.master.id(), alice.id(), usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            keylet::line,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id(),
            usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            keylet::line,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount(),
            usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            keylet::line,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id(),
            usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            keylet::line,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            alice.id(),
            to_currency(""));

        COMPARE_KEYLET(nftOfferKeylet, keylet::nftoffer, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            nftOfferKeylet,
            keylet::nftoffer,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);
        COMPARE_KEYLET(offerKeylet, keylet::offer, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            offerKeylet,
            keylet::offer,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);
        COMPARE_KEYLET(oracleKeylet, keylet::oracle, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            oracleKeylet,
            keylet::oracle,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);
        COMPARE_KEYLET(
            paychanKeylet, keylet::payChan, env.master.id(), alice.id(), 1);
        COMPARE_KEYLET_FAIL(
            paychanKeylet,
            keylet::payChan,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id(),
            1);
        COMPARE_KEYLET_FAIL(
            paychanKeylet,
            keylet::payChan,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount(),
            1);
        COMPARE_KEYLET_FAIL(
            paychanKeylet,
            keylet::payChan,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id(),
            1);

        COMPARE_KEYLET(signersKeylet, keylet::signers, env.master.id());
        COMPARE_KEYLET_FAIL(
            signersKeylet,
            keylet::signers,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount());
        COMPARE_KEYLET(ticketKeylet, keylet::ticket, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            ticketKeylet,
            keylet::ticket,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);
    }

    void
    testGetNFT()
    {
        testcase("getNFT");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();

        // Mint NFT for alice
        uint256 const nftId = token::getNextID(env, alice, 0u, 0u);
        std::string const uri = "https://example.com/nft";
        env(token::mint(alice), token::uri(uri));
        env.close();
        uint256 const nftId2 = token::getNextID(env, alice, 0u, 0u);
        env(token::mint(alice));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(alice, env.seq(alice));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        // Should succeed for valid NFT
        {
            auto nftResult = hfs.getNFT(alice.id(), nftId);
            if (BEAST_EXPECT(nftResult.has_value()))
                BEAST_EXPECT(std::equal(
                    nftResult.value().begin(),
                    nftResult.value().end(),
                    uri.data()));
        }

        // Should fail for invalid account
        {
            auto nftResult = hfs.getNFT(xrpAccount(), nftId);
            if (BEAST_EXPECT(!nftResult.has_value()))
                BEAST_EXPECT(
                    nftResult.error() == HostFunctionError::INVALID_ACCOUNT);
        }

        // Should fail for invalid nftId
        {
            auto nftResult = hfs.getNFT(alice.id(), uint256());
            if (BEAST_EXPECT(!nftResult.has_value()))
                BEAST_EXPECT(
                    nftResult.error() == HostFunctionError::INVALID_PARAMS);
        }

        // Should fail for invalid nftId
        {
            auto const badId = token::getNextID(env, alice, 0u, 1u);
            auto nftResult = hfs.getNFT(alice.id(), badId);
            if (BEAST_EXPECT(!nftResult.has_value()))
                BEAST_EXPECT(
                    nftResult.error() ==
                    HostFunctionError::LEDGER_OBJ_NOT_FOUND);
        }

        {
            auto nftResult = hfs.getNFT(alice.id(), nftId2);
            if (BEAST_EXPECT(!nftResult.has_value()))
                BEAST_EXPECT(
                    nftResult.error() == HostFunctionError::FIELD_NOT_FOUND);
        }
    }

    void
    testTrace()
    {
        testcase("trace");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        std::string msg = "test trace";
        std::string data = "abc";
        auto const slice = Slice(data.data(), data.size());
        auto result = hfs.trace(msg, slice, false);
        BEAST_EXPECT(result.has_value());
        BEAST_EXPECT(result.value() == msg.size() + data.size());

        auto resultHex = hfs.trace(msg, slice, true);
        BEAST_EXPECT(resultHex.has_value());
        BEAST_EXPECT(resultHex.value() == msg.size() + data.size() * 2);
    }

    void
    testTraceNum()
    {
        testcase("traceNum");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        std::string msg = "trace number";
        int64_t num = 123456789;
        auto result = hfs.traceNum(msg, num);
        BEAST_EXPECT(result.has_value());
        BEAST_EXPECT(result.value() == msg.size() + sizeof(num));
    }

    void
    run() override
    {
        testGetLedgerSqn();
        testGetParentLedgerTime();
        testGetParentLedgerHash();
        testCacheLedgerObj();
        testGetTxField();
        testGetCurrentLedgerObjField();
        testGetLedgerObjField();
        testGetTxNestedField();
        testGetCurrentLedgerObjNestedField();
        testGetLedgerObjNestedField();
        testGetTxArrayLen();
        testGetCurrentLedgerObjArrayLen();
        testGetLedgerObjArrayLen();
        testGetTxNestedArrayLen();
        testGetCurrentLedgerObjNestedArrayLen();
        testGetLedgerObjNestedArrayLen();
        testUpdateData();
        testComputeSha512HalfHash();
        testKeyletFunctions();
        testGetNFT();
        testTrace();
        testTraceNum();
    }
};

BEAST_DEFINE_TESTSUITE(WasmHostFuncImpl, app, ripple);

}  // namespace test
}  // namespace ripple
