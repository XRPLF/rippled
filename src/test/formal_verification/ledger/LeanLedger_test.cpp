#include <test/formal_verification/ffi/cpp/CppExterns.h>
#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/KeyletFFI.h>
#include <test/formal_verification/ledger/LedgerDataHelpers.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/digest.h>

#include <cstring>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace xrpl::test {

using namespace formal_verification;

class LeanLedger_test : public LedgerSuite
{
    static STAmount
    iou(uint8_t currencyByte, uint8_t issuerByte, std::uint64_t mantissa, int exponent)
    {
        Issue const issue{fillId<Currency>(currencyByte), fillId<AccountID>(issuerByte)};
        return STAmount{Asset{issue}, mantissa, exponent, false};
    }

    // Build a one-entry Lean ledger from `cppEntry`, read it back generically, and
    // assert the re-emitted SLE matches the original.
    template <class Builder, class Entry>
    void
    expectRoundtrip(Builder builder, Entry const& cppEntry, char const* label)
    {
        uint256 const key = cppEntry.getKey();
        LedgerFFI const ledger =
            LedgerFFIBuilder().add(builder.fromCpp(cppEntry).build(key)).build();
        auto const got = ledger.read(key);
        BEAST_EXPECT(got.has_value());
        if (got)
        {
            // The tag built from Entry::entryType must select the matching LedgerEntry
            if (got->code() != static_cast<uint16_t>(Entry::entryType))
            {
                fail(std::string(label) + ": entry code != entryType");
            }
            else
            {
                expectSameSle(*cppEntry.getSle(), *got->toSle(), label);
            }
        }
        BEAST_EXPECT(ledger.keys().size() == 1);
    }

    void
    testAccountRootRoundtrip()
    {
        beginCase("ledger.account_root_roundtrip");
        AccountID const account = fillId<AccountID>(0x11);
        STAmount const balance = iou(0x01, 0x02, 1234, -2);
        uint256 const key = keylet::account(account).key;
        uint256 const prevTxn = fillId<uint256>(0x12);
        uint256 const accountTxnID = fillId<uint256>(0x13);
        AccountID const regularKey = fillId<AccountID>(0x14);
        uint128 const emailHash = fillId<uint128>(0x15);
        uint256 const walletLocator = fillId<uint256>(0x16);
        Blob const messageKey{0xAA, 0xBB};
        Blob const domain{0xCC};
        AccountID const nftMinter = fillId<AccountID>(0x17);
        uint256 const ammID = fillId<uint256>(0x18);
        uint256 const vaultID = fillId<uint256>(0x22);
        uint256 const loanBrokerID = fillId<uint256>(0x19);

        auto const cppEntry =
            ledger_entries::AccountRootBuilder(account, 7, balance, 5, prevTxn, 99)
                .setFlags(lsfDefaultRipple)
                .setAccountTxnID(accountTxnID)
                .setRegularKey(regularKey)
                .setEmailHash(emailHash)
                .setWalletLocator(walletLocator)
                .setWalletSize(64)
                .setMessageKey(makeSlice(messageKey))
                .setTransferRate(500)
                .setDomain(makeSlice(domain))
                .setTickSize(10)
                .setTicketCount(3)
                .setNFTokenMinter(nftMinter)
                .setMintedNFTokens(11)
                .setBurnedNFTokens(22)
                .setFirstNFTokenSequence(33)
                .setAMMID(ammID)
                .setVaultID(vaultID)
                .setLoanBrokerID(loanBrokerID)
                .build(key);

        expectRoundtrip(AccountRootFFIBuilder(), cppEntry, "account_root");
    }

