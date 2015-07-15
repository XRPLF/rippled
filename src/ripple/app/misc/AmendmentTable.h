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

#ifndef RIPPLE_APP_MISC_AMENDMENTTABLE_H_INCLUDED
#define RIPPLE_APP_MISC_AMENDMENTTABLE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/protocol/Protocol.h>

namespace ripple {

/** The status of all amendments requested in a given window. */
class AmendmentSet
{
public:
    std::uint32_t mCloseTime;
    int mTrustedValidations;                    // number of trusted validations
    hash_map<uint256, int> mVotes; // yes votes by amendment

    AmendmentSet (std::uint32_t ct) : mCloseTime (ct), mTrustedValidations (0)
    {
        ;
    }

    void addVoter ()
    {
        ++mTrustedValidations;
    }

    void addVote (uint256 const& amendment)
    {
        ++mVotes[amendment];
    }

    int count (uint256 const& amendment)
    {
        auto const& it = mVotes.find (amendment);
        return (it == mVotes.end()) ? 0 : it->second;
    }
};

/** 256-bit Id and human friendly name of an amendment.
*/
class AmendmentName final
{
private:
    uint256 mId;
    // Keep the hex string around for error reporting
    std::string mHexString;
    std::string mFriendlyName;
    bool mValid{false};

public:
    AmendmentName () = default;
    AmendmentName (AmendmentName const& rhs) = default;
    // AmendmentName (AmendmentName&& rhs) = default; // MSVS not supported
    AmendmentName (uint256 const& id, std::string friendlyName)
        : mId (id), mFriendlyName (std::move (friendlyName)), mValid (true)
    {
    }
    AmendmentName (std::string id, std::string friendlyName)
        : mHexString (std::move (id)), mFriendlyName (std::move (friendlyName))
    {
        mValid = mId.SetHex (mHexString);
    }
    bool valid () const
    {
        return mValid;
    }
    uint256 const& id () const
    {
        return mId;
    }
    std::string const& hexString () const
    {
        return mHexString;
    }
    std::string const& friendlyName () const
    {
        return mFriendlyName;
    }
};

/** Current state of an amendment.
    Tells if a amendment is supported, enabled or vetoed. A vetoed amendment
    means the node will never announce its support.
*/
class AmendmentState
{
public:
    bool mVetoed{false};  // We don't want this amendment enabled
    bool mEnabled{false};
    bool mSupported{false};
    bool mDefault{false};  // Include in genesis ledger

    std::string mFriendlyName;

    AmendmentState () = default;

    void setVeto ()
    {
        mVetoed = true;
    }
    void setDefault ()
    {
        mDefault = true;
    }
    bool isDefault ()
    {
        return mDefault;
    }
    bool isSupported ()
    {
        return mSupported;
    }
    bool isVetoed ()
    {
        return mVetoed;
    }
    bool isEnabled ()
    {
        return mEnabled;
    }
    std::string const& getFiendlyName ()
    {
        return mFriendlyName;
    }
    void setFriendlyName (std::string const& n)
    {
        mFriendlyName = n;
    }
};

class Section;


/** The amendment table stores the list of enabled and potential amendments.
    Individuals amendments are voted on by validators during the consensus
    process.
*/
class AmendmentTable
{
public:
    /** Create a new AmendmentTable.

        @param majorityTime the number of seconds an amendment must hold a majority
                            before we're willing to vote yes on it.
        @param majorityFraction ratio, out of 256, of servers that must say
                                they want an amendment before we consider it to
                                have a majority.
        @param journal
    */

    virtual ~AmendmentTable() { }

    /**
       @param section the config section of initial amendments
     */
    virtual void addInitial (Section const& section) = 0;

    /** Add an amendment to the AmendmentTable

        @throw will throw if the name parameter is not valid
    */
    virtual void addKnown (AmendmentName const& name) = 0;

    virtual uint256 get (std::string const& name) = 0;

    virtual bool veto (uint256 const& amendment) = 0;
    virtual bool unVeto (uint256 const& amendment) = 0;

    virtual bool enable (uint256 const& amendment) = 0;
    virtual bool disable (uint256 const& amendment) = 0;

    virtual bool isEnabled (uint256 const& amendment) = 0;
    virtual bool isSupported (uint256 const& amendment) = 0;

    /** Enable only the specified amendments.
        Other amendments in the table will be set to disabled.
    */
    virtual void setEnabled (const std::vector<uint256>& amendments) = 0;
    /** Support only the specified amendments.
        Other amendments in the table will be set to unsupported.
    */
    virtual void setSupported (const std::vector<uint256>& amendments) = 0;

    virtual Json::Value getJson (int) = 0;

    /** Returns a Json::objectValue. */
    virtual Json::Value getJson (uint256 const& ) = 0;

    /** Called when a new fully-validated ledger is accepted. */
    void doValidatedLedger (std::shared_ptr<ReadView const> const& lastValidatedLedger)
    {
        if (needValidatedLedger (lastValidatedLedger->seq ()))
            doValidatedLedger (lastValidatedLedger->seq (),
                getEnabledAmendments (*lastValidatedLedger));
    }

    /** Called to determine whether the amendment logic needs to process
        a new validated ledger. (If it could have changed things.)
    */
    virtual bool
    needValidatedLedger (LedgerIndex seq) = 0;

    virtual void
    doValidatedLedger (LedgerIndex ledgerSeq, enabledAmendments_t enabled) = 0;

    // Called by the consensus code when we need to
    // inject pseudo-transactions
    virtual std::map <uint256, std::uint32_t>
    doVoting (
        std::uint32_t closeTime,
        enabledAmendments_t const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        ValidationSet const& valSet) = 0;

    // Called by the consensus code when we need to
    // add feature entries to a validation
    virtual std::vector <uint256>
    doValidation (enabledAmendments_t const&) = 0;


    // The two function below adapt the API callers expect to the
    // internal amendment table API. This allows the amendment
    // table implementation to be independent of the ledger
    // implementation. These APIs will merge when the view code
    // supports a full ledger API

    void
    doValidation (std::shared_ptr <ReadView const> const& lastClosedLedger,
        STObject& baseValidation)
    {
        auto ourAmendments =
            doValidation (getEnabledAmendments(*lastClosedLedger));
        if (! ourAmendments.empty())
            baseValidation.setFieldV256 (sfAmendments,
               STVector256 (sfAmendments, ourAmendments));
    }


    void
    doVoting (
        std::shared_ptr <ReadView const> const& lastClosedLedger,
        ValidationSet const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition)
    {
        // Ask implementation what to do
        auto actions = doVoting (
            lastClosedLedger->parentCloseTime(),
            getEnabledAmendments(*lastClosedLedger),
            getMajorityAmendments(*lastClosedLedger),
            parentValidations);

        // Inject appropriate pseudo-transactions
        for (auto const& it : actions)
        {
            STTx trans (ttAMENDMENT);
            trans.setAccountID (sfAccount, AccountID());
            trans.setFieldH256 (sfAmendment, it.first);
            trans.setFieldU32 (sfLedgerSequence, lastClosedLedger->seq() + 1);

            if (it.second != 0)
                trans.setFieldU32 (sfFlags, it.second);

            Serializer s;
            trans.add (s);

        #if ! RIPPLE_PROPOSE_AMENDMENTS
            return;
        #endif

            uint256 txID = trans.getTransactionID();
            auto tItem = std::make_shared <SHAMapItem> (txID, s.peekData());
            initialPosition->addGiveItem (tItem, true, false);
        }
    }

};

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    beast::Journal journal);

}  // ripple

#endif
