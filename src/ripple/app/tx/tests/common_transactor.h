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

#ifndef RIPPLE_APP_TRANSACTORS_TESTS_COMMON_TRANSACTOR_H_INCLUDED
#define RIPPLE_APP_TRANSACTORS_TESTS_COMMON_TRANSACTOR_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/paths/RippleState.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STAmount.h>
#include <beast/unit_test/suite.h>
#include <cstdint>

namespace ripple {
namespace test {

// Forward declare TestLedger so UserAccount can take it as a parameter.
class TestLedger;

// This class makes it easier to write unit tests that involve user accounts.
class UserAccount
{
private:
    KeyPair master_;
    AccountID acctID_;
    KeyPair regular_;
    bool useRegKey_ = false;
    std::uint32_t sequence_ = 0;

public:
    UserAccount () = delete;
    UserAccount (UserAccount const& rhs) = default;
    UserAccount& operator= (UserAccount const& rhs) = default;

    UserAccount (KeyType kType, std::string const& passphrase);

    AccountID const& getID () const
    {
        return acctID_;
    }

    // Sets the regular key on the account, but does not disable master.
    void setRegKey (
        TestLedger& ledger, KeyType kType, std::string const& passphrase);

    // Removes the regular key.
    void clrRegKey (TestLedger&  ledger);

    // Either disables or enables the master key.
    void disableMaster (TestLedger& ledger, bool doDisable);

    // Select to use either the regular (true) or master (false) key.
    void useRegKey (bool useReg)
    {
        useRegKey_ = useReg;
    }

    std::uint32_t consumeSeq ()
    {
        return ++sequence_;
    }

    // If a transaction fails we have to back up the sequence number, since.
    // the last sequence wasn't consumed.
    void decrSeq ()
    {
        --sequence_;
    }

    RippleAddress const& acctPublicKey () const
    {
        return master_.publicKey;
    }

    RippleAddress const& publicKey () const
    {
        return useRegKey_ ? regular_.publicKey : master_.publicKey;
    }

    RippleAddress const& secretKey () const
    {
        return useRegKey_ ? regular_.secretKey : master_.secretKey;
    }
};

// This class collects a bunch of the ledger shenanigans tests have to do.
class TestLedger
{
private:
    std::shared_ptr<Ledger const> lastClosedLedger_;
    Ledger::pointer openLedger_;
    beast::unit_test::suite& suite_;

public:
    TestLedger () = delete;
    TestLedger (const TestLedger& lhs) = delete;
    TestLedger (
        std::uint64_t startAmountDrops,
        UserAccount const& master,
        beast::unit_test::suite& suite);

    // Return.first  : transaction's TEC
    // Return.second : transaction successfully applied and in ledger.
    std::pair<TER, bool> applyTransaction (STTx const& tx, bool check = true);

    // Apply a transaction that we expect to succeed.
    void applyGoodTransaction (STTx const& tx, bool check = true);

    // Apply a transaction that we expect to fail.  Pass the expected error code.
    void applyBadTransaction (STTx const& tx, TER err, bool check = true);

    // apply a transaction that we expect to fail but charges a fee.  Pass the
    // expected error code.
    void applyTecTransaction (STTx const& tx, TER err, bool check = true);

    // Return the AccountState for a UserAccount
    // VFALCO This function is not necessary, just expose
    //        the ledger data member and use the free function.
    AccountState::pointer getAccountState (UserAccount const& acct) const;

    // Return the current open ledger.
    Ledger::pointer openLedger ()
    {
        return openLedger_;
    }
};

// This class makes it easier to construct unit tests with SignerLists.
//
// Typically construct this class with an initializer list.  Like this:
//
// UserAccount alice;
// UserAccount becky;
// SignerList s {{alice, 5}, {becky, 2}};
//
// where '5' and '2' are the weights of the accounts in the SignerList.
class SignerList
{
private:
    // Our initializer list takes references that we turn into pointers.
    struct InitListEntry
    {
        UserAccount& acct;
        std::uint16_t weight;
    };

    struct SignerAndWeight
    {
        UserAccount* acct;
        std::uint16_t weight;
        SignerAndWeight(InitListEntry& entry)
        : acct (&entry.acct)
        , weight (entry.weight) { }
    };
    std::vector<SignerAndWeight> list_;

public:
    SignerList() = default;
    SignerList(SignerList const& rhs) = default;
    SignerList(SignerList&& rhs) // Visual Studio 2013 won't default move ctor
    : list_(std::move(rhs.list_))
    { }
    SignerList(std::initializer_list<InitListEntry> args)
    {
        list_.reserve(args.size());
        for (auto arg : args)
        {
            list_.emplace_back(arg);
        }
    }

    SignerList& operator=(SignerList const& rhs) = default;
    SignerList& operator=(SignerList&& rhs) // VS 2013 won't default move assign
    {
        list_ = std::move(rhs.list_);
        return *this;
    }

    bool empty() const
    {
        return list_.empty ();
    }

    // Inject this SignerList into the passed transaction.
    void injectInto (STTx& tx) const;
};

// This type makes it easier to write unit tests that use multi-signatures.
//
// The MultiSig type is intended to be short-lived (any user account it
// references should out-live it).  It is also intended to be easily
// moved in a std::vector, so it uses pointers not references.
class MultiSig
{
    UserAccount const* signingFor_;
    UserAccount const* signer_;
    Blob multiSig_;

public:
    MultiSig() = delete;
    MultiSig(MultiSig const& rhs) = default;
    MultiSig(MultiSig&& rhs) // Visual Studio 2013 won't default move ctor
    : signingFor_ (std::move(rhs.signingFor_))
    , signer_ (std::move(rhs.signer_))
    , multiSig_ (std::move(rhs.multiSig_))
    { }

