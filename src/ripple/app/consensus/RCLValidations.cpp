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

std::optional<RCLValidatedLedger>
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
        return std::nullopt;
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
    auto const& signingKey = val->getSignerPublic();
    auto const& hash = val->getLedgerHash();
    auto const seq = val->getFieldU32(sfLedgerSequence);

    // Ensure validation is marked as trusted if signer currently trusted
    auto masterKey = app.validators().getTrustedKey(signingKey);

    if (!val->isTrusted() && masterKey)
        val->setTrusted();

    // If not currently trusted, see if signer is currently listed
    if (!masterKey)
        masterKey = app.validators().getListedKey(signingKey);

    auto& validations = app.getValidations();

    // masterKey is seated only if validator is trusted or listed
    auto const outcome =
        validations.add(calcNodeID(masterKey.value_or(signingKey)), val);

    if (outcome == ValStatus::current)
    {
        if (val->isTrusted())
            app.getLedgerMaster().checkAccept(hash, seq);
        return;
    }

    // Ensure that problematic validations from validators we trust are
    // logged at the highest possible level.
    //
    // One might think that we should more than just log: we ought to also
    // not relay validations that fail these checks. Alas, and somewhat
    // counterintuitively, we *especially* want to forward such validations,
    // so that our peers will also observe them and take independent notice of
    // such validators, informing their operators.
    if (auto const ls = val->isTrusted()
            ? validations.adaptor().journal().fatal()
            : validations.adaptor().journal().warn();
        ls.active())
    {
        auto const id = [&masterKey, &signingKey]() {
            auto ret = toBase58(TokenType::NodePublic, signingKey);

            if (masterKey && masterKey != signingKey)
                ret += ":" + toBase58(TokenType::NodePublic, *masterKey);

            return ret;
        }();

        if (outcome == ValStatus::conflicting)
            ls << "Byzantine Behavior Detector: "
               << (val->isTrusted() ? "trusted " : "untrusted ") << id
               << ": Conflicting validation for " << seq << "!\n["
               << val->getSerializer().slice() << "]";

        if (outcome == ValStatus::multiple)
            ls << "Byzantine Behavior Detector: "
               << (val->isTrusted() ? "trusted " : "untrusted ") << id
               << ": Multiple validations for " << seq << "/" << hash << "!\n["
               << val->getSerializer().slice() << "]";
    }
}

}  // namespace ripple
