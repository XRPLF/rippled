#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol_autogen/ledger_entries/AccountRoot.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>
#include <xrpl/protocol_autogen/transactions/SetRegularKey.h>
#include <xrpl/protocol_autogen/transactions/SignerListSet.h>
#include <xrpl/protocol_autogen/transactions/TicketCreate.h>
#include <xrpl/protocol_autogen/transactions/TrustSet.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/IOU.h>
#include <helpers/TxTest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::test {

TEST(AccountSet, NullAccountSet)
{
    TxTest env;

    Account const alice("alice");
    env.createAccount(alice, XRP(10));

    auto& view = env.getOpenLedger();

    // ask for the ledger entry - account root, to check its flags
    auto sle = view.read(keylet::account(alice));

    EXPECT_NE(sle, nullptr);
    ledger_entries::AccountRoot const accountRoot(sle);
    EXPECT_EQ(accountRoot.getFlags(), 0);
}

TEST(AccountSet, MostFlags)
{
    Account const alice("alice");

    TxTest env;
    env.createAccount(alice, XRP(10000));

    // Give alice a regular key so she can legally set and clear
    // her asfDisableMaster flag.
    Account const aliceRegularKey{"aliceRegularKey", KeyType::Secp256k1};

    env.createAccount(aliceRegularKey, XRP(10000));
    env.close();

    EXPECT_EQ(
        env.submit(transactions::SetRegularKeyBuilder{alice}.setRegularKey(aliceRegularKey), alice)
            .ter,
        tesSUCCESS);
    env.close();

    auto testFlags = [&alice, &aliceRegularKey, &env](
                         std::initializer_list<std::uint32_t> goodFlags) {
        std::uint32_t const origFlags = env.getAccountRoot(alice).getFlags();
        for (std::uint32_t flag{1u}; flag < std::numeric_limits<std::uint32_t>::digits; ++flag)
        {
            if (flag == asfNoFreeze)
            {
                // The asfNoFreeze flag can't be cleared.  It is tested
                // elsewhere.
                continue;
            }
            if (flag == asfAuthorizedNFTokenMinter)
            {
                // The asfAuthorizedNFTokenMinter flag requires the
                // presence or absence of the sfNFTokenMinter field in
                // the transaction.  It is tested elsewhere.
                continue;
            }

            if (flag == asfDisallowIncomingCheck || flag == asfDisallowIncomingPayChan ||
                flag == asfDisallowIncomingNFTokenOffer || flag == asfDisallowIncomingTrustline)
            {
                // These flags are part of the DisallowIncoming amendment
                // and are tested elsewhere
                continue;
            }
            if (flag == asfAllowTrustLineClawback)
            {
                // The asfAllowTrustLineClawback flag can't be cleared.  It
                // is tested elsewhere.
                continue;
            }
            if (flag == asfAllowTrustLineLocking)
            {
                // These flags are part of the AllowTokenLocking amendment
                // and are tested elsewhere
                continue;
            }
            if (std::ranges::find(goodFlags, flag) != goodFlags.end())
            {
                // Good flag
                EXPECT_FALSE(env.getAccountRoot(alice).isFlag(asfToLsf(flag)));

                EXPECT_EQ(
                    env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(flag), alice).ter,
                    tesSUCCESS);
                env.close();

                EXPECT_TRUE(env.getAccountRoot(alice).isFlag(asfToLsf(flag)));

                EXPECT_EQ(
                    env.submit(
                           transactions::AccountSetBuilder{alice}.setClearFlag(flag),
                           aliceRegularKey)
                        .ter,
                    tesSUCCESS);
                env.close();

                EXPECT_FALSE(env.getAccountRoot(alice).isFlag(asfToLsf(flag)));

                std::uint32_t const nowFlags = env.getAccountRoot(alice).getFlags();
                EXPECT_EQ(nowFlags, origFlags);
            }
            else
            {
                // Bad flag
                EXPECT_EQ(env.getAccountRoot(alice).getFlags(), origFlags);
                EXPECT_EQ(
                    env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(flag), alice).ter,
                    tesSUCCESS);
                env.close();
                EXPECT_EQ(env.getAccountRoot(alice).getFlags(), origFlags);

                EXPECT_EQ(
                    env.submit(
                           transactions::AccountSetBuilder{alice}.setClearFlag(flag),
                           aliceRegularKey)
                        .ter,
                    tesSUCCESS);
                env.close();
                EXPECT_EQ(env.getAccountRoot(alice).getFlags(), origFlags);
            }
        }
    };
    testFlags({
        asfRequireDest,
        asfRequireAuth,
        asfDisallowXRP,
        asfGlobalFreeze,
        asfDisableMaster,
        asfDefaultRipple,
        asfDepositAuth,
    });
}