    void
    testCredentialRoundtrip()
    {
        beginCase("ledger.credential_roundtrip");
        AccountID const subject = fillId<AccountID>(0x56);
        AccountID const issuer = fillId<AccountID>(0x57);
        Blob const credentialType{0xDE, 0xAD, 0xBE, 0xEF};
        uint256 const key = keylet::credential(subject, issuer, makeSlice(credentialType)).key;

        auto const cppEntry =
            ledger_entries::CredentialBuilder(
                subject, issuer, makeSlice(credentialType), 4, fillId<uint256>(0x58), 77)
                .setFlags(lsfMPTCanTransfer)
                .setExpiration(999)
                .setURI(makeSlice(Blob{0x01, 0x02, 0x03}))
                .setSubjectNode(8)
                .build(key);

        expectRoundtrip(CredentialFFIBuilder(), cppEntry, "credential");
    }

    void
    testDepositPreauthRoundtrip()
    {
        beginCase("ledger.deposit_preauth_roundtrip");
        AccountID const account = fillId<AccountID>(0x60);
        AccountID const authorized = fillId<AccountID>(0x61);
        uint256 const prevTxn = fillId<uint256>(0x64);
        std::vector<std::pair<AccountID, Blob>> const creds{
            {fillId<AccountID>(0x62), Blob{0x01, 0x02}}, {fillId<AccountID>(0x63), Blob{0x03}}};

        // Keyed by authorized account.
        {
            uint256 const key = keylet::depositPreauth(account, authorized).key;
            auto const cppEntry = ledger_entries::DepositPreauthBuilder(account, 1, prevTxn, 44)
                                      .setFlags(0)
                                      .setAuthorize(authorized)
                                      .build(key);
            expectRoundtrip(DepositPreauthFFIBuilder(), cppEntry, "deposit_preauth.by_account");
        }

        // Keyed by the sorted credential leanSet.
        {
            std::set<std::pair<AccountID, Slice>> sortedCreds;
            for (auto const& [acc, blob] : creds)
                sortedCreds.emplace(acc, makeSlice(blob));
            uint256 const key = keylet::depositPreauth(account, sortedCreds).key;

            STArray arr(sfAuthorizeCredentials);
            for (auto const& [iss, ct] : creds)
            {
                STObject cred(sfCredential);
                cred.setAccountID(sfIssuer, iss);
                cred.setFieldVL(sfCredentialType, ct);
                arr.push_back(std::move(cred));
            }
            auto const cppEntry = ledger_entries::DepositPreauthBuilder(account, 2, prevTxn, 44)
                                      .setFlags(0)
                                      .setAuthorizeCredentials(arr)
                                      .build(key);
            expectRoundtrip(DepositPreauthFFIBuilder(), cppEntry, "deposit_preauth.by_credentials");
        }
    }

    void
    testMPTokenIssuanceRoundtrip()
    {
        beginCase("ledger.mptoken_issuance_roundtrip");
        AccountID const issuer = fillId<AccountID>(0x33);
        uint32_t const sequence = 7;
        uint256 const key = keylet::mptIssuance(makeMptID(sequence, issuer)).key;

        auto const cppEntry = ledger_entries::MPTokenIssuanceBuilder(
                                  issuer, sequence, 4, 1000, fillId<uint256>(0x34), 88)
                                  .setFlags(lsfMPTCanTransfer)
                                  .setTransferFee(50)
                                  .setAssetScale(2)
                                  .setMaximumAmount(9000)
                                  .setLockedAmount(30)
                                  .setMPTokenMetadata(makeSlice(Blob{0xFE, 0xED}))
                                  .setDomainID(fillId<uint256>(0x35))
                                  .setMutableFlags(0x12345678)
                                  .setReferenceHolding(fillId<uint256>(0x36))
                                  .build(key);

        expectRoundtrip(MPTokenIssuanceFFIBuilder(), cppEntry, "mptoken_issuance");
    }

    void
    testMPTokenRoundtrip()
    {
        beginCase("ledger.mptoken_roundtrip");
        AccountID const holder = fillId<AccountID>(0x44);
        MPTID const issuanceID = makeMptID(9, fillId<AccountID>(0x45));
        uint256 const key = keylet::mptoken(issuanceID, holder).key;

        auto const cppEntry =
            ledger_entries::MPTokenBuilder(holder, issuanceID, 3, fillId<uint256>(0x46), 7)
                .setFlags(0)
                .setMPTAmount(500)
                .setLockedAmount(20)
                .build(key);

        expectRoundtrip(MPTokenFFIBuilder(), cppEntry, "mptoken");
    }

