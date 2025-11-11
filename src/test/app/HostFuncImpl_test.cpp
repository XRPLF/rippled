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

#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/digest.h>

namespace ripple {
namespace test {

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
toBytes(Asset const& asset)
{
    if (asset.holds<Issue>())
    {
        Serializer s;
        auto const& issue = asset.get<Issue>();
        s.addBitString(issue.currency);
        if (!isXRP(issue.currency))
            s.addBitString(issue.account);
        auto const data = s.getData();
        return data;
    }

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

struct HostFuncImpl_test : public beast::unit_test::suite
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
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        {
            OpenView ov{*env.current()};
            ApplyContext ac = createApplyContext(env, ov);
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);
            auto const result = hfs.getParentLedgerTime();
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(
                    result.value() ==
                    env.current()
                        ->parentCloseTime()
                        .time_since_epoch()
                        .count());
        }

        env.close(
            env.now() +
            std::chrono::seconds(std::numeric_limits<int32_t>::max() - 1));
        {
            OpenView ov{*env.current()};
            ApplyContext ac = createApplyContext(env, ov);
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);
            auto const result = hfs.getParentLedgerTime();
            if (BEAST_EXPECTS(
                    !result.has_value(), std::to_string(result.value())))
                BEAST_EXPECT(result.error() == HostFunctionError::INTERNAL);
        }
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
    testGetBaseFee()
    {
        testcase("getBaseFee");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const result = hfs.getBaseFee();
        if (BEAST_EXPECT(result.has_value()))
            BEAST_EXPECT(result.value() == env.current()->fees().base.drops());

        {
            Env env2(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.reference_fee =
                        static_cast<int64_t>(
                            std::numeric_limits<int32_t>::max()) +
                        1;
                    return cfg;
                }),
                testable_amendments());
            // Run past the flag ledger so that a Fee change vote occurs and
            // updates FeeSettings. (It also activates all supported
            // amendments.)
            for (auto i = env.current()->seq(); i <= 257; ++i)
                env.close();