TEST(AccountSet, SetAndResetAccountTxnID)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));

    std::uint32_t const origFlags = env.getAccountRoot(alice).getFlags();

    // asfAccountTxnID is special and not actually set as a flag,
    // so we check the field presence instead
    EXPECT_FALSE(env.getAccountRoot(alice).hasAccountTxnID());

    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(asfAccountTxnID), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_TRUE(env.getAccountRoot(alice).hasAccountTxnID());

    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setClearFlag(asfAccountTxnID), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_FALSE(env.getAccountRoot(alice).hasAccountTxnID());

    std::uint32_t const nowFlags = env.getAccountRoot(alice).getFlags();
    EXPECT_EQ(nowFlags, origFlags);
}

TEST(AccountSet, SetNoFreeze)
{
    TxTest env;
    Account const alice("alice");
    Account const eric("eric");

    env.createAccount(alice, XRP(10000));
    env.close();

    // Set eric as alice's regular key (eric doesn't need to be funded)
    EXPECT_EQ(
        env.submit(transactions::SetRegularKeyBuilder{alice}.setRegularKey(eric), alice).ter,
        tesSUCCESS);
    env.close();

    // Verify alice doesn't have NoFreeze flag
    EXPECT_FALSE(env.getAccountRoot(alice).isFlag(lsfNoFreeze));

    // Setting NoFreeze with regular key should fail - requires master key
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(asfNoFreeze), eric).ter,
        tecNEED_MASTER_KEY);
    env.close();

    // Setting NoFreeze with master key should succeed
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(asfNoFreeze), alice).ter,
        tesSUCCESS);
    env.close();

    // Verify alice now has NoFreeze flag
    EXPECT_TRUE(env.getAccountRoot(alice).isFlag(lsfNoFreeze));

    // Try to clear NoFreeze - transaction succeeds but flag remains set
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setClearFlag(asfNoFreeze), alice).ter,
        tesSUCCESS);
    env.close();

    // Verify flag is still set (NoFreeze cannot be cleared once set)
    EXPECT_TRUE(env.getAccountRoot(alice).isFlag(lsfNoFreeze));
}

TEST(AccountSet, Domain)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    // The Domain field is represented as the hex string of the lowercase
    // ASCII of the domain. For example, the domain example.com would be
    // represented as "6578616d706c652e636f6d".
    //
    // To remove the Domain field from an account, send an AccountSet with
    // the Domain set to an empty string.
    std::string const domain = "example.com";

    // Set domain
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setDomain(makeSlice(domain)), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_TRUE(env.getAccountRoot(alice).hasDomain());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(*env.getAccountRoot(alice).getDomain(), makeSlice(domain));

    // Clear domain by setting empty
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setDomain(Slice{}), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_FALSE(env.getAccountRoot(alice).hasDomain());

    // The upper limit on the length is 256 bytes
    // (defined as DOMAIN_BYTES_MAX in SetAccount)
    // test the edge cases: 255, 256, 257.
    std::size_t const maxLength = 256;
    for (std::size_t len = maxLength - 1; len <= maxLength + 1; ++len)
    {
        std::string const domain2 = std::string(len - domain.length() - 1, 'a') + "." + domain;

        EXPECT_EQ(domain2.length(), len);

        if (len <= maxLength)
        {
            EXPECT_EQ(
                env.submit(
                       transactions::AccountSetBuilder{alice}.setDomain(makeSlice(domain2)), alice)
                    .ter,
                tesSUCCESS);
            env.close();

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_EQ(*env.getAccountRoot(alice).getDomain(), makeSlice(domain2));
        }
        else
        {
            EXPECT_EQ(
                env.submit(
                       transactions::AccountSetBuilder{alice}.setDomain(makeSlice(domain2)), alice)
                    .ter,
                telBAD_DOMAIN);
            env.close();
        }
    }
}

TEST(AccountSet, MessageKey)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    // Generate a random ed25519 key pair for the message key
    auto const rkp = randomKeyPair(KeyType::Ed25519);

    // Set the message key
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setMessageKey(rkp.first.slice()), alice)
            .ter,
        tesSUCCESS);
    env.close();

    EXPECT_TRUE(env.getAccountRoot(alice).hasMessageKey());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(*env.getAccountRoot(alice).getMessageKey(), rkp.first.slice());

    // Clear the message key by setting to empty
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setMessageKey(Slice{}), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_FALSE(env.getAccountRoot(alice).hasMessageKey());

    // Try to set an invalid public key - should fail
    using namespace std::string_literals;
    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}.setMessageKey(
                   makeSlice("NOT_REALLY_A_PUBKEY"s)),
               alice)
            .ter,
        telBAD_PUBLIC_KEY);
}

