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

#ifndef RIPPLE_AMENDMENT_TABLE_H
#define RIPPLE_AMENDMENT_TABLE_H

#include <ripple/app/book/Types.h>
#include <ripple/app/misc/Validations.h>

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

    core::Clock::time_point
        m_firstMajority{0};  // First time we saw a majority (close time)
    core::Clock::time_point
        m_lastMajority{0};  // Most recent time we saw a majority (close time)

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

    /** Update the walletDB with the majority times.
     */
    virtual void reportValidations (const AmendmentSet&) = 0;

    virtual Json::Value getJson (int) = 0;

    /** Returns a Json::objectValue. */
    virtual Json::Value getJson (uint256 const& ) = 0;

    virtual void
    doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) = 0;
    virtual void
    doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) = 0;
};

/**
   AmendmentTableInjections is used to insert moc objects into the amendment
   table class while unit testing.
 */
class AmendmentTableInjections
{
public:
    /** Get the first and last majority from the walletDB and update the
     * AmendmentState object.
     */
    virtual void setMajorityTimesFromDBToState (
        AmendmentState& toUpdate,
        uint256 const& amendmentHash) const = 0;
    /** For eash hash, get the first and last majority from the corresponding
     * AmendmentState object and update the walletDB.
     */
    virtual void setMajorityTimesFromStateToDB (
        std::vector<uint256> const& changedAmendments,
        hash_map<uint256, AmendmentState>& amendmentMap) const = 0;
    virtual ValidationSet getValidations (uint256 const& hash) const = 0;
};

// Use for regular system
std::unique_ptr<AmendmentTableInjections> make_AmendmentTableInjections ();
// Use for unit testing
std::unique_ptr<AmendmentTableInjections> make_MOCAmendmentTableInjections ();

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    beast::Journal journal,
    std::unique_ptr<AmendmentTableInjections> injections);

}  // ripple

#endif
