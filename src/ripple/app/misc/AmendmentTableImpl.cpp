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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/basics/contract.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/TxFlags.h>
#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <algorithm>
#include <mutex>

namespace ripple {
/** Track the list of "amendments"

   An "amendment" is an option that can affect transaction processing rules.
   Amendments are proposed and then adopted or rejected by the network. An
   Amendment is uniquely identified by its AmendmentID, a 256-bit key.
*/

class AmendmentTableImpl final : public AmendmentTable
{
protected:
    using amendmentMap_t = hash_map<uint256, AmendmentState>;
    using amendmentList_t = hash_set<uint256>;

    std::mutex mLock;

    amendmentMap_t m_amendmentMap;
    std::uint32_t m_lastUpdateSeq;

    std::chrono::seconds m_majorityTime; // Seconds an amendment must hold a majority
    int mMajorityFraction;  // 256 = 100%
    beast::Journal m_journal;

    AmendmentState& getCreate (uint256 const& amendment);
    AmendmentState* getExisting (uint256 const& amendment);
    void setJson (Json::Value& v, const AmendmentState&);

public:
    AmendmentTableImpl (
        std::chrono::seconds majorityTime,
        int majorityFraction,
        beast::Journal journal)
        : m_lastUpdateSeq (0)
        , m_majorityTime (majorityTime)
        , mMajorityFraction (majorityFraction)
        , m_journal (journal)
    {
    }

    void addInitial (Section const& section) override;

    void addKnown (AmendmentName const& name) override;

    uint256 get (std::string const& name) override;

    bool veto (uint256 const& amendment) override;
    bool unVeto (uint256 const& amendment) override;

    bool enable (uint256 const& amendment) override;
    bool disable (uint256 const& amendment) override;

    bool isEnabled (uint256 const& amendment) override;
    bool isSupported (uint256 const& amendment) override;

    void setEnabled (const std::vector<uint256>& amendments) override;
    void setSupported (const std::vector<uint256>& amendments) override;

    Json::Value getJson (int) override;
    Json::Value getJson (uint256 const&) override;

    bool needValidatedLedger (LedgerIndex seq) override;

    void doValidatedLedger (LedgerIndex seq,
        enabledAmendments_t enabled) override;

    std::vector <uint256>
        doValidation (enabledAmendments_t const& enabledAmendments)
            override;

    std::map <uint256, std::uint32_t> doVoting (std::uint32_t closeTime,
        enabledAmendments_t const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        ValidationSet const& validations) override;

    amendmentList_t getVetoed();
    amendmentList_t getEnabled();

    // Amendments we support, do not veto, and are not enabled
    amendmentList_t getDesired (enabledAmendments_t const&);
};

namespace detail
{
/** preEnabledAmendments is a static collection of amendments that are are
   enabled at build time.

   Add amendments to this collection at build time to enable them on this
   server.
*/

std::vector<AmendmentName> const preEnabledAmendments;
}

void
AmendmentTableImpl::addInitial (Section const& section)
{
    for (auto const& a : detail::preEnabledAmendments)
    {
        if (!a.valid ())
        {
            std::string const errorMsg =
                (boost::format (
                     "preEnabledAmendments contains an invalid hash (expected "
                     "a hex number). Value was: %1%") %
                 a.hexString ()).str ();
            Throw<std::runtime_error> (errorMsg);
        }
    }

    std::vector<AmendmentName> toAdd (detail::preEnabledAmendments);

    {
        // add the amendments from the config file
        int const numExpectedToks = 2;
        for (auto const& line : section.lines ())
        {
            boost::tokenizer<> tokenizer (line);
            std::vector<std::string> tokens (tokenizer.begin (),
                                             tokenizer.end ());
            if (tokens.size () != numExpectedToks)
            {
                std::string const errorMsg =
                    (boost::format (
                         "The %1% section in the config file expects %2% "
                         "items. Found %3%. Line was: %4%") %
                     SECTION_AMENDMENTS % numExpectedToks % tokens.size () %
                     line).str ();
                Throw<std::runtime_error> (errorMsg);
            }

            toAdd.emplace_back (std::move (tokens[0]), std::move (tokens[1]));
            if (!toAdd.back ().valid ())
            {
                std::string const errorMsg =
                    (boost::format (
                         "%1% is not a valid hash. Expected a hex "
                         "number. In config setcion: %2%. Line was: "
                         "%3%") %
                     toAdd.back ().hexString () % SECTION_AMENDMENTS %
                     line).str ();
                Throw<std::runtime_error> (errorMsg);
            }
        }
    }

    for (auto const& a : toAdd)
    {
        addKnown (a);
        enable (a.id ());
    }
}

AmendmentState&
AmendmentTableImpl::getCreate (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto iter (m_amendmentMap.find (amendmentHash));

    if (iter == m_amendmentMap.end())
    {
        AmendmentState& amendment = m_amendmentMap[amendmentHash];
        return amendment;
    }

    return iter->second;
}

AmendmentState*
AmendmentTableImpl::getExisting (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto iter (m_amendmentMap.find (amendmentHash));

    if (iter == m_amendmentMap.end())
        return nullptr;

    return & (iter->second);
}

uint256
AmendmentTableImpl::get (std::string const& name)
{
    std::lock_guard <std::mutex> sl (mLock);

    for (auto const& e : m_amendmentMap)
    {
        if (name == e.second.mFriendlyName)
            return e.first;
    }

    return uint256 ();
}

void
AmendmentTableImpl::addKnown (AmendmentName const& name)
{
    if (!name.valid ())
    {
        std::string const errorMsg =
            (boost::format (
                 "addKnown was given an invalid hash (expected a hex number). "
                 "Value was: %1%") %
             name.hexString ()).str ();
        Throw<std::runtime_error> (errorMsg);
    }

    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState& amendment = getCreate (name.id ());

    if (!name.friendlyName ().empty ())
        amendment.setFriendlyName (name.friendlyName ());

    amendment.mVetoed = false;
    amendment.mSupported = true;
}

bool
AmendmentTableImpl::veto (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState& s = getCreate (amendment);

    if (s.mVetoed)
        return false;

    s.mVetoed = true;
    return true;
}

bool
AmendmentTableImpl::unVeto (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);

