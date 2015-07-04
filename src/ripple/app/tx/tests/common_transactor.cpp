//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
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

#include <ripple/app/tx/tests/common_transactor.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/LedgerConsensus.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/ledger/tests/common_ledger.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/chrono.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/TxFlags.h>

#if 0

namespace ripple {
namespace test {

UserAccount::UserAccount (KeyType kType, std::string const& passphrase)
{
    RippleAddress const seed = RippleAddress::createSeedGeneric (passphrase);
    master_ = generateKeysFromSeed (kType, seed);
    acctID_ = calcAccountID(master_.publicKey);
}

void UserAccount::setRegKey (
    TestLedger& ledger, KeyType kType, std::string const& passphrase)
{
    // Get information for the new regular key.
    RippleAddress const seed = RippleAddress::createSeedGeneric(passphrase);
    KeyPair regular = generateKeysFromSeed (kType, seed);

    // Tell the ledger what we're up to.
    STTx tx = getSetRegularKeyTx (*this,
        calcAccountID(regular.publicKey));
    singleSign (tx, *this);
    ledger.applyGoodTransaction (tx);

    // Remember what changed.
    regular_ = regular;
}

void UserAccount::clrRegKey (TestLedger& ledger)
{
    // Tell the ledger what we're up to.
    STTx tx = getClearRegularKeyTx (*this);
    singleSign (tx, *this);
    ledger.applyGoodTransaction (tx);

    // Remember what changed.
    RippleAddress temp;
    regular_.secretKey = temp;
    regular_.publicKey = temp;
}

void UserAccount::disableMaster (TestLedger& ledger, bool doDisable)
{
    STTx tx = getAccountSetTx (*this);
    SField const& field = doDisable ? sfSetFlag : sfClearFlag;
    tx.setFieldU32 (field, asfDisableMaster);
    singleSign (tx, *this);
    ledger.applyGoodTransaction (tx);
}

//------------------------------------------------------------------------------

TestLedger::TestLedger (
        std::uint64_t startAmountDrops,
        UserAccount const& master,
        beast::unit_test::suite& suite)
: lastClosedLedger()
, openLedger_()
, suite_(suite)
{
    // To leverage createGenesisLedger from the Ledger tests, we must match
    // its interface.
    TestAccount const masterAcct {master.publicKey(), master.secretKey(), 0};
    std::tie (lastClosedLedger, openLedger_) =
        createGenesisLedger(startAmountDrops, masterAcct);
}

std::pair<TER, bool>
TestLedger::applyTransaction (STTx const& tx, bool check)
{
    // Apply the transaction to the open ledger.
    auto const r = apply(
        *openLedger_, tx, check ? tapNONE : tapNO_CHECK_SIGN,
            getConfig(), beast::Journal{});

    // Close the open ledger to see if the transaction was real committed.
    //
    // In part we close the open ledger so we don't have to think about the
    // time sequencing of transactions.  Every transaction applied by a
    // call to this method gets applied individually.  So this transaction
    // is guaranteed to be applied before the next one.
    close_and_advance(openLedger_, lastClosedLedger);

    // Check for the transaction in the closed ledger.
    bool const foundTx =
        lastClosedLedger->txExists(tx.getTransactionID());
    suite_.expect (r.second == foundTx);

    return {r.first, r.second && foundTx};
}

void TestLedger::applyGoodTransaction (STTx const& tx, bool check)
{
    auto ret = applyTransaction (tx, check);
    suite_.expect (ret.first == tesSUCCESS);
    suite_.expect (ret.second == true);
}

void TestLedger::applyBadTransaction (STTx const& tx, TER err, bool check)
{
    auto ret = applyTransaction (tx, check);
    suite_.expect (ret.first == err);
    suite_.expect (ret.second == false);
}

void TestLedger::applyTecTransaction (STTx const& tx, TER err, bool check)
{
    auto ret = applyTransaction (tx, check);
    suite_.expect (ret.first == err);
    suite_.expect (ret.second == true);
}

//------------------------------------------------------------------------------

MultiSig::MultiSig(UserAccount const& signingFor,
        UserAccount const& signer, STTx const& tx)
: signingFor_(&signingFor)
, signer_(&signer)
, multiSig_()
{
    Serializer s = tx.getMultiSigningData (
        calcAccountID(signingFor.acctPublicKey()),
            calcAccountID(signer.acctPublicKey()));
    multiSig_ = signer.secretKey().accountPrivateSign (s.getData());
}

//------------------------------------------------------------------------------

void SignerList::injectInto (STTx& tx) const
{
    // Create the SignerListArray one STObject at a time.
    STArray list (list_.size ());
    for (auto const& entry : list_)
    {
        list.emplace_back(sfSignerEntry);
        STObject& obj = list.back();
        obj.reserve (2);
        obj.setAccountID (sfAccount, entry.acct->getID ());
        obj.setFieldU16 (sfSignerWeight, entry.weight);
        obj.setTypeFromSField (sfSignerEntry);
    }
    // Insert the SignerEntries.
    tx.setFieldArray (sfSignerEntries, list);
}

//------------------------------------------------------------------------------

// Single-sign the passed transaction using acct.
void singleSign (STTx& tx, UserAccount& acct)
{
    tx.setFieldVL (sfSigningPubKey, acct.publicKey().getAccountPublic ());
    tx.sign (acct.secretKey ());
}

// Multi-sign the passed transaction using multiSigs.
void multiSign (STTx& tx, std::vector<MultiSig>& multiSigs)
{
    // multiSigs must be sorted or the signature will fail.
    std::sort(multiSigs.begin(), multiSigs.end());
    insertMultiSigs(tx, multiSigs);
}

// Insert the multiSigs into tx without sorting.  Allows testing error cases.
void insertMultiSigs (STTx& tx, std::vector<MultiSig> const& multiSigs)
{
    // Know when to change out SigningFor containers.
    AccountID prevSigningForID;

    // Create the MultiSigners array one STObject at a time.
    STArray multiSigners;
    boost::optional <STObject> signingFor;
    for (auto const& entry : multiSigs)
    {
        if (entry.signingForAccount() != prevSigningForID)
        {
            if (signingFor != boost::none)
                multiSigners.push_back (std::move(signingFor.get()));

            // Construct the next SigningFor object and fill it in.
            prevSigningForID = entry.signingForAccount();
            signingFor.emplace (sfSigningFor);
            signingFor->reserve (2);
            signingFor->setAccountID (sfAccount, entry.signingForAccount());
            signingFor->setFieldArray (sfSigningAccounts, STArray());
        }
        assert(signingFor);

        // Construct this SigningAccount object and fill it in.
        STArray& signingAccounts = signingFor->peekFieldArray(
            sfSigningAccounts);
        signingAccounts.emplace_back (sfSigningAccount);

        STObject& signingAccount (signingAccounts.back());
        signingAccount.reserve (3);
        signingAccount.setAccountID (sfAccount, entry.signingAccount());
        signingAccount.setFieldVL (sfMultiSignature, entry.multiSignature());
        signingAccount.setFieldVL (sfSigningPubKey, entry.signingPubKey());
    }
    // Remember to put in the final SigningFor object.
    if (signingFor)
        multiSigners.push_back (std::move(signingFor.get()));

    // Inject the multiSigners into tx.
    tx.setFieldArray (sfMultiSigners, multiSigners);
}

//------------------------------------------------------------------------------

// Return a transaction with an SOTemplate, sfTransactionType, sfAccount,
// sfFee, sfFlags, and sfSequence.
STTx getSeqTx (UserAccount& acct, TxType type)
{
    STTx tx (type); // Sets SOTemplate and sfTransactionType.
    tx.setAccountID (sfAccount, acct.getID());
    tx.setFieldAmount (sfFee, STAmount (10));
    tx.setFieldU32 (sfFlags, tfUniversal);
    tx.setFieldU32 (sfSequence, acct.consumeSeq());
    return tx;
}

// Return an unsigned AccountSet transaction.
STTx getAccountSetTx (UserAccount& acct)
{
    STTx tx = getSeqTx (acct, ttACCOUNT_SET);
    return tx;
}

// Return an unsigned OfferCreate transaction.
STTx getOfferCreateTx (UserAccount& acct,
    STAmount const& takerGets, STAmount const& takerPays)
{
    STTx tx = getSeqTx (acct, ttOFFER_CREATE);
    tx.setFieldAmount (sfTakerGets, takerGets);
    tx.setFieldAmount (sfTakerPays, takerPays);
    return tx;
}

// Return an unsigned OfferCancel transaction.
STTx getOfferCancelTx (UserAccount& acct, std::uint32_t offerSeq)
{
    STTx tx = getSeqTx (acct, ttOFFER_CANCEL);
    tx.setFieldU32 (sfOfferSequence, offerSeq);
    return tx;
}

// Return an unsigned transaction good for making a payment.
STTx getPaymentTx (
    UserAccount& from, UserAccount const& to, std::uint64_t amountDrops)
{
    STTx tx = getSeqTx (from, ttPAYMENT);
    tx.setAccountID (sfDestination, to.getID());
    tx.setFieldAmount (sfAmount, amountDrops);
    return tx;
}

STTx getPaymentTx (
    UserAccount& from, UserAccount const& to, STAmount const& amount)
{
    STTx tx = getSeqTx (from, ttPAYMENT);
    tx.setAccountID (sfDestination, to.getID());
    tx.setFieldAmount (sfAmount, amount);
    return tx;
}

// Return a transaction that sets a regular key
STTx getSetRegularKeyTx (UserAccount& acct, AccountID const& regKey)
{
    STTx tx = getSeqTx (acct, ttREGULAR_KEY_SET);
    tx.setAccountID (sfRegularKey,  regKey);
    return tx;
}

// Return a transaction that clears a regular key
STTx getClearRegularKeyTx (UserAccount& acct)
{
    STTx tx = getSeqTx (acct, ttREGULAR_KEY_SET);
    return tx;
}

// Return a SignerListSet transaction.
STTx getSignerListSetTx (
    UserAccount& acct, SignerList const& signers, std::uint32_t quorum)
{
    STTx tx = getSeqTx (acct, ttSIGNER_LIST_SET);
    tx.setFieldU32 (sfSignerQuorum, quorum);
    if (! signers.empty ())
    {
        signers.injectInto (tx);
    }
    return tx;
}

// Return a transaction that creates an un-targeted ticket.
STTx getCreateTicketTx (UserAccount& acct)
{
    STTx tx = getSeqTx (acct, ttTICKET_CREATE);
    return tx;
}

// Return a transaction that creates a targeted ticket.
STTx getCreateTicketTx (UserAccount& acct, UserAccount const& target)
{
    STTx tx = getSeqTx (acct, ttTICKET_CREATE);
    tx.setAccountID (sfTarget, target.getID());
    return tx;
}

// Return a transaction that cancels a ticket.
STTx getCancelTicketTx (UserAccount& acct, uint256 const& ticketID)
{
    STTx tx = getSeqTx (acct, ttTICKET_CANCEL);
    tx.setFieldH256 (sfTicketID, ticketID);
    return tx;
}

// Return a trust set transaction.
STTx getTrustSetTx (UserAccount& from, Issue const& issuer, int limit)
{
    STTx tx = getSeqTx (from, ttTRUST_SET);
    STAmount const stLimit (issuer, limit);
    tx.setFieldAmount (sfLimitAmount, stLimit);
    return tx;
}

//------------------------------------------------------------------------------

void payInDrops (TestLedger& ledger,
    UserAccount& from, UserAccount const& to, std::uint64_t amountDrops)
{
    STTx tx = getPaymentTx (from, to, amountDrops);
    singleSign (tx, from);
    ledger.applyGoodTransaction (tx);
}

std::uint64_t getNativeBalance(TestLedger& ledger, UserAccount& acct)
{
    return ledger.lastClosedLedger->read(
        keylet::account(acct.getID()))->getFieldAmount(
            sfBalance).mantissa();
}

std::uint32_t getOwnerCount(TestLedger& ledger, UserAccount& acct)
{
    return ledger.lastClosedLedger->read(
        keylet::account(acct.getID()))->getFieldU32(sfOwnerCount);
}

std::vector<RippleState::pointer> getRippleStates (
    TestLedger& ledger, UserAccount const& acct, UserAccount const& peer)
{
    std::vector <RippleState::pointer> states;

    forEachItem(*ledger.openLedger(), acct.getID(),
        [&states, &acct, &peer](
            std::shared_ptr<SLE const> const& sleCur)
        {
            // See whether this SLE is a lt_RIPPLE_STATE
            if (!sleCur || sleCur->getType () != ltRIPPLE_STATE)
                return;

            // It's a lt_RIPPLE_STATE.  See if it's one we want to return.
            RippleState::pointer const state (
                RippleState::makeItem (acct.getID(), sleCur));

            if ((state) && (state->getAccountIDPeer() == peer.getID()))
                states.emplace_back (std::move (state));
        });

    return states;
}

std::vector <std::shared_ptr<SLE const>>
getOffersOnAccount (TestLedger& ledger, UserAccount const& acct)
{
    std::vector <std::shared_ptr<SLE const>> offers;

    forEachItem(*ledger.openLedger(), acct.getID(),
        [&offers, &acct](
            std::shared_ptr<SLE const> const& sleCur)
        {
            // If sleCur is an ltOFFER save it.
            if (sleCur && sleCur->getType () == ltOFFER)
                offers.emplace_back (sleCur);
        });
    return offers;
}

std::vector <std::shared_ptr<SLE const>>
getTicketsOnAccount (TestLedger& ledger, UserAccount const& acct)
{
    std::vector <std::shared_ptr<SLE const>> offers;

    forEachItem(*ledger.openLedger(), acct.getID(),
        [&offers, &acct](
            std::shared_ptr<SLE const> const& sleCur)
        {
            // If sleCur is an ltTICKET save it.
            if (sleCur && sleCur->getType () == ltTICKET)
                offers.emplace_back (sleCur);
        });
    return offers;
}

} // test
} // ripple

#endif
