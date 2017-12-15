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

#include <BeastConfig.h>
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

RCLValidatedLedger::RCLValidatedLedger(
    std::shared_ptr<Ledger const> ledger,
    beast::Journal j)
    : ledger_{std::move(ledger)}, j_{j}
{
    auto const hashIndex = ledger_->read(keylet::skip());
    if (hashIndex)
    {
        assert(hashIndex->getFieldU32(sfLastLedgerSequence) == (seq() - 1));
        ancestors_ = hashIndex->getFieldV256(sfHashes).value();
    }
    else
        JLOG(j_.warn()) << "Ledger " << ledger_->seq() << ":"
                        << ledger_->info().hash
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
    return ledger_ ? ledger_->info().seq : Seq{0};
}
auto
RCLValidatedLedger::id() const -> ID
{
    return ledger_ ? ledger_->info().hash : ID{0};
}

auto RCLValidatedLedger::operator[](Seq const& s) const -> ID
{
    if (ledger_ && s >= minSeq() && s <= seq())
    {
        if (s == seq())
            return ledger_->info().hash;
        Seq const diff = seq() - s;
        if (ancestors_.size() >= diff)
            return ancestors_[ancestors_.size() - diff];
    }

    JLOG(j_.warn()) << "Unable to determine hash of ancestor seq=" << s
                    << " from ledger hash=" << ledger_->info().hash
                    << " seq=" << ledger_->info().seq;
    // Default ID that is less than all others
    return ID{};
}

// Return the sequence number of the earliest possible mismatching ancestor
RCLValidatedLedger::Seq
mismatch(RCLValidatedLedger const& a, RCLValidatedLedger const& b)
{
    using Seq = RCLValidatedLedger::Seq;

    // Find overlapping interval for known sequence for the the ledgers
    Seq const lower = std::max(a.minSeq(), b.minSeq());
    Seq const upper = std::min(a.seq(), b.seq());

    Seq curr = upper;
    while (a[curr] != b[curr] && curr >= lower && curr != Seq{0})
        --curr;

    // If the searchable interval mismatches entirely, then we have to
    // assume the ledgers mismatch starting post genesis ledger
    return (curr < lower) ? Seq{1} : (curr + Seq{1});
}

RCLValidationsAdaptor::RCLValidationsAdaptor(Application& app, beast::Journal j)
    : app_(app),  j_(j)
{
    staleValidations_.reserve(512);
}

NetClock::time_point
RCLValidationsAdaptor::now() const
{
    return app_.timeKeeper().closeTime();
}

boost::optional<RCLValidatedLedger>
RCLValidationsAdaptor::acquire(LedgerHash const & hash)
{
    auto ledger = app_.getLedgerMaster().getLedgerByHash(hash);
    if (!ledger)
    {
        JLOG(j_.debug())
            << "Need validated ledger for preferred ledger analysis " << hash;

        Application * pApp = &app_;

        app_.getJobQueue().addJob(
            jtADVANCE, "getConsensusLedger", [pApp, hash](Job&) {
                pApp ->getInboundLedgers().acquire(
                    hash, 0, InboundLedger::fcVALIDATION);
            });
        return boost::none;
    }

    assert(!ledger->open() && ledger->isImmutable());
    assert(ledger->info().hash == hash);

    return RCLValidatedLedger(std::move(ledger), j_);
}

void
RCLValidationsAdaptor::onStale(RCLValidation&& v)
{
    // Store the newly stale validation; do not do significant work in this
    // function since this is a callback from Validations, which may be
    // doing other work.

    ScopedLockType sl(staleLock_);
    staleValidations_.emplace_back(std::move(v));
    if (staleWriting_)
        return;

    // addJob() may return false (Job not added) at shutdown.
    staleWriting_ = app_.getJobQueue().addJob(
        jtWRITE, "Validations::doStaleWrite", [this](Job&) {
            auto event =
                app_.getJobQueue().makeLoadEvent(jtDISK, "ValidationWrite");
            ScopedLockType sl(staleLock_);
            doStaleWrite(sl);
        });
}