    MultiSig& operator=(MultiSig const& rhs) = delete;
    MultiSig& operator=(MultiSig&& rhs) // VS 2013 won't default move assign
    {
        signingFor_ = std::move(rhs.signingFor_);
        signer_ = std::move(rhs.signer_);
        multiSig_ = std::move(rhs.multiSig_);
        return *this;
    }

    MultiSig(UserAccount const& signingFor,
        UserAccount const& signer, STTx const& tx);

    friend bool operator==(MultiSig const& lhs, MultiSig const& rhs)
    {
        return ((lhs.signingFor_->getID() == rhs.signingFor_->getID()) &&
            (lhs.signer_->getID() == rhs.signer_->getID()));
    }

    friend bool operator<(MultiSig const& lhs, MultiSig const& rhs)
    {
        AccountID const& lhsSigingFor = lhs.signingFor_->getID();
        AccountID const& rhsSigningFor = rhs.signingFor_->getID();
        if (lhsSigingFor < rhsSigningFor)
            return true;

        if (lhsSigingFor > rhsSigningFor)
            return false;

        if (lhs.signer_->getID() < rhs.signer_->getID())
            return true;

        return false;
    }

    AccountID const& signingForAccount() const
    {
        return signingFor_->getID();
    }

    AccountID const& signingAccount() const
    {
        return signer_->getID();
    }

    Blob const& multiSignature() const
    {
        return multiSig_;
    }

    Blob const& signingPubKey() const
    {
        return signer_->publicKey().getAccountPublic();
    }
};

//------------------------------------------------------------------------------

// Single-sign the passed transaction using acct.
void singleSign (STTx& tx, UserAccount& acct);

// Multi-sign the passed transaction using multiSigs.
void multiSign (STTx& tx, std::vector<MultiSig>& multiSigs);

// Insert the multiSigs into tx without sorting.  Allows testing error cases.
void insertMultiSigs (STTx& tx, std::vector<MultiSig> const& multiSigs);

//------------------------------------------------------------------------------

// Return a transaction with an SOTemplate, sfTransactionType, sfAccount,
// sfFee, sfFlags, and sfSequence.
STTx getSeqTx (UserAccount& acct, TxType type);

// Get an AccountSet transaction.
STTx getAccountSetTx (UserAccount& acct);

// Return an unsigned OfferCreate transaction.
STTx getOfferCreateTx (UserAccount& acct,
    STAmount const& takerGets, STAmount const& takerPays);

// Return an unsigned OfferCancel transaction.
STTx getOfferCancelTx (UserAccount& acct, std::uint32_t offerSeq);

// Return an unsigned transaction good for making a payment in XRP.
STTx getPaymentTx (
    UserAccount& from, UserAccount const& to, std::uint64_t amountDrops);

// Return an unsigned transaction good for making a payment.
STTx getPaymentTx (
    UserAccount& from, UserAccount const& to, STAmount const& amount);

// Return a transaction that sets a regular key.
STTx getSetRegularKeyTx (UserAccount& acct, AccountID const& regKey);

// Return a transaction that clears a regular key.
STTx getClearRegularKeyTx (UserAccount& acct);

// Return a SignerListSet transaction.  If the quorum is zero and signers
// is empty, then any signer list is removed from the account.
STTx getSignerListSetTx (
    UserAccount& acct, SignerList const& signers, std::uint32_t quorum);

// Return a transaction that creates an  un-targeted ticket.
STTx getCreateTicketTx (UserAccount& acct);

// Return a transaction that creates an  targeted ticket.
STTx getCreateTicketTx (UserAccount& acct, UserAccount const& target);

// Return a transaction that cancels a ticket.
STTx getCancelTicketTx (UserAccount& acct, uint256 const& ticketID);

// Return an unsigned trust set transaction.
STTx getTrustSetTx (UserAccount& from, Issue const& issuer, int limit);

//------------------------------------------------------------------------------

// Complete the a simple Payment transaction in drops.  Expected to succeed.
void payInDrops (TestLedger& ledger,
    UserAccount& from, UserAccount const& to, std::uint64_t amountDrops);

// Return the native balance on an account.
std::uint64_t getNativeBalance(TestLedger& ledger, UserAccount& acct);

// Return the owner count of an account.
std::uint32_t getOwnerCount(TestLedger& ledger, UserAccount& acct);

// Get all RippleStates between two accounts.
std::vector<RippleState::pointer> getRippleStates (
    TestLedger& ledger, UserAccount const& from, UserAccount const& peer);

// Get all Offers on an account.
std::vector <std::shared_ptr<SLE const>>
getOffersOnAccount (TestLedger& ledger, UserAccount const& acct);

// Get all Tickets on an account.
std::vector <std::shared_ptr<SLE const>>
getTicketsOnAccount (TestLedger& ledger, UserAccount const& acct);

} // test
} // ripple

#endif // RIPPLE_APP_TRANSACTORS_TESTS_COMMON_TRANSACTOR_H_INCLUDED