            OpenView ov2{*env2.current()};
            ApplyContext ac2 = createApplyContext(env2, ov2);
            WasmHostFunctionsImpl hfs2(ac2, dummyEscrow);
            auto const result2 = hfs2.getBaseFee();
            if (BEAST_EXPECT(!result2.has_value()))
                BEAST_EXPECT(result2.error() == HostFunctionError::INTERNAL);
        }
    }

    void
    testIsAmendmentEnabled()
    {
        testcase("isAmendmentEnabled");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        // Use featureTokenEscrow for testing
        auto const amendmentId = featureTokenEscrow;

        // Test by id
        {
            auto const result = hfs.isAmendmentEnabled(amendmentId);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 1);
        }

        // Test by name
        std::string const amendmentName = "TokenEscrow";
        {
            auto const result = hfs.isAmendmentEnabled(amendmentName);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 1);
        }

        // Test with a fake amendment id (all zeros)
        uint256 fakeId;
        {
            auto const result = hfs.isAmendmentEnabled(fakeId);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 0);
        }

        // Test with a fake amendment name
        std::string fakeName = "FakeAmendment";
        {
            auto const result = hfs.isAmendmentEnabled(fakeName);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 0);
        }
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
            obj.setFieldArray(sfMemos, STArray{});
        });
        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);
            auto const account = hfs.getTxField(sfAccount);
            BEAST_EXPECT(
                account && std::ranges::equal(*account, env.master.id()));

            auto const owner = hfs.getTxField(sfOwner);
            BEAST_EXPECT(owner && std::ranges::equal(*owner, env.master.id()));

            auto const txType = hfs.getTxField(sfTransactionType);
            BEAST_EXPECT(txType && *txType == toBytes(ttESCROW_FINISH));

            auto const offerSeq = hfs.getTxField(sfOfferSequence);
            BEAST_EXPECT(offerSeq && *offerSeq == toBytes(env.seq(env.master)));

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

        {
            auto const iouAsset = env.master["USD"];
            STTx const stx2 = STTx(ttAMM_DEPOSIT, [&](auto& obj) {
                obj.setAccountID(sfAccount, env.master.id());
                obj.setFieldIssue(sfAsset, STIssue{sfAsset, xrpIssue()});
                obj.setFieldIssue(
                    sfAsset2, STIssue{sfAsset2, iouAsset.issue()});
            });
            ApplyContext ac2 = createApplyContext(env, ov, stx2);
            WasmHostFunctionsImpl hfs(ac2, dummyEscrow);

            auto const asset = hfs.getTxField(sfAsset);
            std::vector<std::uint8_t> expectedAsset(20, 0);
            BEAST_EXPECT(asset && *asset == expectedAsset);

            auto const asset2 = hfs.getTxField(sfAsset2);
            BEAST_EXPECT(asset2 && *asset2 == toBytes(Asset(iouAsset)));
        }

        {
            auto const iouAsset = env.master["GBP"];
            auto const mptId = makeMptID(1, env.master);
            STTx const stx2 = STTx(ttAMM_DEPOSIT, [&](auto& obj) {
                obj.setAccountID(sfAccount, env.master.id());
                obj.setFieldIssue(sfAsset, STIssue{sfAsset, iouAsset.issue()});
                obj.setFieldIssue(sfAsset2, STIssue{sfAsset2, MPTIssue{mptId}});
            });
            ApplyContext ac2 = createApplyContext(env, ov, stx2);
            WasmHostFunctionsImpl hfs(ac2, dummyEscrow);

            auto const asset = hfs.getTxField(sfAsset);
            if (BEAST_EXPECT(asset.has_value()))
            {
                BEAST_EXPECT(*asset == toBytes(Asset(iouAsset)));
            }

            auto const asset2 = hfs.getTxField(sfAsset2);
            if (BEAST_EXPECT(asset2.has_value()))
            {
                BEAST_EXPECT(*asset2 == toBytes(Asset(mptId)));
            }
        }

        {
            std::uint8_t const expectedScale = 8;
            STTx const stx2 = STTx(ttMPTOKEN_ISSUANCE_CREATE, [&](auto& obj) {
                obj.setAccountID(sfAccount, env.master.id());
                obj.setFieldU8(sfAssetScale, expectedScale);
            });
            ApplyContext ac2 = createApplyContext(env, ov, stx2);
            WasmHostFunctionsImpl hfs(ac2, dummyEscrow);

            auto const actualScale = hfs.getTxField(sfAssetScale);
            if (BEAST_EXPECT(actualScale.has_value()))
            {
                BEAST_EXPECT(
                    std::ranges::equal(*actualScale, toBytes(expectedScale)));
            }
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
            BEAST_EXPECT(std::ranges::equal(*account, env.master.id()));

        // Should return the Amount field from the escrow ledger object
        auto const amountField = hfs.getCurrentLedgerObjField(sfAmount);
        if (BEAST_EXPECT(amountField.has_value()))
        {
            BEAST_EXPECT(*amountField == toBytes(XRP(100)));
        }

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
        auto const escrowKeylet =
            keylet::escrow(env.master.id(), env.seq(env.master) - 1);
        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        // Cache the escrow ledger object in slot 1
        auto cacheResult = hfs.cacheLedgerObj(accountKeylet.key, 1);
        BEAST_EXPECT(cacheResult.has_value() && cacheResult.value() == 1);

        // Should return the Account field from the cached ledger object
        auto const account = hfs.getLedgerObjField(1, sfAccount);
        if (BEAST_EXPECTS(
                account.has_value(),
                std::to_string(static_cast<int>(account.error()))))
            BEAST_EXPECT(std::ranges::equal(*account, env.master.id()));

        // Should return the Balance field from the cached ledger object
        auto const balanceField = hfs.getLedgerObjField(1, sfBalance);
        if (BEAST_EXPECT(balanceField.has_value()))
        {
            BEAST_EXPECT(*balanceField == toBytes(env.balance(env.master)));
        }

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
                BEAST_EXPECT(std::ranges::equal(*account, env.master.id()));
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
            BEAST_EXPECT(*signerQuorum == toBytes(static_cast<uint32_t>(2)));
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
                BEAST_EXPECT(std::ranges::equal(*result, alice.id()));
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
                BEAST_EXPECT(std::ranges::equal(*result2, becky.id()));
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
                auto const expected = toBytes(static_cast<std::uint16_t>(1));
                BEAST_EXPECT(*weightResult == expected);
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
                auto const expected = toBytes(static_cast<std::uint32_t>(2));
                BEAST_EXPECT(*quorumResult == expected);
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
        auto const result = hfs.updateData(Slice(data.data(), data.size()));
        BEAST_EXPECT(result && *result == data.size());

        // Should fail for too large data
        std::vector<uint8_t> bigData(
            1024 * 1024 + 1, 0x42);  // > maxWasmDataLength
        auto const tooBig =
            hfs.updateData(Slice(bigData.data(), bigData.size()));
        if (BEAST_EXPECT(!tooBig.has_value()))
            BEAST_EXPECT(
                tooBig.error() == HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    void
    testCheckSignature()
    {
        testcase("checkSignature");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        // Generate a keypair and sign a message
        auto const kp = generateKeyPair(KeyType::secp256k1, randomSeed());
        PublicKey const& pk = kp.first;
        SecretKey const& sk = kp.second;
        std::string const& message = "hello signature";
        auto const sig = sign(pk, sk, Slice(message.data(), message.size()));

        // Should succeed for valid signature
        {
            auto const result = hfs.checkSignature(
                Slice(message.data(), message.size()),
                Slice(sig.data(), sig.size()),
                Slice(pk.data(), pk.size()));
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 1);
        }

        // Should fail for invalid signature
        {
            std::string badSig(sig.size(), 0xFF);
            auto const result = hfs.checkSignature(
                Slice(message.data(), message.size()),
                Slice(badSig.data(), badSig.size()),
                Slice(pk.data(), pk.size()));
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 0);
        }

        // Should fail for invalid public key
        {
            std::string badPk(pk.size(), 0x00);
            auto const result = hfs.checkSignature(
                Slice(message.data(), message.size()),
                Slice(sig.data(), sig.size()),
                Slice(badPk.data(), badPk.size()));
            BEAST_EXPECT(!result.has_value());
            BEAST_EXPECT(result.error() == HostFunctionError::INVALID_PARAMS);
        }

        // Should fail for empty public key
        {
            auto const result = hfs.checkSignature(
                Slice(message.data(), message.size()),
                Slice(sig.data(), sig.size()),
                Slice(nullptr, 0));
            BEAST_EXPECT(!result.has_value());
            BEAST_EXPECT(result.error() == HostFunctionError::INVALID_PARAMS);
        }

        // Should fail for empty signature
        {
            auto const result = hfs.checkSignature(
                Slice(message.data(), message.size()),
                Slice(nullptr, 0),
                Slice(pk.data(), pk.size()));
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 0);
        }

        // Should fail for empty message
        {
            auto const result = hfs.checkSignature(
                Slice(nullptr, 0),
                Slice(sig.data(), sig.size()),
                Slice(pk.data(), pk.size()));
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result.value() == 0);
        }
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
        auto const result =
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
            return std::ranges::equal(bytes, kl.key);
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
#define COMPARE_KEYLET_FAIL(hfsFunc, expected, ...)            \
    {                                                          \
        auto actual = hfs.hfsFunc(__VA_ARGS__);                \
        if (BEAST_EXPECT(!actual.has_value()))                 \
        {                                                      \
            BEAST_EXPECTS(                                     \
                actual.error() == expected,                    \
                std::to_string(HfErrorToInt(actual.error()))); \
        }                                                      \
    }

        COMPARE_KEYLET(accountKeylet, keylet::account, env.master.id());
        COMPARE_KEYLET_FAIL(
            accountKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount());

        COMPARE_KEYLET(
            ammKeylet, keylet::amm, xrpIssue(), env.master["USD"].issue());
        COMPARE_KEYLET_FAIL(
            ammKeylet,
            HostFunctionError::INVALID_PARAMS,
            xrpIssue(),
            xrpIssue());
        COMPARE_KEYLET_FAIL(
            ammKeylet,
            HostFunctionError::INVALID_PARAMS,
            makeMptID(1, env.master.id()),
            xrpIssue());

        COMPARE_KEYLET(checkKeylet, keylet::check, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            checkKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount(), 1);

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
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            alice.id(),
            Slice(longCredType.data(), longCredType.size()));
        COMPARE_KEYLET_FAIL(
            credentialKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            alice.id(),
            Slice(credType.data(), credType.size()));
        COMPARE_KEYLET_FAIL(
            credentialKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount(),
            Slice(credType.data(), credType.size()));

        COMPARE_KEYLET(didKeylet, keylet::did, env.master.id());
        COMPARE_KEYLET_FAIL(
            didKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount());

        COMPARE_KEYLET(
            delegateKeylet, keylet::delegate, env.master.id(), alice.id());
        COMPARE_KEYLET_FAIL(
            delegateKeylet,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id());
        COMPARE_KEYLET_FAIL(
            delegateKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount());
        COMPARE_KEYLET_FAIL(
            delegateKeylet,
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
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id());
        COMPARE_KEYLET_FAIL(
            depositPreauthKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount());
        COMPARE_KEYLET_FAIL(
            depositPreauthKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id());

        COMPARE_KEYLET(escrowKeylet, keylet::escrow, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            escrowKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount(), 1);

        Currency usd = to_currency("USD");
        COMPARE_KEYLET(
            lineKeylet, keylet::line, env.master.id(), alice.id(), usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id(),
            usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount(),
            usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id(),
            usd);
        COMPARE_KEYLET_FAIL(
            lineKeylet,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            alice.id(),
            to_currency(""));

        {
            auto actual = hfs.mptIssuanceKeylet(env.master.id(), 1);
            auto expected = keylet::mptIssuance(1, env.master.id());
            if (BEAST_EXPECT(actual.has_value()))
            {
                BEAST_EXPECT(compareKeylet(actual.value(), expected));
            }
        }
        {
            auto actual = hfs.mptIssuanceKeylet(xrpAccount(), 1);
            if (BEAST_EXPECT(!actual.has_value()))
                BEAST_EXPECT(
                    actual.error() == HostFunctionError::INVALID_ACCOUNT);
        }

        auto const sampleMPTID = makeMptID(1, env.master.id());
        COMPARE_KEYLET(mptokenKeylet, keylet::mptoken, sampleMPTID, alice.id());
        COMPARE_KEYLET_FAIL(
            mptokenKeylet,
            HostFunctionError::INVALID_PARAMS,
            MPTID{},
            alice.id());
        COMPARE_KEYLET_FAIL(
            mptokenKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            sampleMPTID,
            xrpAccount());

        COMPARE_KEYLET(nftOfferKeylet, keylet::nftoffer, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            nftOfferKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);

        COMPARE_KEYLET(offerKeylet, keylet::offer, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            offerKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount(), 1);

        COMPARE_KEYLET(oracleKeylet, keylet::oracle, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            oracleKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount(), 1);

        COMPARE_KEYLET(
            paychanKeylet, keylet::payChan, env.master.id(), alice.id(), 1);
        COMPARE_KEYLET_FAIL(
            paychanKeylet,
            HostFunctionError::INVALID_PARAMS,
            env.master.id(),
            env.master.id(),
            1);
        COMPARE_KEYLET_FAIL(
            paychanKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            env.master.id(),
            xrpAccount(),
            1);
        COMPARE_KEYLET_FAIL(
            paychanKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            env.master.id(),
            1);

        COMPARE_KEYLET(
            permissionedDomainKeylet,
            keylet::permissionedDomain,
            env.master.id(),
            1);
        COMPARE_KEYLET_FAIL(
            permissionedDomainKeylet,
            HostFunctionError::INVALID_ACCOUNT,
            xrpAccount(),
            1);

        COMPARE_KEYLET(signersKeylet, keylet::signers, env.master.id());
        COMPARE_KEYLET_FAIL(
            signersKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount());

        COMPARE_KEYLET(ticketKeylet, keylet::ticket, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            ticketKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount(), 1);

        COMPARE_KEYLET(vaultKeylet, keylet::vault, env.master.id(), 1);
        COMPARE_KEYLET_FAIL(
            vaultKeylet, HostFunctionError::INVALID_ACCOUNT, xrpAccount(), 1);
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
            auto const result = hfs.getNFT(alice.id(), nftId);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(std::ranges::equal(*result, uri));
        }

        // Should fail for invalid account
        {
            auto const result = hfs.getNFT(xrpAccount(), nftId);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECT(
                    result.error() == HostFunctionError::INVALID_ACCOUNT);
        }

        // Should fail for invalid nftId
        {
            auto const result = hfs.getNFT(alice.id(), uint256());
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECT(
                    result.error() == HostFunctionError::INVALID_PARAMS);
        }

        // Should fail for invalid nftId
        {
            auto const badId = token::getNextID(env, alice, 0u, 1u);
            auto const result = hfs.getNFT(alice.id(), badId);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECT(
                    result.error() == HostFunctionError::LEDGER_OBJ_NOT_FOUND);
        }

        {
            auto const result = hfs.getNFT(alice.id(), nftId2);
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FIELD_NOT_FOUND);
        }
    }

    void
    testGetNFTIssuer()
    {
        testcase("getNFTIssuer");
        using namespace test::jtx;

        Env env{*this};
        // Mint NFT for env.master
        uint32_t const taxon = 12345;
        uint256 const nftId = token::getNextID(env, env.master, taxon);
        env(token::mint(env.master, taxon));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        // Should succeed for valid NFT id
        {
            auto const result = hfs.getNFTIssuer(nftId);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(std::ranges::equal(*result, env.master.id()));
        }

        // Should fail for zero NFT id
        {
            auto const result = hfs.getNFTIssuer(uint256());
            if (BEAST_EXPECT(!result.has_value()))
                BEAST_EXPECT(
                    result.error() == HostFunctionError::INVALID_PARAMS);
        }
    }

    void
    testGetNFTTaxon()
    {
        testcase("getNFTTaxon");
        using namespace test::jtx;

        Env env{*this};

        uint32_t const taxon = 54321;
        uint256 const nftId = token::getNextID(env, env.master, taxon);
        env(token::mint(env.master, taxon));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const result = hfs.getNFTTaxon(nftId);
        if (BEAST_EXPECT(result.has_value()))
            BEAST_EXPECT(result.value() == taxon);
    }

    void
    testGetNFTFlags()
    {
        testcase("getNFTFlags");
        using namespace test::jtx;

        Env env{*this};

        // Mint NFT with default flags
        uint256 const nftId =
            token::getNextID(env, env.master, 0u, tfTransferable);
        env(token::mint(env.master, 0), txflags(tfTransferable));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.getNFTFlags(nftId);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(result.value() == tfTransferable);
        }

        // Should return 0 for zero NFT id
        {
            auto const result = hfs.getNFTFlags(uint256());
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(result.value() == 0);
        }
    }

    void
    testGetNFTTransferFee()
    {
        testcase("getNFTTransferFee");
        using namespace test::jtx;

        Env env{*this};

        uint16_t const transferFee = 250;
        uint256 const nftId =
            token::getNextID(env, env.master, 0u, tfTransferable, transferFee);
        env(token::mint(env.master, 0),
            token::xferFee(transferFee),
            txflags(tfTransferable));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.getNFTTransferFee(nftId);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(result.value() == transferFee);
        }

        // Should return 0 for zero NFT id
        {
            auto const result = hfs.getNFTTransferFee(uint256());
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(result.value() == 0);
        }
    }

    void
    testGetNFTSerial()
    {
        testcase("getNFTSerial");
        using namespace test::jtx;

        Env env{*this};

        // Mint NFT with serial 0
        uint256 const nftId = token::getNextID(env, env.master, 0u);
        auto const serial = env.seq(env.master);
        env(token::mint(env.master));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.getNFTSerial(nftId);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(result.value() == serial);
        }

        // Should return 0 for zero NFT id
        {
            auto const result = hfs.getNFTSerial(uint256());
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(result.value() == 0);
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
        auto const result = hfs.trace(msg, slice, false);
        BEAST_EXPECT(result.has_value());
        BEAST_EXPECT(result.value() == msg.size() + data.size());

        auto const resultHex = hfs.trace(msg, slice, true);
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
        auto const result = hfs.traceNum(msg, num);
        BEAST_EXPECT(result.has_value());
        BEAST_EXPECT(result.value() == msg.size() + sizeof(num));
    }

    void
    testTraceAccount()
    {
        testcase("traceAccount");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        std::string msg = "trace account";
        // Valid account
        {
            auto const result = hfs.traceAccount(msg, env.master.id());
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(
                    result.value() ==
                    msg.size() + toBase58(env.master.id()).size());
        }
    }

    void
    testTraceAmount()
    {
        testcase("traceAmount");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        std::string msg = "trace amount";
        STAmount amount = XRP(12345);
        {
            auto const result = hfs.traceAmount(msg, amount);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(
                    result.value() == msg.size() + amount.getFullText().size());
        }

        // IOU amount
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();
        STAmount iouAmount = env.master["USD"](100);
        {
            auto const result = hfs.traceAmount(msg, iouAmount);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(
                    result.value() ==
                    msg.size() + iouAmount.getFullText().size());
        }

        // MPT amount
        {
            auto const mptId = makeMptID(42, env.master.id());
            Asset mptAsset = Asset(mptId);
            STAmount mptAmount(mptAsset, 123456);
            auto const result = hfs.traceAmount(msg, mptAmount);
            if (BEAST_EXPECT(result.has_value()))
                BEAST_EXPECT(
                    result.value() ==
                    msg.size() + mptAmount.getFullText().size());
        }
    }

    // clang-format off

    int const normalExp = 15;

    Bytes const floatIntMin        =  {0x99, 0x20, 0xc4, 0x9b, 0xa5, 0xe3, 0x53, 0xf8};  // -2^63
    Bytes const floatIntZero       =  {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // 0
    Bytes const floatIntMax        =  {0xd9, 0x20, 0xc4, 0x9b, 0xa5, 0xe3, 0x53, 0xf8};  // 2^63-1
    Bytes const floatUIntMax       =  {0xd9, 0x46, 0x8d, 0xb8, 0xba, 0xc7, 0x10, 0xcb};  // 2^64
    Bytes const floatMaxExp        =  {0xEC, 0x43, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // 1e(80+15)
    Bytes const floatPreMaxExp     =  {0xEC, 0x03, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // 1e(79+15)
    Bytes const floatMinusMaxExp   =  {0xAC, 0x43, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // -1e(80+15)
    Bytes const floatMaxIOU        =  {0xEC, 0x63, 0x86, 0xF2, 0x6F, 0xC0, 0xFF, 0xFF};  // 1e(81+15)-1
    Bytes const floatMinExp        =  {0xC0, 0x43, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // 1e-96
    Bytes const float1             =  {0xD4, 0x83, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // 1
    Bytes const floatMinus1        =  {0x94, 0x83, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // -1
    Bytes const float1More         =  {0xD4, 0x83, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x01};  // 1.000 000 000 000 001
    Bytes const float2             =  {0xD4, 0x87, 0x1A, 0xFD, 0x49, 0x8D, 0x00, 0x00};  // 2
    Bytes const float10            =  {0xD4, 0xC3, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00};  // 10
    Bytes const floatInvalidZero   =  {0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // INVALID
    Bytes const floatPi            =  {0xD4, 0x8B, 0x29, 0x43, 0x0A, 0x25, 0x6D, 0x21};  // 3.141592653589793

    std::string const invalid = "invalid_data";

    // clang-format on

    void
    testFloatTrace()
    {
        testcase("FloatTrace");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        std::string msg = "trace float";

        {
            auto const result = hfs.traceFloat(msg, makeSlice(invalid));
            BEAST_EXPECT(
                result &&
                *result ==
                    msg.size() + 14 /* error msg size*/ + invalid.size() * 2);
        }

        {
            auto const result = hfs.traceFloat(msg, makeSlice(floatMaxExp));
            BEAST_EXPECT(
                result && *result == msg.size() + 19 /* string represenation*/);
        }
    }

    void
    testFloatFromInt()
    {
        testcase("FloatFromInt");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result =
                hfs.floatFromInt(std::numeric_limits<int64_t>::min(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatFromInt(std::numeric_limits<int64_t>::min(), 4);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatFromInt(std::numeric_limits<int64_t>::min(), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntMin);
        }

        {
            auto const result = hfs.floatFromInt(0, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }

        {
            auto const result =
                hfs.floatFromInt(std::numeric_limits<int64_t>::max(), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntMax);
        }
    }

    void
    testFloatFromUint()
    {
        testcase("FloatFromUint");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result =
                hfs.floatFromUint(std::numeric_limits<uint64_t>::min(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatFromUint(std::numeric_limits<uint64_t>::min(), 4);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatFromUint(0, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }

        {
            auto const result =
                hfs.floatFromUint(std::numeric_limits<uint64_t>::max(), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatUIntMax);
        }
    }

    void
    testFloatSet()
    {
        testcase("FloatSet");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatSet(1, 0, -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatSet(1, 0, 4);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatSet(1, Number::maxExponent + normalExp + 1, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result =
                hfs.floatSet(1, IOUAmount::maxExponent + normalExp + 1, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result =
                hfs.floatSet(1, IOUAmount::minExponent + normalExp - 1, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }

        {
            auto const result =
                hfs.floatSet(1, IOUAmount::maxExponent + normalExp, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMaxExp);
        }

        {
            auto const result =
                hfs.floatSet(-1, IOUAmount::maxExponent + normalExp, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMinusMaxExp);
        }

        {
            auto const result =
                hfs.floatSet(1, IOUAmount::maxExponent + normalExp - 1, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatPreMaxExp);
        }

        {
            auto const result =
                hfs.floatSet(IOUAmount::maxMantissa, IOUAmount::maxExponent, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMaxIOU);
        }

        {
            auto const result =
                hfs.floatSet(1, IOUAmount::minExponent + normalExp, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMinExp);
        }

        {
            auto const result = hfs.floatSet(10, -1, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == float1);
        }
    }

    void
    testFloatCompare()
    {
        testcase("FloatCompare");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatCompare(Slice(), Slice());
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatCompare(makeSlice(floatInvalidZero), Slice());
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatCompare(makeSlice(float1), makeSlice(invalid));
            BEAST_EXPECT(
                !result &&
                result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto x = floatMaxExp;
            // exp = 81 + 97 = 178
            x[1] |= 0x80;
            x[1] &= 0xBF;
            auto const result =
                hfs.floatCompare(makeSlice(x), makeSlice(floatMaxExp));
            BEAST_EXPECT(
                !result &&
                result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatCompare(
                makeSlice(floatIntMin), makeSlice(floatIntZero));
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == 2);
        }

        {
            auto const result = hfs.floatCompare(
                makeSlice(floatIntMax), makeSlice(floatIntZero));
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == 1);
        }

        {
            auto const result =
                hfs.floatCompare(makeSlice(float1), makeSlice(float1));
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == 0);
        }
    }

    void
    testFloatAdd()
    {
        testcase("floatAdd");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatAdd(Slice(), Slice(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatAdd(Slice(), Slice(), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatAdd(makeSlice(float1), makeSlice(invalid), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatAdd(makeSlice(floatMaxIOU), makeSlice(floatMaxExp), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result = hfs.floatAdd(
                makeSlice(floatIntMin), makeSlice(floatIntZero), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntMin);
        }

        {
            auto const result =
                hfs.floatAdd(makeSlice(floatIntMax), makeSlice(floatIntMin), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }
    }

    void
    testFloatSubtract()
    {
        testcase("floatSubtract");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatSubtract(Slice(), Slice(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatSubtract(Slice(), Slice(), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatSubtract(makeSlice(float1), makeSlice(invalid), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatSubtract(
                makeSlice(floatMaxIOU), makeSlice(floatMinusMaxExp), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result = hfs.floatSubtract(
                makeSlice(floatIntMin), makeSlice(floatIntZero), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntMin);
        }

        {
            auto const result = hfs.floatSubtract(
                makeSlice(floatIntZero), makeSlice(float1), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMinus1);
        }
    }

    void
    testFloatMultiply()
    {
        testcase("floatMultiply");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatMultiply(Slice(), Slice(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatMultiply(Slice(), Slice(), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatMultiply(makeSlice(float1), makeSlice(invalid), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatMultiply(
                makeSlice(floatMaxIOU), makeSlice(float1More), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result =
                hfs.floatMultiply(makeSlice(float1), makeSlice(float1), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == float1);
        }

        {
            auto const result = hfs.floatMultiply(
                makeSlice(floatIntZero), makeSlice(floatMaxIOU), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }

        {
            auto const result = hfs.floatMultiply(
                makeSlice(float10), makeSlice(floatPreMaxExp), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMaxExp);
        }
    }

    void
    testFloatDivide()
    {
        testcase("floatDivide");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatDivide(Slice(), Slice(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatDivide(Slice(), Slice(), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatDivide(makeSlice(float1), makeSlice(invalid), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatDivide(makeSlice(float1), makeSlice(floatIntZero), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const y = hfs.floatSet(
                IOUAmount::maxMantissa, -normalExp - 1, 0);  // 0.9999999...
            if (BEAST_EXPECT(y))
            {
                auto const result =
                    hfs.floatDivide(makeSlice(floatMaxIOU), makeSlice(*y), 0);
                BEAST_EXPECT(!result) &&
                    BEAST_EXPECT(
                        result.error() ==
                        HostFunctionError::FLOAT_COMPUTATION_ERROR);
            }
        }

        {
            auto const result =
                hfs.floatDivide(makeSlice(floatIntZero), makeSlice(float1), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }

        {
            auto const result =
                hfs.floatDivide(makeSlice(floatMaxExp), makeSlice(float10), 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatPreMaxExp);
        }
    }

    void
    testFloatRoot()
    {
        testcase("floatRoot");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatRoot(Slice(), 2, -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatRoot(makeSlice(invalid), 3, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatRoot(makeSlice(float1), -2, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatRoot(makeSlice(floatIntZero), 2, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatIntZero);
        }

        {
            auto const result = hfs.floatRoot(makeSlice(floatMaxIOU), 1, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMaxIOU);
        }

        {
            auto const x = hfs.floatSet(100, 0, 0);  // 100
            if (BEAST_EXPECT(x))
            {
                auto const result = hfs.floatRoot(makeSlice(*x), 2, 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == float10);
            }
        }

        {
            auto const x = hfs.floatSet(1000, 0, 0);  // 1000
            if (BEAST_EXPECT(x))
            {
                auto const result = hfs.floatRoot(makeSlice(*x), 3, 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == float10);
            }
        }

        {
            auto const x = hfs.floatSet(1, -2, 0);  // 0.01
            auto const y = hfs.floatSet(1, -1, 0);  // 0.1
            if (BEAST_EXPECT(x && y))
            {
                auto const result = hfs.floatRoot(makeSlice(*x), 2, 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == *y);
            }
        }
    }

    void
    testFloatPower()
    {
        testcase("floatPower");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatPower(Slice(), 2, -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatPower(makeSlice(invalid), 3, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatPower(makeSlice(float1), -2, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatPower(makeSlice(floatMaxIOU), 2, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result = hfs.floatPower(makeSlice(floatMaxIOU), 81, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatPower(makeSlice(floatMaxIOU), 2, 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() ==
                    HostFunctionError::FLOAT_COMPUTATION_ERROR);
        }

        {
            auto const result = hfs.floatPower(makeSlice(floatMaxIOU), 0, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == float1);
        }

        {
            auto const result = hfs.floatPower(makeSlice(floatMaxIOU), 1, 0);
            BEAST_EXPECT(result) && BEAST_EXPECT(*result == floatMaxIOU);
        }

        {
            auto const x = hfs.floatSet(100, 0, 0);  // 100
            if (BEAST_EXPECT(x))
            {
                auto const result = hfs.floatPower(makeSlice(float10), 2, 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == *x);
            }
        }

        {
            auto const x = hfs.floatSet(1, -1, 0);  // 0.1
            auto const y = hfs.floatSet(1, -2, 0);  // 0.01
            if (BEAST_EXPECT(x && y))
            {
                auto const result = hfs.floatPower(makeSlice(*x), 2, 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == *y);
            }
        }
    }

    void
    testFloatLog()
    {
        testcase("floatLog");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        {
            auto const result = hfs.floatLog(Slice(), -1);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result = hfs.floatLog(makeSlice(invalid), 0);
            BEAST_EXPECT(!result) &&
                BEAST_EXPECT(
                    result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        // perf test logs
        // {
        //     auto const result = hfs.floatLog(makeSlice(floatPi), 0);
        //     if (BEAST_EXPECT(result))
        //     {
        //         std::cout << "lg(" << floatToString(makeSlice(floatPi))
        //                   << ") = " << floatToString(makeSlice(*result))
        //                   << std::endl;
        //     }
        // }
        // {
        //     auto const result = hfs.floatLog(makeSlice(floatIntMax), 0);
        //     if (BEAST_EXPECT(result))
        //     {
        //         std::cout << "lg(" << floatToString(makeSlice(floatIntMax))
        //                   << ") = " << floatToString(makeSlice(*result))
        //                   << std::endl;
        //     }
        // }

        // {
        //     auto const result = hfs.floatLog(makeSlice(floatMaxExp), 0);
        //     if (BEAST_EXPECT(result))
        //     {
        //         std::cout << "lg(" << floatToString(makeSlice(floatMaxExp))
        //                   << ") = " << floatToString(makeSlice(*result))
        //                   << std::endl;
        //     }
        // }

        // {
        //     auto const result = hfs.floatLog(makeSlice(floatMaxIOU), 0);
        //     if (BEAST_EXPECT(result))
        //     {
        //         std::cout << "lg(" << floatToString(makeSlice(floatMaxIOU))
        //                   << ") = " << floatToString(makeSlice(*result))
        //                   << std::endl;
        //     }
        // }

        {
            auto const x =
                hfs.floatSet(9'500'000'000'000'001, -14, 0);  // almost 80+15
            if (BEAST_EXPECT(x))
            {
                auto const result = hfs.floatLog(makeSlice(floatMaxExp), 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == *x);
            }
        }

        {
            auto const x = hfs.floatSet(100, 0, 0);  // 100
            if (BEAST_EXPECT(x))
            {
                auto const result = hfs.floatLog(makeSlice(*x), 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == float2);
            }
        }

        {
            auto const x = hfs.floatSet(1000, 0, 0);  // 1000
            auto const y = hfs.floatSet(3, 0, 0);     // 0.1
            if (BEAST_EXPECT(x && y))
            {
                auto const result = hfs.floatLog(makeSlice(*x), 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == *y);
            }
        }

        {
            auto const x = hfs.floatSet(1, -2, 0);  // 0.01
            auto const y =
                hfs.floatSet(-1999999993734431, -15, 0);  // almost -2
            if (BEAST_EXPECT(x && y))
            {
                auto const result = hfs.floatLog(makeSlice(*x), 0);
                BEAST_EXPECT(result) && BEAST_EXPECT(*result == *y);
            }
        }
    }

    void
    testFloatNonIOU()
    {
        testcase("float Xrp+Mpt");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto const y = hfs.floatSet(20, 0, 0);
        if (!BEAST_EXPECT(y))
            return;

        Bytes x(8);

        // XRP
        memset(x.data(), 0, x.size());
        x[0] = 0x40;
        x[7] = 10;

        {
            auto const result =
                hfs.floatCompare(makeSlice(x), makeSlice(float10));
            BEAST_EXPECT(
                !result &&
                result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatAdd(makeSlice(float10), makeSlice(x), 0);
            BEAST_EXPECT(
                !result &&
                result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        // MPT
        memset(x.data(), 0, x.size());
        x[0] = 0x60;
        x[7] = 10;

        {
            auto const result =
                hfs.floatCompare(makeSlice(x), makeSlice(float10));
            BEAST_EXPECT(
                !result &&
                result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }

        {
            auto const result =
                hfs.floatAdd(makeSlice(float10), makeSlice(x), 0);
            BEAST_EXPECT(
                !result &&
                result.error() == HostFunctionError::FLOAT_INPUT_MALFORMED);
        }
    }

    void
    testFloats()
    {
        testFloatTrace();
        testFloatFromInt();
        testFloatFromUint();
        testFloatSet();
        testFloatCompare();
        testFloatAdd();
        testFloatSubtract();
        testFloatMultiply();
        testFloatDivide();
        testFloatRoot();
        testFloatPower();
        testFloatLog();
        testFloatNonIOU();
    }

    void
    run() override
    {
        testGetLedgerSqn();
        testGetParentLedgerTime();
        testGetParentLedgerHash();
        testGetBaseFee();
        testIsAmendmentEnabled();
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
        testCheckSignature();
        testComputeSha512HalfHash();
        testKeyletFunctions();
        testGetNFT();
        testGetNFTIssuer();
        testGetNFTTaxon();
        testGetNFTFlags();
        testGetNFTTransferFee();
        testGetNFTSerial();
        testTrace();
        testTraceNum();
        testTraceAccount();
        testTraceAmount();
        testFloats();
    }
};

BEAST_DEFINE_TESTSUITE(HostFuncImpl, app, ripple);

}  // namespace test
}  // namespace ripple