    void
    testRippleStateRoundtrip()
    {
        beginCase("ledger.ripple_state_roundtrip");
        STAmount const balance = iou(0x01, 0x02, 0, 0);
        STAmount const lowLimit = iou(0x01, 0x0A, 1000, 0);
        STAmount const highLimit = iou(0x01, 0x0B, 2000, 0);
        uint256 const key =
            keylet::line(fillId<AccountID>(0x0A), fillId<AccountID>(0x0B), fillId<Currency>(0x01))
                .key;

        auto const cppEntry = ledger_entries::RippleStateBuilder(
                                  balance, lowLimit, highLimit, fillId<uint256>(0x0C), 55)
                                  .setFlags(lsfDefaultRipple)
                                  .setLowNode(7)
                                  .setLowQualityIn(100)
                                  .setLowQualityOut(150)
                                  .setHighNode(8)
                                  .setHighQualityIn(200)
                                  .setHighQualityOut(300)
                                  .build(key);

        expectRoundtrip(RippleStateFFIBuilder(), cppEntry, "ripple_state");
    }

    void
    testVaultRoundtrip()
    {
        beginCase("ledger.vault_roundtrip");
        Asset const asset{Issue{fillId<Currency>(0x01), fillId<AccountID>(0x71)}};
        AccountID const pseudoID = fillId<AccountID>(0x72);
        AccountID const owner = fillId<AccountID>(0x73);
        MPTID const shareMPTID = makeMptID(1, fillId<AccountID>(0x74));
        uint32_t const sequence = 5;
        uint256 const key = keylet::vault(owner, sequence).key;

        auto const cppEntry =
            ledger_entries::VaultBuilder(
                fillId<uint256>(0x75), 66, sequence, 3, owner, pseudoID, asset, shareMPTID, 1)
                .setFlags(lsfMPTCanTransfer)
                .setData(makeSlice(Blob{0x77, 0x88}))
                .setAssetsTotal(Number{500, 0})
                .setAssetsAvailable(Number{400, 0})
                .setAssetsMaximum(Number{1000, 0})
                .setLossUnrealized(Number{10, 0})
                .setScale(6)
                .build(key);

        expectRoundtrip(VaultFFIBuilder(), cppEntry, "vault");
    }

    void
    testLoanBrokerRoundtrip()
    {
        beginCase("ledger.loan_broker_roundtrip");
        AccountID const owner = fillId<AccountID>(0x90);
        AccountID const account = fillId<AccountID>(0x91);
        uint32_t const sequence = 5;
        uint256 const key = keylet::loanbroker(owner, sequence).key;

        auto const cppEntry =
            ledger_entries::LoanBrokerBuilder(
                fillId<uint256>(0x93), 88, sequence, 3, 4, fillId<uint256>(0x92), account, owner, 9)
                .setFlags(0)
                .setData(makeSlice(Blob{0x11, 0x22}))
                .setManagementFeeRate(25)
                .setOwnerCount(2)
                .setDebtTotal(Number{100, 0})
                .setCoverRateMinimum(30)
                .setCoverRateLiquidation(40)
                .build(key);

        expectRoundtrip(LoanBrokerFFIBuilder(), cppEntry, "loan_broker");
    }

