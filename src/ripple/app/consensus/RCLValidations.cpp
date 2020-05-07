//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/chrono.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/TimeKeeper.h>
#include <memory>
#include <mutex>
#include <thread>

namespace ripple {

RCLValidatedLedger::RCLValidatedLedger(MakeGenesis)
    : ledgerID_{0}, ledgerSeq_{0}, j_{beast::Journal::getNullSink()}
{
}

RCLValidatedLedger::RCLValidatedLedger(
    std::shared_ptr<Ledger const> const& ledger,
    beast::Journal j)
    : ledgerID_{ledger->info().hash}, ledgerSeq_{ledger->seq()}, j_{j}
{
    auto const hashIndex = ledger->read(keylet::skip());
    if (hashIndex)
    {
        assert(hashIndex->getFieldU32(sfLastLedgerSequence) == (seq() - 1));
        ancestors_ = hashIndex->getFieldV256(sfHashes).value();
    }
    else
        JLOG(j_.warn()) << "Ledger " << ledgerSeq_ << ":" << ledgerID_
                        << " missing recent ancestor hashes";
}

auto
RCLValidatedLedger::minSeq() const -> Seq
{
    return seq() - std::min(seq(), static_cast<Seq>(ancestors_.size()));
}

auto
RCLValidatedLedger::seq() const -> Seq
{
    return ledgerSeq_;
}
auto
RCLValidatedLedger::id() const -> ID
{
    return ledgerID_;
}

auto
RCLValidatedLedger::operator[](Seq const& s) const -> ID
{
    if (s >= minSeq() && s <= seq())
    {
        if (s == seq())
            return ledgerID_;
        Seq const diff = seq() - s;
        return ancestors_[ancestors_.size() - diff];
    }

    JLOG(j_.warn()) << "Unable to determine hash of ancestor seq=" << s
                    << " from ledger hash=" << ledgerID_
                    << " seq=" << ledgerSeq_;
    // Default ID that is less than all others
    return ID{0};
}

// Return the sequence number of the earliest possible mismatching ancestor
RCLValidatedLedger::Seq
mismatch(RCLValidatedLedger const& a, RCLValidatedLedger const& b)
{
    using Seq = RCLValidatedLedger::Seq;

    // Find overlapping interval for known sequence for the ledgers
    Seq const lower = std::max(a.minSeq(), b.minSeq());
    Seq const upper = std::min(a.seq(), b.seq());

    Seq curr = upper;
    while (curr != Seq{0} && a[curr] != b[curr] && curr >= lower)
        --curr;

    // If the searchable interval mismatches entirely, then we have to
    // assume the ledgers mismatch starting post genesis ledger
    return (curr < lower) ? Seq{1} : (curr + Seq{1});
}

RCLValidationsAdaptor::RCLValidationsAdaptor(Application& app, beast::Journal j)
    : app_(app), j_(j)
{
}

NetClock::time_point
RCLValidationsAdaptor::now() const
{
    return app_.timeKeeper().closeTime();
}

boost::optional<RCLValidatedLedger>
RCLValidationsAdaptor::acquire(LedgerHash const& hash)
{
    auto ledger = app_.getLedgerMaster().getLedgerByHash(hash);
    if (!ledger)
    {
        JLOG(j_.debug())
            << "Need validated ledger for preferred ledger analysis " << hash;

        Application* pApp = &app_;

        app_.getJobQueue().addJob(
            jtADVANCE, "getConsensusLedger", [pApp, hash](Job&) {
                pApp->getInboundLedgers().acquire(
                    hash, 0, InboundLedger::Reason::CONSENSUS);
            });
        return boost::none;
    }

    assert(!ledger->open() && ledger->isImmutable());
    assert(ledger->info().hash == hash);

    return RCLValidatedLedger(std::move(ledger), j_);
}

void
handleNewValidation(
    Application& app,
    std::shared_ptr<STValidation> const& val,
    std::string const& source)
{
    PublicKey const& signingKey = val->getSignerPublic();
    uint256 const& hash = val->getLedgerHash();

    // Ensure validation is marked as trusted if signer currently trusted
    auto masterKey = app.validators().getTrustedKey(signingKey);
    if (!val->isTrusted() && masterKey)
        val->setTrusted();

    // If not currently trusted, see if signer is currently listed
    if (!masterKey)
        masterKey = app.validators().getListedKey(signingKey);

    RCLValidations& validations = app.getValidations();
    beast::Journal const j = validations.adaptor().journal();

    auto dmp = [&](beast::Journal::Stream s, std::string const& msg) {
        std::string id = toBase58(TokenType::NodePublic, signingKey);

        if (masterKey)
            id += ":" + toBase58(TokenType::NodePublic, *masterKey);

        s << (val->isTrusted() ? "trusted" : "untrusted") << " "
          << (val->isFull() ? "full" : "partial") << " validation: " << hash
          << " from " << id << " via " << source << ": " << msg << "\n"
          << " [" << val->getSerializer().slice() << "]";
    };

    // masterKey is seated only if validator is trusted or listed
    if (masterKey)
    {
        ValStatus const outcome = validations.add(calcNodeID(*masterKey), val);
        auto const seq = val->getFieldU32(sfLedgerSequence);

        if (j.debug())
            dmp(j.debug(), to_string(outcome));

        // One might think that we would not wish to relay validations that
        // fail these checks. Somewhat counterintuitively, we actually want
        // to do it for validations that we receive but deem suspicious, so
        // that our peers will also observe them and realize they're bad.
        if (outcome == ValStatus::conflicting && j.warn())
        {
            dmp(j.warn(),
                "conflicting validations issued for " + to_string(seq) +
                    " (likely from a Byzantine validator)");
        }

        if (outcome == ValStatus::multiple && j.warn())
        {
            dmp(j.warn(),
                "multiple validations issued for " + to_string(seq) +
                    " (multiple validators operating with the same key?)");
        }

        if (val->isTrusted() && outcome == ValStatus::current)
            app.getLedgerMaster().checkAccept(hash, seq);
    }
    else
    {
        JLOG(j.debug()) << "Val for " << hash << " from "
                        << toBase58(TokenType::NodePublic, signingKey)
                        << " not added UNlisted";
    }
}

std::vector<std::shared_ptr<STValidation>>
negativeUNLFilter(
    std::vector<std::shared_ptr<STValidation>> const& validations,
    hash_set<NodeID> const& negUnl)
{
    /* Remove validations that are from validators on the negative UNL. */
    if (negUnl.empty())
        return validations;

    std::vector<std::shared_ptr<STValidation>> res;
    for (auto const& v : validations)
    {
        if (negUnl.find(v->getNodeID()) == negUnl.end())
            res.push_back(v);
    }
    return res;
}

}  // namespace ripple