TEST(AccountSet, WalletID)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    std::string_view const locator =
        "9633EC8AF54F16B5286DB1D7B519EF49EEFC050C0C8AC4384F1D88ACD1BFDF05";
    uint256 locatorHash{};
    EXPECT_TRUE(locatorHash.parseHex(locator));

    // Set the wallet locator
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setWalletLocator(locatorHash), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_TRUE(env.getAccountRoot(alice).hasWalletLocator());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(*env.getAccountRoot(alice).getWalletLocator(), locatorHash);

    // Clear the wallet locator by setting to zero
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setWalletLocator(beast::kZero), alice)
            .ter,
        tesSUCCESS);
    env.close();

    EXPECT_FALSE(env.getAccountRoot(alice).hasWalletLocator());
}

TEST(AccountSet, EmailHash)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    std::string_view const mh = "5F31A79367DC3137FADA860C05742EE6";
    uint128 emailHash{};
    EXPECT_TRUE(emailHash.parseHex(mh));

    // Set the email hash
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setEmailHash(emailHash), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_TRUE(env.getAccountRoot(alice).hasEmailHash());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(*env.getAccountRoot(alice).getEmailHash(), emailHash);

    // Clear the email hash by setting to zero
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setEmailHash(beast::kZero), alice).ter,
        tesSUCCESS);
    env.close();

    EXPECT_FALSE(env.getAccountRoot(alice).hasEmailHash());
}

TEST(AccountSet, TransferRate)
{
    struct TestCase
    {
        double set;
        TER code;
        double get;
    };

    // Test data: {rate to set, expected TER, expected stored rate}
    std::vector<TestCase> const testData = {
        {1.0, tesSUCCESS, 1.0},
        {1.1, tesSUCCESS, 1.1},
        {2.0, tesSUCCESS, 2.0},
        {2.1, temBAD_TRANSFER_RATE, 2.0},  // > 2.0 is invalid
        {0.0, tesSUCCESS, 1.0},            // 0 clears the rate (default = 1.0)
        {2.0, tesSUCCESS, 2.0},
        {0.9, temBAD_TRANSFER_RATE, 2.0},  // < 1.0 is invalid
    };

    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    for (auto const& r : testData)
    {
        auto const rateValue = static_cast<std::uint32_t>(QUALITY_ONE * r.set);

        EXPECT_EQ(
            env.submit(transactions::AccountSetBuilder{alice}.setTransferRate(rateValue), alice)
                .ter,
            r.code);
        env.close();

        // If the field is not present, expect the default value (1.0)
        if (!env.getAccountRoot(alice).hasTransferRate())
        {
            EXPECT_EQ(r.get, 1.0);
        }
        else
        {
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_EQ(
                *env.getAccountRoot(alice).getTransferRate(),
                static_cast<std::uint32_t>(r.get * QUALITY_ONE));
        }
    }
}

TEST(AccountSet, BadInputs)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    // Setting and clearing the same flag is invalid
    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfDisallowXRP)
                   .setClearFlag(asfDisallowXRP),
               alice)
            .ter,
        temINVALID_FLAG);

    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfRequireAuth)
                   .setClearFlag(asfRequireAuth),
               alice)
            .ter,
        temINVALID_FLAG);

    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfRequireDest)
                   .setClearFlag(asfRequireDest),
               alice)
            .ter,
        temINVALID_FLAG);

    // Setting asf flag while also using corresponding tf flag is invalid
    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfDisallowXRP)
                   .setFlags(tfAllowXRP),
               alice)
            .ter,
        temINVALID_FLAG);

    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfRequireAuth)
                   .setFlags(tfOptionalAuth),
               alice)
            .ter,
        temINVALID_FLAG);

    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfRequireDest)
                   .setFlags(tfOptionalDestTag),
               alice)
            .ter,
        temINVALID_FLAG);

    // Using invalid flags (mask) is invalid
    EXPECT_EQ(
        env.submit(
               transactions::AccountSetBuilder{alice}
                   .setSetFlag(asfRequireDest)
                   .setFlags(tfAccountSetMask),
               alice)
            .ter,
        temINVALID_FLAG);

    // Disabling master key without an alternative key is invalid
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(asfDisableMaster), alice).ter,
        tecNO_ALTERNATIVE_KEY);
}