    void
    testLoanRoundtrip()
    {
        beginCase("ledger.loan_roundtrip");
        uint256 const loanBrokerID = fillId<uint256>(0xA0);
        AccountID const borrower = fillId<AccountID>(0xA1);
        uint32_t const loanSeq = 7;
        uint256 const key = keylet::loan(loanBrokerID, loanSeq).key;

        auto const cppEntry = ledger_entries::LoanBuilder(
                                  fillId<uint256>(0xA2),
                                  88,
                                  3,
                                  4,
                                  loanBrokerID,
                                  loanSeq,
                                  borrower,
                                  100,
                                  200,
                                  Number{250, 0})
                                  .setFlags(0)
                                  .setOverpaymentFee(10)
                                  .setInterestRate(11)
                                  .setLateInterestRate(12)
                                  .setCloseInterestRate(13)
                                  .setOverpaymentInterestRate(14)
                                  .setGracePeriod(300)
                                  .setPreviousPaymentDueDate(400)
                                  .setNextPaymentDueDate(500)
                                  .setPaymentRemaining(6)
                                  .setPrincipalOutstanding(Number{500, 0})
                                  .setLoanScale(9)
                                  .build(key);

        expectRoundtrip(LoanFFIBuilder(), cppEntry, "loan");
    }

    void
    testPermissionedDomainRoundtrip()
    {
        beginCase("ledger.permissioned_domain_roundtrip");
        AccountID const owner = fillId<AccountID>(0xB0);
        uint32_t const sequence = 9;
        uint256 const key = keylet::permissionedDomain(owner, sequence).key;

        std::vector<std::pair<AccountID, Blob>> const creds{
            {fillId<AccountID>(0xB1), Blob{0x01, 0x02}}, {fillId<AccountID>(0xB2), Blob{0x03}}};
        STArray arr(sfAcceptedCredentials);
        for (auto const& [iss, ct] : creds)
        {
            STObject cred(sfCredential);
            cred.setAccountID(sfIssuer, iss);
            cred.setFieldVL(sfCredentialType, ct);
            arr.push_back(std::move(cred));
        }

        auto const cppEntry = ledger_entries::PermissionedDomainBuilder(
                                  owner, sequence, arr, 5, fillId<uint256>(0xB3), 66)
                                  .setFlags(0)
                                  .build(key);

        expectRoundtrip(PermissionedDomainFFIBuilder(), cppEntry, "permissioned_domain");
    }

    void
    testLedgerHeaderRoundtrip()
    {
        beginCase("ledger.header_roundtrip");
        uint32_t const seq = 42;
        uint32_t const parentCloseTime = 700000000;
        uint256 const parentHash = fillId<uint256>(0x80);

        LedgerFFI const ledger =
            LedgerFFIBuilder()
                .header(LedgerHeaderFFI::build(seq, parentCloseTime, parentHash))
                .build();

        auto const got = ledger.header();
        BEAST_EXPECT(got.seq() == seq);
        BEAST_EXPECT(got.parentCloseTime() == parentCloseTime);
        BEAST_EXPECT(got.parentHash() == parentHash);
    }

    void
    testFeesRoundtrip()
    {
        beginCase("ledger.fees_roundtrip");
        XRPAmount const base{10};
        XRPAmount const reserve{2000000};
        XRPAmount const increment{500000};

        LedgerFFI const ledger =
            LedgerFFIBuilder().fees(FeesFFI::build(base, reserve, increment)).build();

        auto const got = ledger.fees();
        BEAST_EXPECT(got.base() == base);
        BEAST_EXPECT(got.reserve() == reserve);
        BEAST_EXPECT(got.increment() == increment);
    }

    void
    testSha512HalfFFI()
    {
        beginCase("ledger.sha512half_ffi");
        // The only crypto primitive Lean delegates to C++: every keylet index hash
        // routes through it, so verify its ByteArray-in / uint256-out marshaling.
        Blob counting(64);
        for (std::size_t i = 0; i < counting.size(); ++i)
            counting[i] = static_cast<std::uint8_t>(i);

        std::vector<Blob> const inputs{
            Blob{0x00}, Blob{0x61, 0x62}, Blob{0x61, 0x62, 0x63}, counting};

        for (Blob const& in : inputs)
        {
            LeanObjectFFI const inBytes = mkBytes(in.data(), in.size());
            UInt256FFI const got(cpp_sha_512_half(inBytes.raw()));
            uint256 const expected = sha512Half(Slice{in.data(), in.size()});
            BEAST_EXPECT(got.read() == expected);
        }
    }

