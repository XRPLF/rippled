#include <test/jtx.h>
#include <test/jtx/acctdelete.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <secp256k1.h>

namespace ripple {
namespace test {

class StealthPayment_test : public beast::unit_test::suite
{
private:
    // Helper function to generate a valid compressed secp256k1 public key
    static Blob
    generateValidEphemeralKey()
    {
        // Generate a valid compressed secp256k1 public key (33 bytes)
        // This is a real valid public key for testing
        Blob key = {
            0x02, 0x9b, 0x63, 0x47, 0x39, 0x85, 0x05, 0xf5, 0xec,
            0x93, 0x82, 0x6d, 0xc6, 0x1c, 0x19, 0xf4, 0x7c, 0x66,
            0xc0, 0x28, 0x3e, 0xe9, 0xbe, 0x98, 0x0e, 0x29, 0xce,
            0x32, 0x5a, 0x0f, 0x29, 0x51, 0xdb};
        return key;
    }

    // Helper function to create a stealth payment transaction
    static Json::Value
    stealthPayment(
        jtx::Account const& account,
        jtx::Account const& dest,
        STAmount const& amount,
        Blob const& ephemeralKey)
    {
        Json::Value jv;
        jv[jss::TransactionType] = jss::StealthPayment;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = dest.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        jv[sfEphemeralPublicKey.jsonName] = strHex(ephemeralKey);
        return jv;
    }

public:
    void
    testAmendmentDisabled()
    {
        testcase("Amendment disabled");

        using namespace jtx;

        // Test with amendment disabled
        Env env{*this, testable_amendments() - featureStealthAddresses};
        Account const alice("alice");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();

        // Should fail with temDISABLED when amendment not enabled
        env(stealthPayment(alice, stealth, XRP(100), ephemeralKey),
            ter(temDISABLED));
    }

    void
    testValidStealthPayment()
    {
        testcase("Valid stealth payment");

        using namespace jtx;

        // Test with amendment enabled
        Env env{*this};
        Account const alice("alice");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();

        // Get reserve amount
        auto const reserve = env.current()->fees().accountReserve(0);

        // Valid stealth payment should succeed and create account
        env(stealthPayment(alice, stealth, reserve, ephemeralKey));
        env.close();

        // Verify the stealth account was created
        auto const sleStealthAccount = env.le(stealth);
        BEAST_EXPECT(sleStealthAccount);

        if (sleStealthAccount)
        {
            // Verify lsfStealthAddress flag is set
            BEAST_EXPECT(sleStealthAccount->isFlag(lsfStealthAddress));

            // Verify account has correct balance
            BEAST_EXPECT(
                sleStealthAccount->getFieldAmount(sfBalance) == reserve);
        }
    }

    void
    testInvalidEphemeralKey()
    {
        testcase("Invalid ephemeral key");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice);
        env.close();

        auto const reserve = env.current()->fees().accountReserve(0);

        // Test with wrong length (32 bytes instead of 33)
        Blob shortKey(32, 0x02);
        env(stealthPayment(alice, stealth, reserve, shortKey),
            ter(temBAD_EPHEMERAL_KEY));

        // Test with wrong length (34 bytes)
        Blob longKey(34, 0x02);
        env(stealthPayment(alice, stealth, reserve, longKey),
            ter(temBAD_EPHEMERAL_KEY));

        // Test with invalid secp256k1 point (all zeros)
        Blob invalidKey(33, 0x00);
        env(stealthPayment(alice, stealth, reserve, invalidKey),
            ter(temBAD_EPHEMERAL_KEY));

        // Test with invalid prefix (should be 0x02 or 0x03 for compressed)
        Blob invalidPrefix = {
            0x01, 0x9b, 0x63, 0x47, 0x39, 0x85, 0x05, 0xf5, 0xec,
            0x93, 0x82, 0x6d, 0xc6, 0x1c, 0x19, 0xf4, 0x7c, 0x66,
            0xc0, 0x28, 0x3e, 0xe9, 0xbe, 0x98, 0x0e, 0x29, 0xce,
            0x32, 0x5a, 0x0f, 0x29, 0x51, 0xdb};
        env(stealthPayment(alice, stealth, reserve, invalidPrefix),
            ter(temBAD_EPHEMERAL_KEY));
    }

    void
    testMissingEphemeralKey()
    {
        testcase("Missing ephemeral key");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice);
        env.close();

        auto const reserve = env.current()->fees().accountReserve(0);