void
RCLValidationsAdaptor::flush(hash_map<PublicKey, RCLValidation>&& remaining)
{
    bool anyNew = false;
    {
        ScopedLockType sl(staleLock_);

        for (auto const& keyVal : remaining)
        {
            staleValidations_.emplace_back(std::move(keyVal.second));
            anyNew = true;
        }

        // If we have new validations to write and there isn't a write in
        // progress already, then write to the database synchronously.
        if (anyNew && !staleWriting_)
        {
            staleWriting_ = true;
            doStaleWrite(sl);
        }

        // In the case when a prior asynchronous doStaleWrite was scheduled,
        // this loop will block until all validations have been flushed.
        // This ensures that all validations are written upon return from
        // this function.

        while (staleWriting_)
        {
            ScopedUnlockType sul(staleLock_);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// NOTE: doStaleWrite() must be called with staleLock_ *locked*.  The passed
// ScopedLockType& acts as a reminder to future maintainers.
void
RCLValidationsAdaptor::doStaleWrite(ScopedLockType&)
{
    static const std::string insVal(
        "INSERT INTO Validations "
        "(InitialSeq, LedgerSeq, LedgerHash,NodePubKey,SignTime,RawData) "
        "VALUES (:initialSeq, :ledgerSeq, "
        ":ledgerHash,:nodePubKey,:signTime,:rawData);");
    static const std::string findSeq(
        "SELECT LedgerSeq FROM Ledgers WHERE Ledgerhash=:ledgerHash;");

    assert(staleWriting_);

    while (!staleValidations_.empty())
    {
        std::vector<RCLValidation> currentStale;
        currentStale.reserve(512);
        staleValidations_.swap(currentStale);

        {
            ScopedUnlockType sul(staleLock_);
            {
                auto db = app_.getLedgerDB().checkoutDb();

                Serializer s(1024);
                soci::transaction tr(*db);
                for (RCLValidation const& wValidation : currentStale)
                {
                    // Only save full validations until we update the schema
                    if(!wValidation.full())
                        continue;
                    s.erase();
                    STValidation::pointer const& val = wValidation.unwrap();
                    val->add(s);

                    auto const ledgerHash = to_string(val->getLedgerHash());

                    boost::optional<std::uint64_t> ledgerSeq;
                    *db << findSeq, soci::use(ledgerHash),
                        soci::into(ledgerSeq);

                    auto const initialSeq = ledgerSeq.value_or(
                        app_.getLedgerMaster().getCurrentLedgerIndex());
                    auto const nodePubKey = toBase58(
                        TokenType::TOKEN_NODE_PUBLIC, val->getSignerPublic());
                    auto const signTime =
                        val->getSignTime().time_since_epoch().count();

                    soci::blob rawData(*db);
                    rawData.append(
                        reinterpret_cast<const char*>(s.peekData().data()),
                        s.peekData().size());
                    assert(rawData.get_len() == s.peekData().size());

                    *db << insVal, soci::use(initialSeq), soci::use(ledgerSeq),
                        soci::use(ledgerHash), soci::use(nodePubKey),
                        soci::use(signTime), soci::use(rawData);
                }

                tr.commit();
            }
        }
    }

    staleWriting_ = false;
}

bool
handleNewValidation(Application& app,
    STValidation::ref val,
    std::string const& source)
{
    PublicKey const& signingKey = val->getSignerPublic();
    uint256 const& hash = val->getLedgerHash();

    // Ensure validation is marked as trusted if signer currently trusted
    boost::optional<PublicKey> masterKey =
        app.validators().getTrustedKey(signingKey);
    if (!val->isTrusted() && masterKey)
        val->setTrusted();

    // If not currently trusted, see if signer is currently listed
    if (!masterKey)
        masterKey = app.validators().getListedKey(signingKey);

    bool shouldRelay = false;
    RCLValidations& validations = app.getValidations();
    beast::Journal j = validations.adaptor().journal();

    // masterKey is seated only if validator is trusted or listed
    if (masterKey)
    {
        ValStatus const outcome = validations.add(*masterKey, val);

        auto dmp = [&](beast::Journal::Stream s, std::string const& msg) {
            s << "Val for " << hash
              << (val->isTrusted() ? " trusted/" : " UNtrusted/")
              << (val->isFull() ? " full" : "partial") << " from "
              << toBase58(TokenType::TOKEN_NODE_PUBLIC, *masterKey)
              << "signing key "
              << toBase58(TokenType::TOKEN_NODE_PUBLIC, signingKey) << " "
              << msg
              << " src=" << source;
        };

        if(j.debug())
            dmp(j.debug(), to_string(outcome));

        if(outcome == ValStatus::badFullSeq && j.warn())
        {
            auto const seq = val->getFieldU32(sfLedgerSequence);
            dmp(j.warn(), " already fully validated sequence past " + to_string(seq));
        }

        if (val->isTrusted() && outcome == ValStatus::current)
        {
            app.getLedgerMaster().checkAccept(
                hash, val->getFieldU32(sfLedgerSequence));

            shouldRelay = true;
        }

    }
    else
    {
        JLOG(j.debug()) << "Val for " << hash << " from "
                    << toBase58(TokenType::TOKEN_NODE_PUBLIC, signingKey)
                    << " not added UNlisted";
    }

    // This currently never forwards untrusted validations, though we may
    // reconsider in the future. From @JoelKatz:
    // The idea was that we would have a certain number of validation slots with
    // priority going to validators we trusted. Remaining slots might be
    // allocated to validators that were listed by publishers we trusted but
    // that we didn't choose to trust. The shorter term plan was just to forward
    // untrusted validations if peers wanted them or if we had the
    // ability/bandwidth to. None of that was implemented.
    return shouldRelay;
}

std::size_t
getNodesAfter(
    RCLValidations& vals,
    std::shared_ptr<Ledger const> ledger,
    uint256 const& ledgerID)
{
    return vals.getNodesAfter(
        RCLValidatedLedger{std::move(ledger), vals.adaptor().journal()},
        ledgerID);
}

uint256
getPreferred(
    RCLValidations& vals,
    std::shared_ptr<Ledger const> ledger,
    LedgerIndex minValidSeq)
{
    return vals.getPreferred(
        RCLValidatedLedger{std::move(ledger), vals.adaptor().journal()},
        minValidSeq);
}

uint256
getPreferredLCL(
    RCLValidations& vals,
    std::shared_ptr<Ledger const> ledger,
    LedgerIndex minSeq,
    hash_map<uint256, std::uint32_t> const& peerCounts)
{
    return vals.getPreferredLCL(
        RCLValidatedLedger{std::move(ledger), vals.adaptor().journal()},
        minSeq,
        peerCounts);
}
}  // namespace ripple