TEST(AccountSet, RequireAuthWithDir)
{
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");

    env.createAccount(alice, XRP(10000));
    env.close();

    // alice should have an empty directory
    EXPECT_TRUE(dirIsEmpty(env.getClosedLedger(), keylet::ownerDir(alice.id())));

    // Give alice a signer list, then there will be stuff in the directory
    // Build the SignerEntries array
    STArray signerEntries(1);
    {
        signerEntries.push_back(STObject::makeInnerObject(sfSignerEntry));
        STObject& entry = signerEntries.back();
        entry[sfAccount] = bob.id();
        entry[sfSignerWeight] = std::uint16_t{1};
    }

    EXPECT_EQ(
        env.submit(
               transactions::SignerListSetBuilder{alice, 1}.setSignerEntries(signerEntries), alice)
            .ter,
        tesSUCCESS);
    env.close();

    EXPECT_FALSE(dirIsEmpty(env.getClosedLedger(), keylet::ownerDir(alice.id())));

    // Setting RequireAuth should fail because alice has owner objects
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(asfRequireAuth), alice).ter,
        tecOWNERS);

    // Remove the signer list (quorum = 0, no entries)
    EXPECT_EQ(env.submit(transactions::SignerListSetBuilder{alice, 0}, alice).ter, tesSUCCESS);
    env.close();

    EXPECT_TRUE(dirIsEmpty(env.getClosedLedger(), keylet::ownerDir(alice.id())));

    // Now setting RequireAuth should succeed
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setSetFlag(asfRequireAuth), alice).ter,
        tesSUCCESS);
}

TEST(AccountSet, Ticket)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    // Get alice's current sequence - the ticket will be created at seq + 1
    std::uint32_t const aliceSeqBefore = env.getAccountRoot(alice.id()).getSequence();
    std::uint32_t const ticketSeq = aliceSeqBefore + 1;

    // Create a ticket
    EXPECT_EQ(env.submit(transactions::TicketCreateBuilder{alice, 1}, alice).ter, tesSUCCESS);
    env.close();

    // Verify alice has 1 owner object (the ticket)
    EXPECT_EQ(env.getAccountRoot(alice.id()).getOwnerCount(), 1u);
    // Verify ticket exists
    EXPECT_TRUE(env.getClosedLedger().exists(keylet::kTicket(alice.id(), ticketSeq)));

    // Try using a ticket that alice doesn't have
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setTicketSequence(ticketSeq + 1), alice)
            .ter,
        terPRE_TICKET);
    env.close();

    // Verify ticket still exists
    EXPECT_TRUE(env.getClosedLedger().exists(keylet::kTicket(alice.id(), ticketSeq)));

    // Get alice's sequence before using the ticket
    std::uint32_t const aliceSeq = env.getAccountRoot(alice.id()).getSequence();

    // Actually use alice's ticket (noop AccountSet)
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setTicketSequence(ticketSeq), alice).ter,
        tesSUCCESS);
    env.close();

    // Verify ticket is consumed (no owner objects)
    EXPECT_EQ(env.getAccountRoot(alice.id()).getOwnerCount(), 0u);
    EXPECT_FALSE(env.getClosedLedger().exists(keylet::kTicket(alice.id(), ticketSeq)));

    // Verify alice's sequence did NOT advance (ticket use doesn't increment seq)
    EXPECT_EQ(env.getAccountRoot(alice.id()).getSequence(), aliceSeq);

    // Try re-using a ticket that alice already used
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setTicketSequence(ticketSeq), alice).ter,
        tefNO_TICKET);
}

TEST(AccountSet, BadSigningKey)
{
    TxTest env;
    Account const alice("alice");

    env.createAccount(alice, XRP(10000));
    env.close();

    // Build a valid transaction first, then corrupt the signing key
    auto stx = transactions::AccountSetBuilder{alice}
                   .setSequence(env.getAccountRoot(alice.id()).getSequence())
                   .setFee(XRPAmount{10})
                   .build(alice.pk(), alice.sk())
                   .getSTTx();

    // Create a copy with a bad signing key
    STObject obj = *stx;
    obj.setFieldVL(sfSigningPubKey, makeSlice(std::string("badkey")));

    auto result = env.submit(std::make_shared<STTx>(std::move(obj)));
    EXPECT_EQ(result.ter, temBAD_SIGNATURE);
    EXPECT_FALSE(result.applied);
}