        // Create transaction without ephemeral key
        Json::Value jv;
        jv[jss::TransactionType] = jss::StealthPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Destination] = stealth.human();
        jv[jss::Amount] = to_string(reserve);
        // Missing: jv[sfEphemeralPublicKey.jsonName]

        env(jv, ter(temMALFORMED));
    }

    void
    testInsufficientAmount()
    {
        testcase("Insufficient amount for account creation");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Try to create account with less than reserve
        env(stealthPayment(alice, stealth, reserve - drops(1), ephemeralKey),
            ter(tecNO_DST_INSUF_XRP));
    }

    void
    testNonXRPAmount()
    {
        testcase("Non-XRP amount not allowed");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const gw("gateway");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, gw);
        env.close();

        auto const USD = gw["USD"];
        env.trust(USD(1000), alice);
        env.close();

        env(pay(gw, alice, USD(500)));
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();

        // Try to send IOU - should fail
        env(stealthPayment(alice, stealth, USD(100), ephemeralKey),
            ter(temBAD_AMOUNT));
    }

    void
    testSelfPayment()
    {
        testcase("Self payment not allowed");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Try to send to self
        env(stealthPayment(alice, alice, reserve, ephemeralKey),
            ter(temDST_IS_SRC));
    }

    void
    testDuplicateStealthAddress()
    {
        testcase("Cannot fund existing stealth address");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey1 = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // First stealth payment - should succeed
        env(stealthPayment(alice, stealth, reserve, ephemeralKey1));
        env.close();

        // Verify account is marked as stealth
        auto const sleStealthAccount = env.le(stealth);
        BEAST_EXPECT(sleStealthAccount);
        BEAST_EXPECT(sleStealthAccount->isFlag(lsfStealthAddress));

        // Try to send to same stealth address again - should fail
        auto ephemeralKey2 = generateValidEphemeralKey();
        env(stealthPayment(bob, stealth, XRP(10), ephemeralKey2),
            ter(tecNO_PERMISSION));
    }

    void
    testStealthAccountOnlyAccountDelete()
    {
        testcase("Stealth account can only do AccountDelete");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Create stealth account
        env(stealthPayment(alice, stealth, reserve + XRP(100), ephemeralKey));
        env.close();

        // Verify it's a stealth account
        auto const sleStealthAccount = env.le(stealth);
        BEAST_EXPECT(sleStealthAccount);
        BEAST_EXPECT(sleStealthAccount->isFlag(lsfStealthAddress));

        // Try to make a payment FROM stealth account - should fail
        // Note: This requires setting the secret for the stealth account
        // which in real world would be derived from the ephemeral key
        // For testing purposes, we'll try to use the stealth account
        Json::Value jv;
        jv[jss::TransactionType] = jss::Payment;
        jv[jss::Account] = stealth.human();
        jv[jss::Destination] = bob.human();
        jv[jss::Amount] = XRP(10).value().getJson(JsonOptions::none);

        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Try SetAccount transaction
        jv[jss::TransactionType] = jss::AccountSet;
        jv[sfSetFlag.jsonName] = asfRequireDest;
        jv.removeMember(jss::Destination);
        jv.removeMember(jss::Amount);

        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Try to create a trust line from stealth account
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::TrustSet;
        jv[jss::Account] = stealth.human();
        auto const USD = alice["USD"];
        jv[jss::LimitAmount] = USD(1000).value().getJson(JsonOptions::none);

        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));
    }

    void
    testStealthAccountDelete()
    {
        testcase("Stealth account can perform AccountDelete");

        using namespace jtx;

        Env env{*this};

        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);
        auto const acctDelFee = drops(env.current()->fees().increment);

        // Create stealth account with enough for deletion
        env(stealthPayment(
            alice, stealth, reserve + XRP(100) + acctDelFee, ephemeralKey));
        env.close();

        // Verify it's created
        BEAST_EXPECT(env.le(stealth));

        // Close enough ledgers to enable account deletion
        incLgrSeqForAccDel(env, stealth);

        // AccountDelete should succeed for stealth account
        env(acctdelete(stealth, bob), fee(acctDelFee), sig(stealth));
        env.close();

        // Verify account is deleted
        BEAST_EXPECT(!env.le(stealth));
    }

    void
    testStealthAccountCanReceiveRegularPayment()
    {
        testcase("Stealth account can receive regular Payment transactions");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Create stealth account
        env(stealthPayment(alice, stealth, reserve + XRP(50), ephemeralKey));
        env.close();

        // Verify creation
        auto const sleStealthBefore = env.le(stealth);
        BEAST_EXPECT(sleStealthBefore);
        BEAST_EXPECT(sleStealthBefore->isFlag(lsfStealthAddress));
        auto const balanceBefore = (*sleStealthBefore)[sfBalance];

        // Regular Payment TO stealth account should succeed
        env(pay(bob, stealth, XRP(25)));
        env.close();

        // Verify stealth account received the payment
        auto const sleStealthAfter = env.le(stealth);
        BEAST_EXPECT(sleStealthAfter);
        auto const balanceAfter = (*sleStealthAfter)[sfBalance];
        BEAST_EXPECT(balanceAfter == balanceBefore + XRP(25));
    }

    void
    testStealthAccountCannotSendRegularPayment()
    {
        testcase("Stealth account cannot send regular Payment");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Create stealth account with funds
        env(stealthPayment(alice, stealth, reserve + XRP(100), ephemeralKey));
        env.close();

        // Verify it has funds
        auto const sleStealthBefore = env.le(stealth);
        BEAST_EXPECT(sleStealthBefore);
        auto const balanceBefore = (*sleStealthBefore)[sfBalance];
        BEAST_EXPECT(balanceBefore >= reserve + XRP(100));

        // Try to send regular Payment FROM stealth - should fail
        // Must build transaction manually since pay() helper doesn't work with stealth accounts
        Json::Value jv;
        jv[jss::TransactionType] = jss::Payment;
        jv[jss::Account] = stealth.human();
        jv[jss::Destination] = bob.human();
        jv[jss::Amount] = XRP(50).value().getJson(JsonOptions::none);
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Verify balance unchanged
        auto const sleStealthAfter = env.le(stealth);
        BEAST_EXPECT(sleStealthAfter);
        BEAST_EXPECT((*sleStealthAfter)[sfBalance] == balanceBefore);
    }

    void
    testCannotSendStealthPaymentToExistingStealthAccount()
    {
        testcase("Cannot send StealthPayment to existing stealth account");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey1 = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Create initial stealth account
        env(stealthPayment(alice, stealth, reserve + XRP(50), ephemeralKey1));
        env.close();

        // Verify it's a stealth account
        auto const sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        BEAST_EXPECT(sleStealth->isFlag(lsfStealthAddress));

        // Try to send another StealthPayment to same address - should fail
        auto ephemeralKey2 = generateValidEphemeralKey();
        env(stealthPayment(bob, stealth, XRP(25), ephemeralKey2),
            ter(tecNO_PERMISSION));

        // Balance should be unchanged
        auto const sleStealthAfter = env.le(stealth);
        BEAST_EXPECT(sleStealthAfter);
        BEAST_EXPECT(
            (*sleStealthAfter)[sfBalance] == (*sleStealth)[sfBalance]);
    }

    void
    testStealthAccountBlockedTransactionTypes()
    {
        testcase("Stealth account blocked from various transaction types");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Create stealth account with funds
        env(stealthPayment(alice, stealth, reserve + XRP(200), ephemeralKey));
        env.close();

        // Verify stealth account exists with flag
        auto sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        BEAST_EXPECT(sleStealth->isFlag(lsfStealthAddress));
        auto const initialBalance = (*sleStealth)[sfBalance];

        // Test OfferCreate - should be blocked
        Json::Value jv;
        jv[jss::TransactionType] = jss::OfferCreate;
        jv[jss::Account] = stealth.human();
        jv[jss::TakerPays] = XRP(10).value().getJson(JsonOptions::none);
        // Create a valid IOU format
        Json::Value usd;
        usd[jss::currency] = "USD";
        usd[jss::issuer] = alice.human();
        usd[jss::value] = "10";
        jv[jss::TakerGets] = usd;
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Verify no balance change (transaction was blocked)
        sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        BEAST_EXPECT((*sleStealth)[sfBalance] == initialBalance);

        // Test OfferCancel
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::OfferCancel;
        jv[jss::Account] = stealth.human();
        jv[sfOfferSequence.jsonName] = 1;
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Test SetRegularKey
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::SetRegularKey;
        jv[jss::Account] = stealth.human();
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Test SignerListSet
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::SignerListSet;
        jv[jss::Account] = stealth.human();
        jv[sfSignerQuorum.jsonName] = 0;
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Test EscrowCreate
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::EscrowCreate;
        jv[jss::Account] = stealth.human();
        jv[jss::Destination] = bob.human();
        jv[jss::Amount] = XRP(10).value().getJson(JsonOptions::none);
        jv[sfFinishAfter.jsonName] = env.now().time_since_epoch().count() + 10;
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Test CheckCreate
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::CheckCreate;
        jv[jss::Account] = stealth.human();
        jv[jss::Destination] = bob.human();
        jv[sfSendMax.jsonName] = XRP(10).value().getJson(JsonOptions::none);
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Test DepositPreauth
        jv = Json::Value{};
        jv[jss::TransactionType] = jss::DepositPreauth;
        jv[jss::Account] = stealth.human();
        jv[sfAuthorize.jsonName] = bob.human();
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));

        // Verify stealth account still exists and has correct flag
        auto const sleStealthFinal = env.le(stealth);
        BEAST_EXPECT(sleStealthFinal);
        BEAST_EXPECT(sleStealthFinal->isFlag(lsfStealthAddress));
    }

    void
    testStealthAccountFullLifecycle()
    {
        testcase("Full lifecycle: Create, receive, delete");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);
        auto const acctDelFee = drops(env.current()->fees().increment);

        // Step 1: Create stealth account
        env(stealthPayment(alice, stealth, reserve + XRP(100), ephemeralKey));
        env.close();

        auto sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        BEAST_EXPECT(sleStealth->isFlag(lsfStealthAddress));
        auto balance1 = (*sleStealth)[sfBalance];
        BEAST_EXPECT(balance1 == reserve + XRP(100));

        // Step 2: Receive multiple regular payments
        env(pay(bob, stealth, XRP(25)));
        env.close();

        sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        auto balance2 = (*sleStealth)[sfBalance];
        BEAST_EXPECT(balance2 == balance1 + XRP(25));

        env(pay(carol, stealth, XRP(30)));
        env.close();

        sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        auto balance3 = (*sleStealth)[sfBalance];
        BEAST_EXPECT(balance3 == balance2 + XRP(30));

        // Step 3: Verify cannot send payments out
        // Must build transaction manually
        Json::Value jv;
        jv[jss::TransactionType] = jss::Payment;
        jv[jss::Account] = stealth.human();
        jv[jss::Destination] = alice.human();
        jv[jss::Amount] = XRP(10).value().getJson(JsonOptions::none);
        env(jv, sig(stealth), ter(temSTEALTH_INVALID_TX));
        
        // The transaction failed in preflight, so balance remains unchanged
        sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        auto const balanceAfterFailedPayment = (*sleStealth)[sfBalance];
        BEAST_EXPECT(balanceAfterFailedPayment == balance3);

        // Step 4: Delete account and sweep all funds
        incLgrSeqForAccDel(env, stealth);

        // Get bob's balance before receiving the sweep
        auto const sleBobBefore = env.le(bob);
        BEAST_EXPECT(sleBobBefore);
        auto const bobBalanceBefore = (*sleBobBefore)[sfBalance];
        
        // Get stealth balance before deletion (should match balance3 from earlier)
        sleStealth = env.le(stealth);
        BEAST_EXPECT(sleStealth);
        auto const stealthBalanceBeforeDelete = (*sleStealth)[sfBalance];
        BEAST_EXPECT(stealthBalanceBeforeDelete == balanceAfterFailedPayment);
        
        env(acctdelete(stealth, bob), fee(acctDelFee), sig(stealth));
        env.close();

        // Verify account deleted
        BEAST_EXPECT(!env.le(stealth));

        // Verify bob received the funds (balance minus AccountDelete fee)
        auto const sleBobAfter = env.le(bob);
        BEAST_EXPECT(sleBobAfter);
        auto const bobBalanceAfter = (*sleBobAfter)[sfBalance];
        BEAST_EXPECT(
            bobBalanceAfter ==
            bobBalanceBefore + stealthBalanceBeforeDelete - acctDelFee);
    }

    void
    testMissingDestination()
    {
        testcase("Missing destination field");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();
        auto const reserve = env.current()->fees().accountReserve(0);

        // Create transaction without destination
        Json::Value jv;
        jv[jss::TransactionType] = jss::StealthPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Amount] = to_string(reserve);
        jv[sfEphemeralPublicKey.jsonName] = strHex(ephemeralKey);
        // Missing: jv[jss::Destination]

        env(jv, ter(temMALFORMED));
    }

    void
    testZeroOrNegativeAmount()
    {
        testcase("Zero or negative amount");

        using namespace jtx;

        Env env{*this};
        Account const alice("alice");
        Account const stealth("stealth");

        env.fund(XRP(10000), alice);
        env.close();

        auto ephemeralKey = generateValidEphemeralKey();

        // Try zero amount
        env(stealthPayment(alice, stealth, XRP(0), ephemeralKey),
            ter(temBAD_AMOUNT));

        // Try negative amount (if possible)
        Json::Value jv;
        jv[jss::TransactionType] = jss::StealthPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Destination] = stealth.human();
        jv[jss::Amount] = "-1000000";  // -1 XRP in drops
        jv[sfEphemeralPublicKey.jsonName] = strHex(ephemeralKey);

        env(jv, ter(temBAD_AMOUNT));
    }

    void
    testCryptographicKeyDerivation()
    {
        testcase("Cryptographic key derivation and signing");

        using namespace jtx;

        // This test validates the complete cryptographic flow:
        // 1. Alice generates ephemeral key pair
        // 2. Alice derives stealth address using Bob's root public key
        // 3. Bob derives stealth private key using his root private key
        // 4. Bob signs transaction with derived stealth private key

        // Initialize secp256k1 context
        secp256k1_context* ctx_secp = secp256k1_context_create(
            SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

        // Step 1: Generate Bob's root key pair
        unsigned char bob_root_privkey[32];
        for (int i = 0; i < 32; i++)
            bob_root_privkey[i] = i + 1;  // Deterministic for testing

        secp256k1_pubkey bob_root_pubkey_raw;
        int ret = secp256k1_ec_pubkey_create(
            ctx_secp, &bob_root_pubkey_raw, bob_root_privkey);
        BEAST_EXPECT(ret == 1);

        // Serialize Bob's root public key (compressed)
        unsigned char bob_root_pubkey[33];
        size_t bob_pub_len = 33;
        secp256k1_ec_pubkey_serialize(
            ctx_secp,
            bob_root_pubkey,
            &bob_pub_len,
            &bob_root_pubkey_raw,
            SECP256K1_EC_COMPRESSED);
        BEAST_EXPECT(bob_pub_len == 33);

        // Step 2: Alice generates ephemeral key pair
        unsigned char alice_ephemeral_privkey[32];
        for (int i = 0; i < 32; i++)
            alice_ephemeral_privkey[i] =
                i + 100;  // Different from Bob's key

        secp256k1_pubkey alice_ephemeral_pubkey_raw;
        ret = secp256k1_ec_pubkey_create(
            ctx_secp, &alice_ephemeral_pubkey_raw, alice_ephemeral_privkey);
        BEAST_EXPECT(ret == 1);

        // Serialize Alice's ephemeral public key (compressed)
        unsigned char alice_ephemeral_pubkey[33];
        size_t alice_pub_len = 33;
        secp256k1_ec_pubkey_serialize(
            ctx_secp,
            alice_ephemeral_pubkey,
            &alice_pub_len,
            &alice_ephemeral_pubkey_raw,
            SECP256K1_EC_COMPRESSED);
        BEAST_EXPECT(alice_pub_len == 33);

        // Step 3: Alice computes shared secret using ECDH
        // shared_secret = alice_ephemeral_priv * bob_root_pub
        unsigned char alice_shared_secret[32];
        ret = secp256k1_ecdh(
            ctx_secp,
            alice_shared_secret,
            &bob_root_pubkey_raw,
            alice_ephemeral_privkey,
            nullptr,
            nullptr);
        BEAST_EXPECT(ret == 1);

        // Step 4: Bob computes the same shared secret using ECDH
        // shared_secret = bob_root_priv * alice_ephemeral_pub
        unsigned char bob_shared_secret[32];
        ret = secp256k1_ecdh(
            ctx_secp,
            bob_shared_secret,
            &alice_ephemeral_pubkey_raw,
            bob_root_privkey,
            nullptr,
            nullptr);
        BEAST_EXPECT(ret == 1);

        // Step 5: Verify both computed the same shared secret
        bool secrets_match = true;
        for (int i = 0; i < 32; i++)
        {
            if (alice_shared_secret[i] != bob_shared_secret[i])
            {
                secrets_match = false;
                break;
            }
        }
        BEAST_EXPECT(secrets_match);

        // Step 6: Derive key offset from shared secret using SHA256
        unsigned char key_offset[32];
        ripple::sha256_hasher h;
        h(alice_shared_secret, 32);
        auto const digest = static_cast<ripple::sha256_hasher::result_type>(h);
        std::memcpy(key_offset, digest.data(), 32);

        // Step 7: Alice derives stealth public key
        // stealth_pub = bob_root_pub + (key_offset * G)
        secp256k1_pubkey offset_point;
        ret = secp256k1_ec_pubkey_create(ctx_secp, &offset_point, key_offset);
        BEAST_EXPECT(ret == 1);

        // Combine: stealth_pub = bob_root_pub + offset_point
        const secp256k1_pubkey* pubkeys[2] = {
            &bob_root_pubkey_raw, &offset_point};
        secp256k1_pubkey stealth_pubkey_raw;
        ret = secp256k1_ec_pubkey_combine(
            ctx_secp, &stealth_pubkey_raw, pubkeys, 2);
        BEAST_EXPECT(ret == 1);

        // Serialize stealth public key
        unsigned char stealth_pubkey[33];
        size_t stealth_pub_len = 33;
        secp256k1_ec_pubkey_serialize(
            ctx_secp,
            stealth_pubkey,
            &stealth_pub_len,
            &stealth_pubkey_raw,
            SECP256K1_EC_COMPRESSED);
        BEAST_EXPECT(stealth_pub_len == 33);

        // Step 8: Bob derives stealth private key
        // stealth_priv = bob_root_priv + key_offset (scalar addition mod n)
        unsigned char stealth_privkey[32];
        std::memcpy(stealth_privkey, bob_root_privkey, 32);
        ret = secp256k1_ec_seckey_tweak_add(ctx_secp, stealth_privkey, key_offset);
        BEAST_EXPECT(ret == 1);

        // Step 9: Verify the stealth private key matches the stealth public key
        secp256k1_pubkey derived_stealth_pubkey_raw;
        ret = secp256k1_ec_pubkey_create(
            ctx_secp, &derived_stealth_pubkey_raw, stealth_privkey);
        BEAST_EXPECT(ret == 1);

        // Compare the derived public key with the computed stealth public key
        unsigned char derived_stealth_pubkey[33];
        size_t derived_pub_len = 33;
        secp256k1_ec_pubkey_serialize(
            ctx_secp,
            derived_stealth_pubkey,
            &derived_pub_len,
            &derived_stealth_pubkey_raw,
            SECP256K1_EC_COMPRESSED);
        BEAST_EXPECT(derived_pub_len == 33);

        bool pubkeys_match = true;
        for (int i = 0; i < 33; i++)
        {
            if (stealth_pubkey[i] != derived_stealth_pubkey[i])
            {
                pubkeys_match = false;
                break;
            }
        }
        BEAST_EXPECT(pubkeys_match);

        // Step 10: Test signing with the derived stealth private key
        // Sign a dummy message
        unsigned char message[32];
        for (int i = 0; i < 32; i++)
            message[i] = i * 2;

        secp256k1_ecdsa_signature signature;
        ret = secp256k1_ecdsa_sign(
            ctx_secp, &signature, message, stealth_privkey, nullptr, nullptr);
        BEAST_EXPECT(ret == 1);

        // Step 11: Verify the signature using the stealth public key
        ret = secp256k1_ecdsa_verify(
            ctx_secp, &signature, message, &stealth_pubkey_raw);
        BEAST_EXPECT(ret == 1);

        // Cleanup
        secp256k1_context_destroy(ctx_secp);
    }

    void
    run() override
    {
        testAmendmentDisabled();
        testValidStealthPayment();
        testInvalidEphemeralKey();
        testMissingEphemeralKey();
        testInsufficientAmount();
        testNonXRPAmount();
        testSelfPayment();
        testDuplicateStealthAddress();
        testStealthAccountOnlyAccountDelete();
        testStealthAccountDelete();
        testStealthAccountCanReceiveRegularPayment();
        testStealthAccountCannotSendRegularPayment();
        testCannotSendStealthPaymentToExistingStealthAccount();
        testStealthAccountBlockedTransactionTypes();
        testStealthAccountFullLifecycle();
        testMissingDestination();
        testZeroOrNegativeAmount();
        testCryptographicKeyDerivation();
    }
};

BEAST_DEFINE_TESTSUITE(StealthPayment, app, ripple);

}  // namespace test
}  // namespace ripple
