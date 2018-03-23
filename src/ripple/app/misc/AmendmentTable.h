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
#include <ripple/protocol/STValidation.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/Protocol.h>

namespace ripple {

/** The amendment table stores the list of enabled and potential amendments.
    Individuals amendments are voted on by validators during the consensus
    process.
*/
class AmendmentTable
{
public:
    virtual ~AmendmentTable() = default;

    virtual uint256 find (std::string const& name) = 0;

    virtual bool veto (uint256 const& amendment) = 0;
    virtual bool unVeto (uint256 const& amendment) = 0;

    virtual bool enable (uint256 const& amendment) = 0;
    virtual bool disable (uint256 const& amendment) = 0;

    virtual bool isEnabled (uint256 const& amendment) = 0;
    virtual bool isSupported (uint256 const& amendment) = 0;

    /**
     * @brief returns true if one or more amendments on the network
     * have been enabled that this server does not support
     *
     * @return true if an unsupported feature is enabled on the network
     */
    virtual bool hasUnsupportedEnabled () = 0;

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
    doValidatedLedger (
        LedgerIndex ledgerSeq,
        std::set <uint256> const& enabled) = 0;

    // Called by the consensus code when we need to
    // inject pseudo-transactions
    virtual std::map <uint256, std::uint32_t>
    doVoting (
        NetClock::time_point closeTime,
        std::set <uint256> const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        std::vector<STValidation::pointer> const& valSet) = 0;

    // Called by the consensus code when we need to
    // add feature entries to a validation
    virtual std::vector <uint256>
    doValidation (std::set <uint256> const& enabled) = 0;

    // The set of amendments to enable in the genesis ledger
    // This will return all known, non-vetoed amendments.
    // If we ever have two amendments that should not both be
    // enabled at the same time, we should ensure one is vetoed.
    virtual std::vector <uint256>
    getDesired () = 0;

    // The function below adapts the API callers expect to the
    // internal amendment table API. This allows the amendment
    // table implementation to be independent of the ledger
    // implementation. These APIs will merge when the view code
    // supports a full ledger API

    void
    doVoting (
        std::shared_ptr <ReadView const> const& lastClosedLedger,
        std::vector<STValidation::pointer> const& parentValidations,
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
            STTx amendTx (ttAMENDMENT,
                [&it, seq = lastClosedLedger->seq() + 1](auto& obj)
                {
                    obj.setAccountID (sfAccount, AccountID());
                    obj.setFieldH256 (sfAmendment, it.first);
                    obj.setFieldU32 (sfLedgerSequence, seq);

                    if (it.second != 0)
                        obj.setFieldU32 (sfFlags, it.second);
                });

            Serializer s;
            amendTx.add (s);

            initialPosition->addGiveItem (
                std::make_shared <SHAMapItem> (
                    amendTx.getTransactionID(),
                    s.peekData()),
                true,
                false);
        }
    }

};

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    Section const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal);

}  // ripple

#endif