TEST(AccountSet, Gateway)
{
    Account const alice("alice");
    Account const bob("bob");
    Account const gw("gateway");
    IOU const usd("USD", gw);

    // Test gateway with a variety of allowed transfer rates
    for (double transferRate = 1.0; transferRate <= 2.0; transferRate += 0.03125)
    {
        TxTest env;

        env.createAccount(gw, XRP(10000), asfDefaultRipple);
        env.createAccount(alice, XRP(10000), asfDefaultRipple);
        env.createAccount(bob, XRP(10000), asfDefaultRipple);
        env.close();

        // Set up trust lines: alice and bob trust gw for USD
        EXPECT_EQ(
            env.submit(transactions::TrustSetBuilder{alice}.setLimitAmount(usd.amount(10)), alice)
                .ter,
            tesSUCCESS);
        EXPECT_EQ(
            env.submit(transactions::TrustSetBuilder{bob}.setLimitAmount(usd.amount(10)), bob).ter,
            tesSUCCESS);
        env.close();

        // Set transfer rate on the gateway
        EXPECT_EQ(
            env.submit(
                   transactions::AccountSetBuilder{gw}.setTransferRate(
                       static_cast<std::uint32_t>(transferRate * QUALITY_ONE)),
                   gw)
                .ter,
            tesSUCCESS);
        env.close();

        // Calculate the amount with transfer rate applied
        auto const amount = usd.amount(1);
        Rate const rate(static_cast<std::uint32_t>(transferRate * QUALITY_ONE));
        auto const amountWithRate = multiply(amount, rate);

        // Gateway pays alice 10 USD
        EXPECT_EQ(
            env.submit(transactions::PaymentBuilder{gw, alice, usd.amount(10)}, gw).ter,
            tesSUCCESS);
        env.close();

        // Alice pays bob 1 USD (with sendmax to cover transfer fee)
        EXPECT_EQ(
            env.submit(
                   transactions::PaymentBuilder{alice, bob, usd.amount(1)}.setSendMax(
                       usd.amount(10)),
                   alice)
                .ter,
            tesSUCCESS);
        env.close();

        // Check balances
        EXPECT_EQ(env.getBalance(alice.id(), usd), usd.amount(10) - amountWithRate);
        EXPECT_EQ(env.getBalance(bob.id(), usd), usd.amount(1));
    }

    // Test out-of-bounds legacy transfer rates (4.0 and 4.294967295)
    // These require direct ledger modification since the transactor blocks them
    for (std::uint32_t const transferRate : {4000000000U, 4294967295U})
    {
        TxTest env;
        env.createAccount(gw, XRP(10000), asfDefaultRipple);
        env.createAccount(alice, XRP(10000), asfDefaultRipple);
        env.createAccount(bob, XRP(10000), asfDefaultRipple);
        env.close();

        // Set up trust lines
        EXPECT_EQ(
            env.submit(transactions::TrustSetBuilder{alice}.setLimitAmount(usd.amount(10)), alice)
                .ter,
            tesSUCCESS);
        EXPECT_EQ(
            env.submit(transactions::TrustSetBuilder{bob}.setLimitAmount(usd.amount(10)), bob).ter,
            tesSUCCESS);
        env.close();

        // Set an acceptable transfer rate first (we'll hack it later)
        EXPECT_EQ(
            env.submit(
                   transactions::AccountSetBuilder{gw}.setTransferRate(
                       static_cast<std::uint32_t>(2.0 * QUALITY_ONE)),
                   gw)
                .ter,
            tesSUCCESS);
        env.close();

        // Directly modify the ledger to set an out-of-bounds transfer rate
        // This bypasses the transactor's validation
        auto& view = env.getOpenLedger();
        auto slePtr = view.read(keylet::account(gw.id()));
        ASSERT_NE(slePtr, nullptr);
        auto sleCopy = std::make_shared<SLE>(*slePtr);
        (*sleCopy)[sfTransferRate] = transferRate;
        view.rawReplace(sleCopy);

        // Calculate the amount with the legacy transfer rate
        auto const amount = usd.amount(1);
        auto const amountWithRate = multiply(amount, Rate(transferRate));

        // Gateway pays alice 10 USD
        EXPECT_EQ(
            env.submit(transactions::PaymentBuilder{gw, alice, usd.amount(10)}, gw).ter,
            tesSUCCESS);

        // Alice pays bob 1 USD
        EXPECT_EQ(
            env.submit(
                   transactions::PaymentBuilder{alice, bob, amount}.setSendMax(usd.amount(10)),
                   alice)
                .ter,
            tesSUCCESS);

        // Check balances
        EXPECT_EQ(env.getBalance(alice.id(), usd), usd.amount(10) - amountWithRate);
        EXPECT_EQ(env.getBalance(bob.id(), usd), amount);
    }
}

}  // namespace xrpl::test