    if (!s || !s->mVetoed)
        return false;

    s->mVetoed = false;
    return true;
}

bool
AmendmentTableImpl::enable (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState& s = getCreate (amendment);

    if (s.mEnabled)
        return false;

    s.mEnabled = true;
    return true;
}

bool
AmendmentTableImpl::disable (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);

    if (!s || !s->mEnabled)
        return false;

    s->mEnabled = false;
    return true;
}

bool
AmendmentTableImpl::isEnabled (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);
    return s && s->mEnabled;
}

bool
AmendmentTableImpl::isSupported (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);
    return s && s->mSupported;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getVetoed ()
{
    amendmentList_t ret;
    std::lock_guard <std::mutex> sl (mLock);
    for (auto const& e : m_amendmentMap)
    {
        if (e.second.mVetoed)
            ret.insert (e.first);
    }
    return ret;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getEnabled ()
{
    amendmentList_t ret;
    std::lock_guard <std::mutex> sl (mLock);
    for (auto const& e : m_amendmentMap)
    {
        if (e.second.mEnabled)
            ret.insert (e.first);
    }
    return ret;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getDesired (enabledAmendments_t const& enabled)
{
    amendmentList_t ret;
    std::lock_guard <std::mutex> sl (mLock);

    for (auto const& e : m_amendmentMap)
    {
        if (e.second.mSupported && ! e.second.mVetoed &&
            (enabled.count (e.first) == 0))
            ret.insert (e.first);
    }

    return ret;
}

void
AmendmentTableImpl::setEnabled (const std::vector<uint256>& amendments)
{
    std::lock_guard <std::mutex> sl (mLock);
    for (auto& e : m_amendmentMap)
    {
        e.second.mEnabled = false;
    }
    for (auto const& e : amendments)
    {
        m_amendmentMap[e].mEnabled = true;
    }
}

void
AmendmentTableImpl::setSupported (const std::vector<uint256>& amendments)
{
    std::lock_guard <std::mutex> sl (mLock);
    for (auto &e : m_amendmentMap)
    {
        e.second.mSupported = false;
    }
    for (auto const& e : amendments)
    {
        m_amendmentMap[e].mSupported = true;
    }
}

std::vector <uint256>
AmendmentTableImpl::doValidation (
    enabledAmendments_t const& enabledAmendments)
{
    auto lAmendments = getDesired (enabledAmendments);

    if (lAmendments.empty())
        return {};

    std::vector <uint256> amendments (lAmendments.begin(), lAmendments.end());
    std::sort (amendments.begin (), amendments.end ());

    return amendments;
}

std::map <uint256, std::uint32_t>
AmendmentTableImpl::doVoting (
    std::uint32_t closeTime,
    enabledAmendments_t const& enabledAmendments,
    majorityAmendments_t const& majorityAmendments,
    ValidationSet const& valSet)
{
    // LCL must be flag ledger
    //assert((lastClosedLedger->info().seq % 256) == 0);

    AmendmentSet amendmentSet (closeTime);

    // process validations for ledger before flag ledger
    for (auto const& entry : valSet)
    {
        auto const& val = *entry.second;

        if (val.isTrusted ())
        {
            amendmentSet.addVoter ();
            if (val.isFieldPresent (sfAmendments))
            {
                for (auto const& amendment : val.getFieldV256 (sfAmendments))
                {
                    amendmentSet.addVote (amendment);
                }
            }
        }
    }
    int threshold =
        (amendmentSet.mTrustedValidations * mMajorityFraction + 255) / 256;

    if (m_journal.trace)
        m_journal.trace <<
            amendmentSet.mTrustedValidations << " trusted validations, threshold is "
            << threshold;

    // Map of amendments to the action to be taken
    // for each one. The action is the value of the
    // flags in the pseudo-transaction
    std::map <uint256, std::uint32_t> actions;

    {
        std::lock_guard <std::mutex> sl (mLock);

        // process all amendments we know of
        for (auto const& entry : m_amendmentMap)
        {
            bool const hasValMajority = amendmentSet.count (entry.first) >= threshold;

            std::uint32_t majorityTime = 0;
            auto const it = majorityAmendments.find (entry.first);
            if (it != majorityAmendments.end ())
                majorityTime = it->second;

            // FIXME: Add logging here
            if (enabledAmendments.count (entry.first) != 0)
            {
                // Already enabled, nothing to do
            }
            else  if (hasValMajority && (majorityTime == 0) && (! entry.second.mVetoed))
            {
                // Ledger says no majority, validators say yes
                actions[entry.first] = tfGotMajority;
            }
            else if (! hasValMajority && (majorityTime != 0))
            {
                // Ledger says majority, validators say no
                actions[entry.first] = tfLostMajority;
            }
            else if ((majorityTime != 0) &&
                ((majorityTime + m_majorityTime.count()) <= closeTime) &&
                ! entry.second.mVetoed)
            {
                // Ledger says majority held
                actions[entry.first] = 0;
            }
        }
    }

    return actions;
}

bool
AmendmentTableImpl::needValidatedLedger (LedgerIndex ledgerSeq)
{
    std::lock_guard <std::mutex> sl (mLock);

    // Is there a ledger in which an amendment could have been enabled
    // between these two ledger sequences?

    return ((ledgerSeq - 1) / 256) != ((m_lastUpdateSeq - 1) / 256);
}

void
AmendmentTableImpl::doValidatedLedger (LedgerIndex ledgerSeq,
    enabledAmendments_t enabled)
{
    std::lock_guard <std::mutex> sl (mLock);

    for (auto& e : m_amendmentMap)
        e.second.mEnabled = (enabled.count (e.first) != 0);
}

Json::Value
AmendmentTableImpl::getJson (int)
{
    Json::Value ret(Json::objectValue);
    {
        std::lock_guard <std::mutex> sl(mLock);
        for (auto const& e : m_amendmentMap)
        {
            setJson (ret[to_string (e.first)] = Json::objectValue, e.second);
        }
    }
    return ret;
}

void
AmendmentTableImpl::setJson (Json::Value& v, const AmendmentState& fs)
{
    if (!fs.mFriendlyName.empty())
        v[jss::name] = fs.mFriendlyName;

    v[jss::supported] = fs.mSupported;
    v[jss::vetoed] = fs.mVetoed;
    v[jss::enabled] = fs.mEnabled;
}

Json::Value
AmendmentTableImpl::getJson (uint256 const& amendmentID)
{
    Json::Value ret = Json::objectValue;
    Json::Value& jAmendment = (ret[to_string (amendmentID)] = Json::objectValue);

    {
        std::lock_guard <std::mutex> sl(mLock);

        AmendmentState& amendmentState = getCreate (amendmentID);
        setJson (jAmendment, amendmentState);
    }

    return ret;
}

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    beast::Journal journal)
{
    return std::make_unique<AmendmentTableImpl>(
        majorityTime, majorityFraction, journal);
}

}  // ripple
