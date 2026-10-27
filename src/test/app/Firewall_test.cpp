#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/paychan.h>
#include <test/jtx/proposal.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/tag.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class Firewall_test : public ConfidentialTransferTestBase
{
    // A counterparty signature adds one base fee to the minimum.
    static jtx::Fee
    signedFee(jtx::Env& env)
    {
        return jtx::Fee{STAmount{env.current()->fees().base * 2}};
    }

    static UInt256
    firewallID(jtx::Account const& account)
    {
        return keylet::firewall(account.id()).key;
    }

    static bool
    hasFirewall(jtx::Env const& env, jtx::Account const& account)
    {
        return env.le(keylet::firewall(account.id())) != nullptr;
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;

        // Without the amendment the transactions are disabled.
        {
            Env env{*this, features - featureFirewall};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, carol, backup);
            env.close();

            env(firewall::set(alice.id(), carol.id(), backup.id()), Ter(temDISABLED));
            env.close();
            BEAST_EXPECT(!hasFirewall(env, alice));
        }

        // With it, a firewall is created.
        {
            Env env{*this, features};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, carol, backup);
            env.close();

            auto const ownersBefore = ownerCount(env, alice);
            env(firewall::set(alice.id(), carol.id(), backup.id()));
            env.close();

            BEAST_EXPECT(hasFirewall(env, alice));
            // The firewall and the backup preauthorization.
            BEAST_EXPECT(ownerCount(env, alice) == ownersBefore + 2);

            auto const sle = env.le(keylet::firewall(alice.id()));
            BEAST_EXPECT(sle && sle->getAccountID(sfOwner) == alice.id());
            BEAST_EXPECT(sle && sle->getAccountID(sfCounterparty) == carol.id());
        }
    }

    void
    testCreatePreflight(FeatureBitset features)
    {
        testcase("create preflight");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        // The counterparty is required.
        {
            auto jv = firewall::set(alice.id(), carol.id(), backup.id());
            jv.removeMember(sfCounterparty.jsonName);
            env(jv, Ter(temMALFORMED));
        }

        // The backup is required.
        {
            auto jv = firewall::set(alice.id(), carol.id(), backup.id());
            jv.removeMember(sfBackup.jsonName);
            env(jv, Ter(temMALFORMED));
        }

        // Neither may be the account itself.
        env(firewall::set(alice.id(), alice.id(), backup.id()), Ter(temMALFORMED));
        env(firewall::set(alice.id(), carol.id(), alice.id()), Ter(temMALFORMED));

        // A creation carries no counterparty signature.
        env(firewall::set(alice.id(), carol.id(), backup.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temMALFORMED));

        env.close();
        BEAST_EXPECT(!hasFirewall(env, alice));
    }

    void
    testCreatePreclaim(FeatureBitset features)
    {
        testcase("create preclaim");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const absent{"absent"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        // The counterparty and the backup must exist.
        env(firewall::set(alice.id(), absent.id(), backup.id()), Ter(tecNO_DST));
        env(firewall::set(alice.id(), carol.id(), absent.id()), Ter(tecNO_DST));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        BEAST_EXPECT(hasFirewall(env, alice));

        // Only one firewall per account.
        env(firewall::set(alice.id(), carol.id(), backup.id()), Ter(tecDUPLICATE));
        env.close();
    }

    void
    testUpdate(FeatureBitset features)
    {
        testcase("update");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, dave, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // An update needs the counterparty's signature.
        env(firewall::set(alice.id(), fwID), Ter(temBAD_SIGNER));

        // Signed by someone other than the recorded counterparty.
        env(firewall::set(alice.id(), fwID),
            Sig(sfCounterpartySignature, dave),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env.close();

        // Signed by the counterparty, changing the counterparty.
        env(firewall::set(alice.id(), fwID),
            firewall::kCounterparty(dave),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();

        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && sle->getAccountID(sfCounterparty) == dave.id());

        // The old counterparty can no longer authorize.
        env(firewall::set(alice.id(), fwID),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env.close();
    }

    void
    testBlockedAndAllowed(FeatureBitset features)
    {
        testcase("blocked and allowed");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The backup was preauthorized when the firewall was created.
        env(pay(alice, backup, XRP(10)));
        env.close();

        // An unauthorized destination is blocked.
        env(pay(alice, stranger, XRP(10)), Ter(tefFIREWALL_BLOCK));

        // Paths are blocked even to a preauthorized destination, because they
        // can deliver somewhere the preauthorization check never sees. A
        // cross-currency send is used so the pathfinder actually sets sfPaths.
        Account const gw{"gw"};
        env.fund(XRP(10000), gw);
        env.close();
        env.trust(gw["USD"](1000), backup);
        env.close();
        env(offer(gw, XRP(100), gw["USD"](100)));
        env.close();

        env(pay(alice, backup, gw["USD"](10)), Paths(XRP), Ter(tefFIREWALL_BLOCK));

        // A blocked transaction type is rejected whatever its destination.
        env(offer(alice, XRP(10), alice["USD"](10)), Ter(tefFIREWALL_BLOCK));

        // A transaction the firewall allows still works.
        env(noop(alice));
        env.close();

        // An account without a firewall is unaffected.
        env(pay(stranger, alice, XRP(10)));
        env.close();
    }

    void
    testPreauth(FeatureBitset features)
    {
        testcase("preauth");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        env(pay(alice, stranger, XRP(10)), Ter(tefFIREWALL_BLOCK));

        // Preauthorizing needs the counterparty's signature.
        env(firewall::authorize(alice.id(), fwID, stranger.id()), Ter(temMALFORMED));

        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();

        // Now the payment goes through.
        env(pay(alice, stranger, XRP(10)));
        env.close();

        // Authorizing the same destination twice fails.
        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecDUPLICATE));

        // An account may not preauthorize itself.
        env(firewall::authorize(alice.id(), fwID, alice.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temCANNOT_PREAUTH_SELF));
        env.close();

        // Removing the preauthorization blocks it again.
        env(firewall::unauthorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();

        env(pay(alice, stranger, XRP(10)), Ter(tefFIREWALL_BLOCK));
        env.close();

        // Removing one that does not exist fails.
        env(firewall::unauthorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testMaxFee(FeatureBitset features)
    {
        testcase("max fee");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        auto jv = firewall::set(alice.id(), carol.id(), backup.id());
        jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
        env(jv);
        env.close();

        // A fee above the cap is blocked, one at or below it is not.
        env(noop(alice), Fee(XRP(2)), Ter(tefFIREWALL_BLOCK));
        env(noop(alice), Fee(XRP(1)));
        env.close();
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("account delete");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The ledger sequence must advance far enough for AccountDelete.
        for (int i = 0; i < 256; ++i)
            env.close();

        // A firewall is an obligation, so the account cannot be deleted while
        // one is set.
        env(acctdelete(alice, backup),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
    }

    void
    testDelete(FeatureBitset features)
    {
        testcase("delete");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        auto const ownersBefore = ownerCount(env, alice);
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore + 3);

        // A delete needs the counterparty's signature.
        env(firewall::del(alice.id(), fwID), Ter(temMALFORMED));
        env(firewall::del(alice.id(), fwID),
            Sig(sfCounterpartySignature, stranger),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env.close();

        env(firewall::del(alice.id(), fwID), Sig(sfCounterpartySignature, carol), signedFee(env));
        env.close();

        // The firewall and every preauthorization it owned are gone, and the
        // reserve comes back.
        BEAST_EXPECT(!hasFirewall(env, alice));
        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore);

        // Value moves freely again.
        env(pay(alice, stranger, XRP(10)));
        env.close();
    }

    void
    testDefaultAction()
    {
        testcase("default action");

        // A transaction that names no action, and a number with no transaction,
        // are blocked while a firewall is set.
        BEAST_EXPECT(TxSettings{}.firewall == FirewallAction::Block);
        BEAST_EXPECT(firewallAction(static_cast<TxType>(6)) == FirewallAction::Block);
    }

    void
    testActionTable()
    {
        testcase("action per transaction type");

        std::array<std::pair<TxType, FirewallAction>, 81> const expected{
            {
                {ttPAYMENT, FirewallAction::Check},
                {ttESCROW_CREATE, FirewallAction::Check},
                {ttESCROW_FINISH, FirewallAction::Allow},
                {ttACCOUNT_SET, FirewallAction::Allow},
                {ttESCROW_CANCEL, FirewallAction::Allow},
                {ttREGULAR_KEY_SET, FirewallAction::Allow},
                {ttOFFER_CREATE, FirewallAction::Block},
                {ttOFFER_CANCEL, FirewallAction::Allow},
                {ttTICKET_CREATE, FirewallAction::Allow},
                {ttSIGNER_LIST_SET, FirewallAction::Allow},
                {ttPAYCHAN_CREATE, FirewallAction::Check},
                {ttPAYCHAN_FUND, FirewallAction::Check},
                {ttPAYCHAN_CLAIM, FirewallAction::Allow},
                {ttCHECK_CREATE, FirewallAction::Check},
                {ttCHECK_CASH, FirewallAction::Allow},
                {ttCHECK_CANCEL, FirewallAction::Allow},
                {ttDEPOSIT_PREAUTH, FirewallAction::Allow},
                {ttTRUST_SET, FirewallAction::Allow},
                {ttACCOUNT_DELETE, FirewallAction::Allow},
                {ttNFTOKEN_MINT, FirewallAction::Check},
                {ttNFTOKEN_BURN, FirewallAction::Allow},
                {ttNFTOKEN_CREATE_OFFER, FirewallAction::Check},
                {ttNFTOKEN_CANCEL_OFFER, FirewallAction::Allow},
                {ttNFTOKEN_ACCEPT_OFFER, FirewallAction::Block},
                {ttCLAWBACK, FirewallAction::Allow},
                {ttAMM_CLAWBACK, FirewallAction::Allow},
                {ttAMM_CREATE, FirewallAction::Block},
                {ttAMM_DEPOSIT, FirewallAction::Block},
                {ttAMM_WITHDRAW, FirewallAction::Block},
                {ttAMM_VOTE, FirewallAction::Block},
                {ttAMM_BID, FirewallAction::Block},
                {ttAMM_DELETE, FirewallAction::Block},
                {ttDID_SET, FirewallAction::Allow},
                {ttDID_DELETE, FirewallAction::Allow},
                {ttORACLE_SET, FirewallAction::Allow},
                {ttORACLE_DELETE, FirewallAction::Allow},
                {ttLEDGER_STATE_FIX, FirewallAction::Allow},
                {ttMPTOKEN_ISSUANCE_CREATE, FirewallAction::Allow},
                {ttMPTOKEN_ISSUANCE_DESTROY, FirewallAction::Allow},
                {ttMPTOKEN_ISSUANCE_SET, FirewallAction::Allow},
                {ttMPTOKEN_AUTHORIZE, FirewallAction::Allow},
                {ttCREDENTIAL_CREATE, FirewallAction::Allow},
                {ttCREDENTIAL_ACCEPT, FirewallAction::Allow},
                {ttCREDENTIAL_DELETE, FirewallAction::Allow},
                {ttNFTOKEN_MODIFY, FirewallAction::Allow},
                {ttPERMISSIONED_DOMAIN_SET, FirewallAction::Allow},
                {ttPERMISSIONED_DOMAIN_DELETE, FirewallAction::Allow},
                {ttDELEGATE_SET, FirewallAction::Allow},
                {ttVAULT_CREATE, FirewallAction::Block},
                {ttVAULT_SET, FirewallAction::Block},
                {ttVAULT_DELETE, FirewallAction::Block},
                {ttVAULT_DEPOSIT, FirewallAction::Block},
                {ttVAULT_WITHDRAW, FirewallAction::Block},
                {ttVAULT_CLAWBACK, FirewallAction::Block},
                {ttBATCH, FirewallAction::Allow},
                {ttLOAN_BROKER_SET, FirewallAction::Block},
                {ttLOAN_BROKER_DELETE, FirewallAction::Block},
                {ttLOAN_BROKER_COVER_DEPOSIT, FirewallAction::Block},
                {ttLOAN_BROKER_COVER_WITHDRAW, FirewallAction::Block},
                {ttLOAN_BROKER_COVER_CLAWBACK, FirewallAction::Block},
                {ttLOAN_SET, FirewallAction::Block},
                {ttLOAN_DELETE, FirewallAction::Block},
                {ttLOAN_MANAGE, FirewallAction::Block},
                {ttLOAN_PAY, FirewallAction::Block},
                {ttCONFIDENTIAL_MPT_CONVERT, FirewallAction::Block},
                {ttCONFIDENTIAL_MPT_MERGE_INBOX, FirewallAction::Allow},
                {ttCONFIDENTIAL_MPT_CONVERT_BACK, FirewallAction::Block},
                {ttCONFIDENTIAL_MPT_SEND, FirewallAction::Block},
                {ttCONFIDENTIAL_MPT_CLAWBACK, FirewallAction::Allow},
                {ttSPONSORSHIP_TRANSFER, FirewallAction::Allow},
                {ttSPONSORSHIP_SET, FirewallAction::Allow},
                {ttCONFIDENTIAL_MPT_MIRROR_UPDATE, FirewallAction::Allow},
                {ttCONFIDENTIAL_MPT_HOLDER_KEY_UPDATE, FirewallAction::Allow},
                {ttTRANSACTION_PROPOSAL_CANCEL, FirewallAction::Allow},
                {ttTRANSACTION_PROPOSAL_CREATE, FirewallAction::Block},
                {ttAMENDMENT, FirewallAction::Allow},
                {ttFEE, FirewallAction::Allow},
                {ttUNL_MODIFY, FirewallAction::Allow},
                {ttFIREWALL_SET, FirewallAction::Allow},
                {ttFIREWALL_DELETE, FirewallAction::Allow},
                {ttWITHDRAW_PREAUTH, FirewallAction::Allow},
            },
        };

        for (auto const& [type, action] : expected)
        {
            BEAST_EXPECTS(
                firewallAction(type) == action,
                TxFormats::getInstance().findByType(type)->getName());
        }
    }

    void
    testNFTokenAcceptOffer(FeatureBitset features)
    {
        testcase("NFTokenAcceptOffer");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();

        UInt256 const aliceNft = token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0), Txflags(tfTransferable));
        UInt256 const bobNft = token::getNextID(env, bob, 0, tfTransferable);
        env(token::mint(bob, 0), Txflags(tfTransferable));
        env.close();

        UInt256 const sellOffer =
            keylet::nftokenOffer(bob, SeqProxy::rawSequence(env.seq(bob))).key;
        env(token::createOffer(bob, bobNft, XRP(10)), Txflags(tfSellNFToken));
        UInt256 const buyOffer =
            keylet::nftokenOffer(carol, SeqProxy::rawSequence(env.seq(carol))).key;
        env(token::createOffer(carol, aliceNft, XRP(10)), token::Owner(alice));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // Accepting a sell offer pays its owner, whom no destination field names.
        env(token::acceptSellOffer(alice, sellOffer), Ter(tefFIREWALL_BLOCK));

        // Accepting a buy offer hands the NFT to the offer's owner.
        env(token::acceptBuyOffer(alice, buyOffer), Ter(tefFIREWALL_BLOCK));
        env.close();
    }

    void
    testConfidentialMirrorUpdate(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const newIssuerKey{"newIssuerKey"};
        ConfidentialEnv ct{env, alice, {{.account = bob}}};
        env.fund(XRP(10000), carol, backup);
        env.close();

        ct.mpt.generateKeyPair(newIssuerKey);
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});
        Buffer const newIssuerCipher =
            ct.mpt.encryptAmount(newIssuerKey, 100, generateBlindingFactor());

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // Re-encrypting a holder's issuer mirror moves no value, so a firewalled issuer may.
        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = newIssuerCipher,
        });

        auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
    }

    void
    testConfidentialHolderKeyUpdate(FeatureBitset features)
    {
        testcase("ConfidentialMPTHolderKeyUpdate");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const bobNewKey{"bobNewKey"};
        ConfidentialEnv ct{env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};
        env.fund(XRP(10000), carol, backup);
        env.close();

        env(firewall::set(bob.id(), carol.id(), backup.id()));
        env.close();

        ct.mpt.generateKeyPair(bobNewKey);
        auto const reEnc = reencryptHolderBalances(ct.mpt, bob, bob, bobNewKey);
        BEAST_EXPECT(reEnc.has_value());
        if (!reEnc)
            return;

        // Rotating the holder's own encryption key moves no value, so a firewalled holder may.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .spendingCiphertext = reEnc->first,
            .inboxCiphertext = reEnc->second,
            .proof = gMakeZeroBuffer(1),
            .flags = tfHolderKeyRotation,
        });

        auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
        if (!BEAST_EXPECT(sle))
            return;
        auto const newPubKey = ct.mpt.getPubKey(bobNewKey);
        BEAST_EXPECT(newPubKey && strHex((*sle)[sfHolderEncryptionKey]) == strHex(*newPubKey));
    }

    void
    testTransactionProposalCancel(FeatureBitset features)
    {
        testcase("TransactionProposalCancel");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, target, bob, carol, backup);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        proposal::authorizeProposer(env, target, alice);
        env(proposal::create(
            alice,
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq),
            proposal::expiration(env, 1000s)));
        env.close();
        UInt256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // Cancelling a proposal only releases the owner's reserve, so a firewalled owner may.
        env(proposal::cancel(alice, proposalID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
    }

    // The engine result of jv carrying a counterparty signing key that is not a
    // public key. Simulation skips signature verification, which would refuse
    // the key before preflight.
    static std::string
    simulateBadCounterpartyKey(jtx::Env& env, json::Value jv)
    {
        json::Value sig = json::ValueType::Object;
        sig[sfSigningPubKey.jsonName] = "0102";
        jv[sfCounterpartySignature.jsonName] = sig;
        json::Value params;
        params[jss::tx_json] = jv;
        return env.rpc("json", "simulate", to_string(params))[jss::result][jss::engine_result]
            .asString();
    }

    // The fee for a transaction carrying a counterparty signature of n signers.
    static jtx::Fee
    multiSignedFee(jtx::Env& env, std::uint32_t n)
    {
        return jtx::Fee{STAmount{env.current()->fees().base * (1 + n)}};
    }

    void
    testUpdatePreflight(FeatureBitset features)
    {
        testcase("update preflight");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, carol, backup, gw);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // The backup is set only on creation.
        env(firewall::set(alice.id(), fwID),
            firewall::kBackup(backup),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temMALFORMED));

        // The new counterparty may not be the account itself.
        env(firewall::set(alice.id(), fwID),
            firewall::kCounterparty(alice),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temMALFORMED));

        // A counterparty signing key that is not a public key.
        BEAST_EXPECT(
            simulateBadCounterpartyKey(env, firewall::set(alice.id(), fwID)) == "temBAD_SIGNATURE");

        // MaxFee must be a non-negative XRP amount.
        for (json::Value const& maxFee :
             {json::Value{"-1"}, gw["USD"](1).value().getJson(JsonOptions::Values::None)})
        {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfMaxFee.jsonName] = maxFee;
            env(jv, Sig(sfCounterpartySignature, carol), signedFee(env), Ter(temBAD_AMOUNT));
        }
        env.close();
    }

    void
    testUpdatePreclaim(FeatureBitset features)
    {
        testcase("update preclaim");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const absent{"absent"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env(firewall::set(bob.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // A firewall ID that names no firewall.
        env(firewall::set(alice.id(), UInt256{1}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_TARGET));

        // Another account's firewall.
        env(firewall::set(alice.id(), firewallID(bob)),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_PERMISSION));

        // The counterparty already recorded.
        env(firewall::set(alice.id(), fwID),
            firewall::kCounterparty(carol),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecDUPLICATE));

        // A new counterparty that does not exist.
        env(firewall::set(alice.id(), fwID),
            firewall::kCounterparty(absent),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_DST));
        env.close();
    }

    void
    testUpdateMaxFee(FeatureBitset features)
    {
        testcase("update max fee");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // An update that only sets a cap keeps the counterparty.
        {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
            env(jv, Sig(sfCounterpartySignature, carol), signedFee(env));
            env.close();
        }
        auto sle = env.le(keylet::firewall(alice.id()));
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(sle->getFieldAmount(sfMaxFee) == XRP(1));
        BEAST_EXPECT(sle->getAccountID(sfCounterparty) == carol.id());
        env(noop(alice), Fee(XRP(2)), Ter(tefFIREWALL_BLOCK));

        // A zero cap removes it.
        {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfMaxFee.jsonName] = "0";
            env(jv, Sig(sfCounterpartySignature, carol), signedFee(env));
            env.close();
        }
        sle = env.le(keylet::firewall(alice.id()));
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(!sle->isFieldPresent(sfMaxFee));
        env(noop(alice), Fee(XRP(2)));
        env.close();
    }

    void
    testMultiSignedCounterparty(FeatureBitset features)
    {
        testcase("multi-signed counterparty");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        Account const dave{"dave"};
        Account const erin{"erin"};
        env.fund(XRP(10000), alice, carol, backup, stranger, dave, erin);
        env.close();

        env(signers(carol, 2, {{dave, 1}, {erin, 1}}));
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // Each signer of the counterparty's signature adds one base fee.
        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Msig(sfCounterpartySignature, dave, erin),
            signedFee(env),
            Ter(telINSUF_FEE_P));
        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Msig(sfCounterpartySignature, dave, erin),
            multiSignedFee(env, 2));
        env.close();
        env(pay(alice, stranger, XRP(10)));

        auto jv = firewall::set(alice.id(), fwID);
        jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
        env(jv, Msig(sfCounterpartySignature, dave, erin), signedFee(env), Ter(telINSUF_FEE_P));
        env(jv, Msig(sfCounterpartySignature, dave, erin), multiSignedFee(env, 2));
        env.close();

        env(firewall::del(alice.id(), fwID),
            Msig(sfCounterpartySignature, dave, erin),
            signedFee(env),
            Ter(telINSUF_FEE_P));
        env(firewall::del(alice.id(), fwID),
            Msig(sfCounterpartySignature, dave, erin),
            multiSignedFee(env, 2));
        env.close();
        BEAST_EXPECT(!hasFirewall(env, alice));
    }

    void
    testReserve(FeatureBitset features)
    {
        testcase("reserve");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        auto const base = env.current()->fees().base;
        env.fund(XRP(10000), carol, backup, stranger);
        // alice can hold one object, bob two.
        env.fund(drops(env.current()->fees().accountReserve(1, 1) + base * 10), alice);
        env.fund(drops(env.current()->fees().accountReserve(2, 1) + base * 10), bob);
        env.close();

        // A firewall and its backup preauthorization take two reserves.
        env(firewall::set(alice.id(), carol.id(), backup.id()), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!hasFirewall(env, alice));

        // A further preauthorization takes a third.
        env(firewall::set(bob.id(), carol.id(), backup.id()));
        env.close();
        env(firewall::authorize(bob.id(), firewallID(bob), stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!env.le(keylet::withdrawPreauth(bob.id(), stranger.id(), 0)));
    }

    void
    testWithdrawPreauthPreflight(FeatureBitset features)
    {
        testcase("preauth preflight");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // Exactly one of Authorize and Unauthorize.
        {
            auto jv = firewall::authorize(alice.id(), fwID, stranger.id());
            jv[sfUnauthorize.jsonName] = backup.human();
            env(jv, Sig(sfCounterpartySignature, carol), signedFee(env), Ter(temMALFORMED));
        }
        {
            auto jv = firewall::authorize(alice.id(), fwID, stranger.id());
            jv.removeMember(sfAuthorize.jsonName);
            env(jv, Sig(sfCounterpartySignature, carol), signedFee(env), Ter(temMALFORMED));
        }

        // The zero account cannot be named.
        env(firewall::authorize(alice.id(), fwID, AccountID{}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temINVALID_ACCOUNT_ID));

        // A counterparty signing key that is not a public key.
        BEAST_EXPECT(
            simulateBadCounterpartyKey(env, firewall::authorize(alice.id(), fwID, stranger.id())) ==
            "temBAD_SIGNATURE");
        env.close();
    }

    void
    testWithdrawPreauthPreclaim(FeatureBitset features)
    {
        testcase("preauth preclaim");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        Account const absent{"absent"};
        env.fund(XRP(10000), alice, bob, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The account to preauthorize must exist.
        env(firewall::authorize(alice.id(), firewallID(alice), absent.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_TARGET));

        // An account without a firewall has nothing to preauthorize.
        env(firewall::authorize(bob.id(), firewallID(alice), stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_TARGET));

        // The firewall ID must be the account's own.
        env(firewall::set(bob.id(), carol.id(), backup.id()));
        env.close();
        env(firewall::authorize(alice.id(), firewallID(bob), stranger.id()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testDeleteChecks(FeatureBitset features)
    {
        testcase("delete checks");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env(firewall::set(bob.id(), carol.id(), backup.id()));
        env.close();

        // A counterparty signing key that is not a public key.
        BEAST_EXPECT(
            simulateBadCounterpartyKey(env, firewall::del(alice.id(), firewallID(alice))) ==
            "temBAD_SIGNATURE");

        // A firewall ID that names no firewall.
        env(firewall::del(alice.id(), UInt256{1}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_TARGET));

        // Another account's firewall.
        env(firewall::del(alice.id(), firewallID(bob)),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(hasFirewall(env, alice));
        BEAST_EXPECT(hasFirewall(env, bob));
    }

    void
    testCheckedTransactions(FeatureBitset features)
    {
        testcase("checked transactions");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, carol, backup, stranger, gw);
        env.close();
        UInt256 const aliceNft = token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0), Txflags(tfTransferable));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // A checked type other than Payment goes to a preauthorized destination.
        auto const escrowSeq = env.seq(alice);
        env(escrow::create(alice, backup, XRP(10)), escrow::kFinishTime(env.now() + 1s));
        env(escrow::create(alice, stranger, XRP(10)),
            escrow::kFinishTime(env.now() + 1s),
            Ter(tefFIREWALL_BLOCK));
        env.close();

        // A checked type that names no destination is blocked.
        env(token::createOffer(alice, aliceNft, XRP(10)),
            Txflags(tfSellNFToken),
            Ter(tefFIREWALL_BLOCK));

        // A payment to itself is blocked even without paths.
        env(pay(alice, alice, gw["USD"](10)), Sendmax(XRP(20)), Ter(tefFIREWALL_BLOCK));
        env.close();

        // The escrow to the preauthorized backup can be finished.
        env(escrow::finish(backup, alice, escrowSeq));
        env.close();
    }

    void
    testAccountDeleteWithPreauths(FeatureBitset features)
    {
        testcase("account delete with preauthorizations");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // Enough preauthorizations that some sort ahead of the firewall in
        // the owner directory, so AccountDelete reaches them before it.
        std::vector<Account> preauthorized;
        preauthorized.reserve(8);
        for (int i = 0; i < 8; ++i)
            preauthorized.emplace_back("preauthorized" + std::to_string(i));
        for (auto const& account : preauthorized)
        {
            env.fund(XRP(1000), account);
            env.close();
            env(firewall::authorize(alice.id(), fwID, account.id()),
                Sig(sfCounterpartySignature, carol),
                signedFee(env));
            env.close();
        }

        for (int i = 0; i < 256; ++i)
            env.close();

        env(acctdelete(alice, backup),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
        BEAST_EXPECT(hasFirewall(env, alice));
    }

    void
    testLedgerEntry(FeatureBitset features)
    {
        testcase("ledger_entry");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()), Dtag(7));
        env.close();

        auto const ledgerEntry = [&env](json::Value const& params) {
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };

        // A firewall by its owner's address.
        {
            json::Value params;
            params[jss::firewall] = alice.human();
            auto const jrr = ledgerEntry(params);
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Firewall);
            BEAST_EXPECT(jrr[jss::node][sfOwner.jsonName] == alice.human());
        }
        {
            json::Value params;
            params[jss::firewall] = "not an address";
            BEAST_EXPECT(ledgerEntry(params)[jss::error] == "malformedAddress");
        }

        // A preauthorization by owner, authorized account and tag, and by ID.
        std::string preauthIndex;
        {
            json::Value params;
            params[jss::withdraw_preauth][jss::owner] = alice.human();
            params[jss::withdraw_preauth][jss::authorized] = backup.human();
            params[jss::withdraw_preauth][jss::destination_tag] = 7;
            auto const jrr = ledgerEntry(params);
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::WithdrawPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == backup.human());
            preauthIndex = jrr[jss::node][jss::index].asString();
        }
        {
            json::Value params;
            params[jss::withdraw_preauth] = preauthIndex;
            auto const jrr = ledgerEntry(params);
            BEAST_EXPECT(jrr[jss::node][jss::index] == preauthIndex);
        }

        // A tag that is not an integer counts as no tag.
        {
            json::Value params;
            params[jss::withdraw_preauth][jss::owner] = alice.human();
            params[jss::withdraw_preauth][jss::authorized] = backup.human();
            params[jss::withdraw_preauth][jss::destination_tag] = "7";
            BEAST_EXPECT(ledgerEntry(params)[jss::error] == "entryNotFound");
        }

        // No tag names the preauthorization with tag 0.
        {
            json::Value params;
            params[jss::withdraw_preauth][jss::owner] = alice.human();
            params[jss::withdraw_preauth][jss::authorized] = backup.human();
            BEAST_EXPECT(ledgerEntry(params)[jss::error] == "entryNotFound");
        }

        // The owner and the authorized account are required.
        {
            json::Value params;
            params[jss::withdraw_preauth][jss::authorized] = backup.human();
            BEAST_EXPECT(ledgerEntry(params).isMember(jss::error));
        }
        {
            json::Value params;
            params[jss::withdraw_preauth][jss::owner] = alice.human();
            BEAST_EXPECT(ledgerEntry(params).isMember(jss::error));
        }
    }

    void
    testBatchInnerBlocked(FeatureBitset features)
    {
        testcase("batch inner blocked");
        using namespace jtx;

        for (std::uint32_t const mode : {tfIndependent, tfAllOrNothing})
        {
            std::string logs;
            std::string innerID;
            {
                Env env{
                    *this,
                    envconfig(),
                    features,
                    std::make_unique<CaptureLogs>(&logs),
                    beast::Severity::Debug};
                Account const alice{"alice"};
                Account const carol{"carol"};
                Account const backup{"backup"};
                Account const stranger{"stranger"};
                env.fund(XRP(10000), alice, carol, backup, stranger);
                env.close();

                env(firewall::set(alice.id(), carol.id(), backup.id()));
                env.close();

                auto const backupBefore = env.balance(backup);
                auto const strangerBefore = env.balance(stranger);

                // The Batch itself is allowed; each inner transaction meets the
                // firewall on its own.
                auto const seq = env.seq(alice);
                auto const batchTxn = env.jt(
                    batch::outer(alice, seq, batch::calcBatchFee(env, 0, 2), mode),
                    batch::Inner(pay(alice, backup, XRP(10)), seq + 1),
                    batch::Inner(pay(alice, stranger, XRP(10)), seq + 2));
                innerID = to_string(batchTxn.stx->getBatchTransactionIDs()[1]);
                env(batchTxn);
                env.close();

                BEAST_EXPECT(env.balance(stranger) == strangerBefore);
                if (mode == tfIndependent)
                {
                    BEAST_EXPECT(env.balance(backup) == backupBefore + XRP(10));
                }
                else
                {
                    BEAST_EXPECT(env.balance(backup) == backupBefore);
                }
            }
            BEAST_EXPECT(logs.contains(innerID + " failure: tefFIREWALL_BLOCK"));
        }
    }

    void
    testMaxFeeOutOfRange(FeatureBitset features)
    {
        testcase("max fee out of range");
        using namespace jtx;

        // A native amount above kMaxNativeN, which only decoding a serialized
        // amount produces.
        Serializer raw;
        raw.add64(std::uint64_t{0x4000000000000000} | (STAmount::kMaxNativeN + 1));
        SerialIter sit(raw.slice());
        STAmount const tooBig{sit, sfMaxFee};

        for (bool const cleanup : {false, true})
        {
            Env env{*this, cleanup ? features : features - fixCleanup3_2_0};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, carol, backup);
            env.close();

            STTx tx{*env.jt(firewall::set(alice.id(), carol.id(), backup.id())).stx};
            tx.setFieldAmount(sfMaxFee, tooBig);
            tx.sign(alice.pk(), alice.sk());

            // FirewallSet's own MaxFee check refuses it; with fixCleanup3_2_0
            // the universal amount check refuses it first.
            auto const jr = env.rpc("submit", strHex(tx.getSerializer().slice()));
            BEAST_EXPECT(jr[jss::result][jss::engine_result] == "temBAD_AMOUNT");
            env.close();
            BEAST_EXPECT(!hasFirewall(env, alice));
        }
    }

    void
    testBatchInnerManagement(FeatureBitset features)
    {
        testcase("batch inner firewall management");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, carol, backup, stranger, bob);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        auto withMaxFee = firewall::set(alice.id(), fwID);
        withMaxFee[sfMaxFee.jsonName] = to_string(XRP(1).value());

        // An inner Batch transaction carries no signature of its own, so it
        // cannot carry the counterparty's consent; the firewall types are
        // refused as inner transactions.
        json::Value garbage = json::ValueType::Object;
        garbage[jss::SigningPubKey] = strHex(carol.pk().slice());
        garbage[jss::TxnSignature] = "DEADBEEF";
        json::Value noSignature = json::ValueType::Object;
        noSignature[jss::SigningPubKey] = "";

        struct Case
        {
            json::Value inner;
            std::optional<json::Value> counterSig;
            TER expected;
        };
        std::vector<Case> const cases{
            {
                .inner = firewall::del(alice.id(), fwID),
                .counterSig = garbage,
                .expected = temBAD_SIGNATURE,
            },
            {
                .inner = firewall::del(alice.id(), fwID),
                .counterSig = noSignature,
                .expected = temINVALID_INNER_BATCH,
            },
            {
                .inner = firewall::del(alice.id(), fwID),
                .counterSig = std::nullopt,
                .expected = temMALFORMED,
            },
            {
                .inner = withMaxFee,
                .counterSig = garbage,
                .expected = temBAD_SIGNATURE,
            },
            {
                .inner = withMaxFee,
                .counterSig = noSignature,
                .expected = temINVALID_INNER_BATCH,
            },
            {
                .inner = withMaxFee,
                .counterSig = std::nullopt,
                .expected = temINVALID_INNER_BATCH,
            },
            {
                .inner = firewall::authorize(alice.id(), fwID, stranger.id()),
                .counterSig = garbage,
                .expected = temBAD_SIGNATURE,
            },
            {
                .inner = firewall::authorize(alice.id(), fwID, stranger.id()),
                .counterSig = noSignature,
                .expected = temINVALID_INNER_BATCH,
            },
            {
                .inner = firewall::authorize(alice.id(), fwID, stranger.id()),
                .counterSig = std::nullopt,
                .expected = temMALFORMED,
            },
        };

        for (auto const& [inner, counterSig, expected] : cases)
        {
            auto jv = inner;
            if (counterSig)
                jv[sfCounterpartySignature.jsonName] = *counterSig;
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batch::calcBatchFee(env, 0, 2), tfAllOrNothing),
                batch::Inner(jv, seq + 1),
                batch::Inner(noop(alice), seq + 2),
                Ter(expected));
            env.close();

            auto const sle = env.le(keylet::firewall(alice.id()));
            BEAST_EXPECT(sle && !sle->isFieldPresent(sfMaxFee));
            BEAST_EXPECT(!env.le(keylet::withdrawPreauth(alice.id(), stranger.id(), 0)));
        }

        // A creation needs no consent, but the rule is per type.
        auto const seq = env.seq(bob);
        env(batch::outer(bob, seq, batch::calcBatchFee(env, 0, 2), tfAllOrNothing),
            batch::Inner(firewall::set(bob.id(), carol.id(), backup.id()), seq + 1),
            batch::Inner(noop(bob), seq + 2),
            Ter(temINVALID_INNER_BATCH));
        env.close();
        BEAST_EXPECT(!hasFirewall(env, bob));
    }

    void
    testCrossCurrencyPayment(FeatureBitset features)
    {
        testcase("cross-currency payment");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        Account const maker{"maker"};
        env.fund(XRP(10000), alice, carol, backup, gw, maker);
        env.close();
        auto const usd = gw["USD"];
        env(trust(alice, usd(1000), tfSetNoRipple));
        env(trust(backup, usd(1000), tfSetNoRipple));
        env(trust(maker, usd(1000), tfSetNoRipple));
        env.close();
        env(pay(gw, alice, usd(100)));
        env(pay(gw, maker, usd(100)));
        env.close();
        // An offer selling 1 USD for 1000 XRP.
        auto const makerOfferSeq = env.seq(maker);
        env(offer(maker, XRP(1000), usd(1)));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // Paying a preauthorized destination in another asset than the one
        // spent would route through the order book without any Paths.
        auto const aliceXrp = env.balance(alice);
        env(pay(alice, backup, usd(1)), Sendmax(XRP(1000)), Ter(tefFIREWALL_BLOCK));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceXrp);
        BEAST_EXPECT(env.le(xrpl::keylet::offer(maker.id(), SeqProxy::rawSequence(makerOfferSeq))));

        // A SendMax in the delivered asset is allowed, and a same-asset
        // payment does not cross the book.
        env(pay(alice, backup, usd(1)), Sendmax(usd(1)));
        env(pay(alice, backup, usd(1)));
        env.close();
        BEAST_EXPECT(env.balance(backup, usd) == usd(2));
        BEAST_EXPECT(env.le(xrpl::keylet::offer(maker.id(), SeqProxy::rawSequence(makerOfferSeq))));
    }

    void
    testRippling(FeatureBitset features)
    {
        testcase("rippling");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        Account const other{"other"};
        Account const holder{"holder"};
        env.fund(XRP(10000), alice, carol, backup, gw, other, holder);
        env.close();
        env(fset(gw, asfDefaultRipple));
        env(trust(alice, gw["USD"](1000), tfSetNoRipple));
        // A line that ripples on alice's side, made before the firewall.
        env(trust(alice, gw["EUR"](1000)));
        env(trust(holder, gw["USD"](1000)));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env(firewall::set(gw.id(), carol.id(), backup.id()));
        env.close();

        // A holder may not make or leave a line that ripples while it holds a
        // limit on it.
        env(trust(alice, gw["USD"](1000), tfClearNoRipple), Ter(tefFIREWALL_BLOCK));
        env(trust(alice, other["USD"](1000)), Ter(tefFIREWALL_BLOCK));
        env(trust(alice, gw["EUR"](2000)), Ter(tefFIREWALL_BLOCK));
        env(fset(alice, asfDefaultRipple), Ter(tefFIREWALL_BLOCK));
        env.close();

        // Lines that cannot ripple are fine.
        env(trust(alice, other["USD"](1000), tfSetNoRipple));
        env(trust(alice, gw["EUR"](2000), tfSetNoRipple));
        env(trust(alice, gw["USD"](500)));
        env.close();

        // An issuer's own lines ripple with a zero limit on its side, so it can
        // still freeze them.
        env(trust(gw, holder["USD"](0), tfSetFreeze));
        env.close();
        auto const line = env.le(keylet::trustLine(gw.id(), holder.id(), gw["USD"].currency));
        BEAST_EXPECT(line && line->isFlag(gw.id() > holder.id() ? lsfHighFreeze : lsfLowFreeze));
    }

    void
    testCreateMaxFeeZero(FeatureBitset features)
    {
        testcase("create max fee zero");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        // A zero cap means no cap, as on an update.
        auto jv = firewall::set(alice.id(), carol.id(), backup.id());
        jv[sfMaxFee.jsonName] = "0";
        env(jv);
        env.close();
        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfMaxFee));
        env(noop(alice));
        env(firewall::del(alice.id(), firewallID(alice)),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!hasFirewall(env, alice));
    }

    void
    testBackupPreauthKept(FeatureBitset features)
    {
        testcase("backup preauthorization kept");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()), Dtag(9));
        env.close();
        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && sle->getAccountID(sfBackup) == backup.id());
        BEAST_EXPECT(sle && (*sle)[~sfDestinationTag] == 9u);

        // The backup's preauthorization cannot be removed while the firewall
        // exists, so one destination always stays reachable.
        env(firewall::unauthorize(alice.id(), firewallID(alice), backup.id()),
            Dtag(9),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(env.le(keylet::withdrawPreauth(alice.id(), backup.id(), 9)));
    }

    void
    testSigningAuthority(FeatureBitset features)
    {
        testcase("signing authority");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const dave{"dave"};
        Account const newKey{"newKey"};
        env.fund(XRP(10000), alice, carol, backup, dave, newKey);
        env.close();
        env(regkey(alice, dave));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The master key cannot be disabled while a firewall exists.
        env(fset(alice, asfDisableMaster), Sig(alice), Ter(tefFIREWALL_BLOCK));

        // A new regular key or signer list needs the counterparty's consent.
        env(regkey(alice, newKey), Ter(tefFIREWALL_BLOCK));
        env(regkey(alice, newKey),
            Sig(sfCounterpartySignature, dave),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env(signers(alice, 1, {{dave, 1}}), Ter(tefFIREWALL_BLOCK));
        env(signers(alice, 1, {{dave, 1}}),
            Sig(sfCounterpartySignature, dave),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env.close();
        auto sle = env.le(keylet::account(alice.id()));
        BEAST_EXPECT(sle && sle->getAccountID(sfRegularKey) == dave.id());
        BEAST_EXPECT(!env.le(keylet::signerList(alice.id())));

        env(regkey(alice, newKey), Sig(sfCounterpartySignature, carol), signedFee(env));
        env(signers(alice, 1, {{dave, 1}}), Sig(sfCounterpartySignature, carol), signedFee(env));
        env.close();
        sle = env.le(keylet::account(alice.id()));
        BEAST_EXPECT(sle && sle->getAccountID(sfRegularKey) == newKey.id());
        BEAST_EXPECT(env.le(keylet::signerList(alice.id())));

        // An inner Batch transaction cannot carry the consent.
        auto inner = regkey(alice, dave);
        json::Value noSignature = json::ValueType::Object;
        noSignature[jss::SigningPubKey] = "";
        inner[sfCounterpartySignature.jsonName] = noSignature;
        auto const seq = env.seq(alice);
        env(batch::outer(alice, seq, batch::calcBatchFee(env, 0, 2), tfIndependent),
            batch::Inner(inner, seq + 1),
            batch::Inner(noop(alice), seq + 2));
        env.close();
        sle = env.le(keylet::account(alice.id()));
        BEAST_EXPECT(sle && sle->getAccountID(sfRegularKey) == newKey.id());
    }

    void
    testTypesWithoutDestination(FeatureBitset features)
    {
        testcase("types without a destination");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        // Made before the firewall: an escrow and a channel to a destination
        // the firewall does not preauthorize.
        auto const cancelSeq = env.seq(alice);
        env(escrow::create(alice, stranger, XRP(10)),
            escrow::kFinishTime(env.now() + 1s),
            escrow::kCancelTime(env.now() + 2s));
        auto const strangerChan = paychan::channel(alice, stranger, env.seq(alice));
        env(paychan::create(alice, stranger, XRP(100), 100s, alice.pk()));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        auto const finishSeq = env.seq(alice);
        env(escrow::create(alice, backup, XRP(10)), escrow::kFinishTime(env.now() + 1s));
        auto const backupChan = paychan::channel(alice, backup, env.seq(alice));
        env(paychan::create(alice, backup, XRP(100), 100s, alice.pk()));
        env.close();
        env.close();

        // The owner finishes an escrow it already committed, and cancels one
        // back to itself.
        env(escrow::finish(alice, alice, finishSeq));
        env(escrow::cancel(alice, alice, cancelSeq));

        // Funding a channel checks the channel's destination.
        env(paychan::fund(alice, backupChan, XRP(10)));
        env(paychan::fund(alice, strangerChan, XRP(10)), Ter(tefFIREWALL_BLOCK));
        env.close();
        BEAST_EXPECT(paychan::channelAmount(*env.current(), backupChan) == XRP(110));
        BEAST_EXPECT(paychan::channelAmount(*env.current(), strangerChan) == XRP(100));
    }

    void
    testChannelClaim(FeatureBitset features)
    {
        testcase("channel claim");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        auto const strangerChan = paychan::channel(alice, stranger, env.seq(alice));
        env(paychan::create(alice, stranger, XRP(100), 100s, alice.pk()));
        auto const backupChan = paychan::channel(alice, backup, env.seq(alice));
        env(paychan::create(alice, backup, XRP(100), 100s, alice.pk()));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The source raising the paid balance sends value to the destination.
        env(paychan::claim(alice, strangerChan, XRP(50)), Ter(tefFIREWALL_BLOCK));
        env(paychan::claim(alice, backupChan, XRP(50)));
        env.close();
        BEAST_EXPECT(paychan::channelBalance(*env.current(), strangerChan) == XRP(0));
        BEAST_EXPECT(paychan::channelBalance(*env.current(), backupChan) == XRP(50));

        // Closing without a balance returns the rest to the source.
        env(paychan::claim(alice, strangerChan), Txflags(tfClose));
        env.close();
    }

    void
    testConfidentialMergeInbox(FeatureBitset features)
    {
        testcase("ConfidentialMPTMergeInbox");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        ConfidentialEnv ct{env, alice, {{.account = bob}}};
        env.fund(XRP(10000), carol, backup);
        env.close();

        env(firewall::set(bob.id(), carol.id(), backup.id()));
        env.close();

        // Merging the holder's own inbox moves nothing away from it.
        ct.mpt.mergeInbox({.account = bob});
    }

    void
    testSigningAuthorityChecks(FeatureBitset features)
    {
        testcase("signing authority checks");
        using namespace jtx;

        // Without the amendment a counterparty signature on these types is
        // not accepted.
        {
            Env env{*this, features - featureFirewall};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const dave{"dave"};
            env.fund(XRP(10000), alice, carol, dave);
            env.close();
            env(regkey(alice, dave),
                Sig(sfCounterpartySignature, carol),
                signedFee(env),
                Ter(temDISABLED));
            env(signers(alice, 1, {{dave, 1}}),
                Sig(sfCounterpartySignature, carol),
                signedFee(env),
                Ter(temDISABLED));
        }

        // A counterparty signing key that is not a public key.
        Env env{*this, features};
        Account const alice{"alice"};
        Account const dave{"dave"};
        env.fund(XRP(10000), alice, dave);
        env.close();
        BEAST_EXPECT(simulateBadCounterpartyKey(env, regkey(alice, dave)) == "temBAD_SIGNATURE");
        BEAST_EXPECT(
            simulateBadCounterpartyKey(env, signers(alice, 1, {{dave, 1}})) == "temBAD_SIGNATURE");
    }

    void
    testPreauthSigners(FeatureBitset features)
    {
        testcase("preauth signers and tags");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // Only the recorded counterparty authorizes a preauthorization.
        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, stranger),
            signedFee(env),
            Ter(tefBAD_AUTH));

        // The backup under another tag is an ordinary preauthorization.
        env(firewall::authorize(alice.id(), fwID, backup.id()),
            Dtag(5),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        env(firewall::unauthorize(alice.id(), fwID, backup.id()),
            Dtag(5),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!env.le(keylet::withdrawPreauth(alice.id(), backup.id(), 5)));
        BEAST_EXPECT(env.le(keylet::withdrawPreauth(alice.id(), backup.id(), 0)));
    }

    void
    testRipplingSides(FeatureBitset features)
    {
        testcase("rippling on either side of a line");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);

        // One issuer whose account ID sorts below alice's and one above, so
        // alice is the high side of one line and the low side of the other.
        std::vector<Account> lower;
        std::vector<Account> higher;
        for (int i = 0; i < 16; ++i)
        {
            Account const peer{"issuer" + std::to_string(i)};
            (peer.id() < alice.id() ? lower : higher).push_back(peer);
        }
        BEAST_EXPECT(!lower.empty() && !higher.empty());
        if (lower.empty() || higher.empty())
            return;
        Account const low = lower.front();
        Account const high = higher.front();
        env.fund(XRP(10000), low, high);
        env.close();
        env(trust(alice, low["USD"](100), tfSetNoRipple));
        env(trust(alice, high["USD"](100), tfSetNoRipple));
        env(trust(alice, low["EUR"](100)));
        env(trust(alice, high["EUR"](100)));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // An existing line keeps its side's NoRipple, whichever side alice is.
        env(trust(alice, low["USD"](200)));
        env(trust(alice, high["USD"](200)));
        env(trust(alice, low["EUR"](200)), Ter(tefFIREWALL_BLOCK));
        env(trust(alice, high["EUR"](200)), Ter(tefFIREWALL_BLOCK));
        env.close();
    }

    void
    testChannelChecks(FeatureBitset features)
    {
        testcase("channel checks");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();

        auto const bobChan = paychan::channel(bob, alice, env.seq(bob));
        env(paychan::create(bob, alice, XRP(100), 100s, bob.pk()));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The destination's claim brings value in.
        auto const sig = paychan::signClaimAuth(bob.pk(), bob.sk(), bobChan, XRP(10));
        env(paychan::claim(alice, bobChan, XRP(10), XRP(10), Slice(sig), bob.pk()));
        env.close();
        BEAST_EXPECT(paychan::channelBalance(*env.current(), bobChan) == XRP(10));

        // A channel that does not exist: a claim fails in the transactor, a
        // fund at the firewall.
        UInt256 const missing{1};
        env(paychan::claim(alice, missing, XRP(10)), Ter(tecNO_TARGET));
        env(paychan::fund(alice, missing, XRP(10)), Ter(tefFIREWALL_BLOCK));
        env.close();
    }

    void
    testMptPayment(FeatureBitset features)
    {
        testcase("MPT payment");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        MPTTester mpt(env, gw, {.holders = {alice, backup}});
        mpt.create({.ownerCount = 1, .flags = tfMPTCanTransfer});
        mpt.authorize({.account = alice});
        mpt.authorize({.account = backup});
        auto const token = mpt["MPT"];
        env(pay(gw, alice, token(500)));
        env.fund(XRP(10000), carol);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        env(pay(alice, backup, token(10)), Sendmax(token(10)));
        env(pay(alice, backup, token(10)), Sendmax(XRP(10)), Ter(tefFIREWALL_BLOCK));
        env.close();
        BEAST_EXPECT(mpt.getBalance(backup) == 10);
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = jtx::testableAmendments();
        testEnabled(all);
        testCreatePreflight(all);
        testCreatePreclaim(all);
        testUpdate(all);
        testBlockedAndAllowed(all);
        testPreauth(all);
        testMaxFee(all);
        testAccountDelete(all);
        testDelete(all);
        testDefaultAction();
        testActionTable();
        testNFTokenAcceptOffer(all);
        testConfidentialMirrorUpdate(all);
        testConfidentialHolderKeyUpdate(all);
        testTransactionProposalCancel(all);
        testUpdatePreflight(all);
        testUpdatePreclaim(all);
        testUpdateMaxFee(all);
        testMultiSignedCounterparty(all);
        testReserve(all);
        testWithdrawPreauthPreflight(all);
        testWithdrawPreauthPreclaim(all);
        testDeleteChecks(all);
        testCheckedTransactions(all);
        testAccountDeleteWithPreauths(all);
        testLedgerEntry(all);
        testBatchInnerBlocked(all);
        testMaxFeeOutOfRange(all);
        testBatchInnerManagement(all);
        testCrossCurrencyPayment(all);
        testRippling(all);
        testCreateMaxFeeZero(all);
        testBackupPreauthKept(all);
        testSigningAuthority(all);
        testTypesWithoutDestination(all);
        testChannelClaim(all);
        testConfidentialMergeInbox(all);
        testSigningAuthorityChecks(all);
        testPreauthSigners(all);
        testRipplingSides(all);
        testChannelChecks(all);
        testMptPayment(all);
    }
};

BEAST_DEFINE_TESTSUITE(Firewall, app, xrpl);

}  // namespace xrpl::test