    void
    testPseudoAccountAddressFFI()
    {
        using namespace jtx;
        beginCase("ledger.pseudo_account_address_ffi");
        Env env(*this);
        auto const& view = *env.current();
        LeanObjectFFI const parentHash = mkBytes(view.header().parentHash);

        for (uint256 const& ownerKey :
             {fillId<uint256>(0x01), fillId<uint256>(0xAB), fillId<uint256>(0xCD)})
        {
            AccountID const expected = pseudoAccountAddress(view, ownerKey);
            LeanObjectFFI const ownerKeyBytes = mkBytes(ownerKey);
            LeanObjectFFI const got(
                cpp_pseudo_account_address_hash(0, parentHash.raw(), ownerKeyBytes.raw()));
            BEAST_EXPECT(readBytes(got.raw()) == Blob(expected.begin(), expected.end()));
        }
    }

    void
    testKeylets()
    {
        beginCase("ledger.keylets");
        // Each entry's index is computed by Lean (Indexes.lean) over the same fields
        // rippled hashes; assert byte-for-byte equality with keylet::*.
        AccountID const a = fillId<AccountID>(0xA1);
        AccountID const b = fillId<AccountID>(0xB2);
        Currency const cur = fillId<Currency>(0xC3);
        MPTID const mptID = makeMptID(7, a);
        Blob const credType{0x01, 0x02, 0x03};
        uint32_t const seq = 42;
        uint256 const loanBrokerID = fillId<uint256>(0xD4);

        BEAST_EXPECT(leanKeylet::account(a) == keylet::account(a).key);
        BEAST_EXPECT(leanKeylet::line(a, b, cur) == keylet::line(a, b, cur).key);
        BEAST_EXPECT(leanKeylet::mptIssuance(mptID) == keylet::mptIssuance(mptID).key);
        BEAST_EXPECT(leanKeylet::mptoken(mptID, b) == keylet::mptoken(mptID, b).key);
        BEAST_EXPECT(
            leanKeylet::credential(a, b, makeSlice(credType)) ==
            keylet::credential(a, b, makeSlice(credType)).key);
        BEAST_EXPECT(leanKeylet::depositPreauthAccount(a, b) == keylet::depositPreauth(a, b).key);

        std::vector<std::pair<AccountID, Blob>> const creds{
            {fillId<AccountID>(0x62), Blob{0x01, 0x02}}, {fillId<AccountID>(0x63), Blob{0x03}}};
        std::set<std::pair<AccountID, Slice>> sortedCreds;
        for (auto const& [acc, blob] : creds)
            sortedCreds.emplace(acc, makeSlice(blob));
        BEAST_EXPECT(
            leanKeylet::depositPreauthCreds(a, creds) ==
            keylet::depositPreauth(a, sortedCreds).key);

        BEAST_EXPECT(
            leanKeylet::permissionedDomain(a, seq) == keylet::permissionedDomain(a, seq).key);
        BEAST_EXPECT(leanKeylet::vault(a, seq) == keylet::vault(a, seq).key);
        BEAST_EXPECT(leanKeylet::loanBroker(a, seq) == keylet::loanbroker(a, seq).key);
        BEAST_EXPECT(leanKeylet::loan(loanBrokerID, seq) == keylet::loan(loanBrokerID, seq).key);
    }

    void
    runTests() override
    {
        testSha512HalfFFI();
        testPseudoAccountAddressFFI();
        testKeylets();
        testLedgerHeaderRoundtrip();
        testFeesRoundtrip();
        testAccountRootRoundtrip();
        testCredentialRoundtrip();
        testDepositPreauthRoundtrip();
        testMPTokenIssuanceRoundtrip();
        testMPTokenRoundtrip();
        testRippleStateRoundtrip();
        testVaultRoundtrip();
        testLoanBrokerRoundtrip();
        testLoanRoundtrip();
        testPermissionedDomainRoundtrip();
    }
};

BEAST_DEFINE_TESTSUITE(LeanLedger, formal_verification, xrpl);

}  // namespace xrpl::test
