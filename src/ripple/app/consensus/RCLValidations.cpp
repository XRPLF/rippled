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

RCLValidationsPolicy::RCLValidationsPolicy(Application& app) : app_(app)
{
    staleValidations_.reserve(512);
}

NetClock::time_point
RCLValidationsPolicy::now() const
{
    return app_.timeKeeper().closeTime();
}

void
RCLValidationsPolicy::onStale(RCLValidation&& v)
{
    // Store the newly stale validation; do not do significant work in this
    // function since this is a callback from Validations, which may be
    // doing other work.

    ScopedLockType sl(staleLock_);
    staleValidations_.emplace_back(std::move(v));
    if (staleWriting_)
        return;

    // addJob() may return false (Job not added) at shutdown.
    staleWriting_  = app_.getJobQueue().addJob(
        jtWRITE, "Validations::doStaleWrite", [this](Job&) {
            auto event =
                app_.getJobQueue().makeLoadEvent(jtDISK, "ValidationWrite");
            ScopedLockType sl(staleLock_);
            doStaleWrite(sl);
        });
}

void
RCLValidationsPolicy::flush(hash_map<PublicKey, RCLValidation>&& remaining)
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
RCLValidationsPolicy::doStaleWrite(ScopedLockType&)
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
                for (auto const& rclValidation : currentStale)
                {
                    s.erase();
                    STValidation::pointer const& val = rclValidation.unwrap();
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
    PublicKey const& signer = val->getSignerPublic();
    uint256 const& hash = val->getLedgerHash();

    // Ensure validation is marked as trusted if signer currently trusted
    boost::optional<PublicKey> pubKey = app.validators().getTrustedKey(signer);
    if (!val->isTrusted() && pubKey)
        val->setTrusted();
    RCLValidations& validations  = app.getValidations();

    beast::Journal j = validations.journal();

    // Do not process partial validations.
    if (!val->isFull())
    {
        const bool current = isCurrent(
            validations.parms(),
            app.timeKeeper().closeTime(),
            val->getSignTime(),
            val->getSeenTime());

        JLOG(j.debug()) << "Val (partial) for " << hash << " from "
                         << toBase58(TokenType::TOKEN_NODE_PUBLIC, signer)
                         << " ignored "
                         << (val->isTrusted() ? "trusted/" : "UNtrusted/")
                         << (current ? "current" : "stale");

        // Only forward if current and trusted
        return current && val->isTrusted();
    }

    if (!val->isTrusted())
    {
        JLOG(j.trace()) << "Node "
                        << toBase58(TokenType::TOKEN_NODE_PUBLIC, signer)
                        << " not in UNL st="
                        << val->getSignTime().time_since_epoch().count()
                        << ", hash=" << hash
                        << ", shash=" << val->getSigningHash()
                        << " src=" << source;
    }

    // If not currently trusted, see if signer is currently listed
    if (!pubKey)
        pubKey = app.validators().getListedKey(signer);

    bool shouldRelay = false;

    // only add trusted or listed
    if (pubKey)
    {
        using AddOutcome = RCLValidations::AddOutcome;

        AddOutcome const res = validations.add(*pubKey, val);

        // This is a duplicate validation
        if (res == AddOutcome::repeat)
            return false;

        // This validation replaced a prior one with the same sequence number
        if (res == AddOutcome::sameSeq)
        {
            auto const seq = val->getFieldU32(sfLedgerSequence);
            JLOG(j.warn()) << "Trusted node "
                           << toBase58(TokenType::TOKEN_NODE_PUBLIC, *pubKey)
                           << " published multiple validations for ledger "
                           << seq;
        }

        JLOG(j.debug()) << "Val for " << hash << " from "
                    << toBase58(TokenType::TOKEN_NODE_PUBLIC, signer)
                    << " added "
                    << (val->isTrusted() ? "trusted/" : "UNtrusted/")
                    << ((res == AddOutcome::current) ? "current" : "stale");

        // Trusted current validations should be checked and relayed.
        // Trusted validations with sameSeq replaced an older validation
        // with that sequence number, so should still be checked and relayed.
        if (val->isTrusted() &&
            (res == AddOutcome::current || res == AddOutcome::sameSeq))
        {
            app.getLedgerMaster().checkAccept(
                hash, val->getFieldU32(sfLedgerSequence));

            shouldRelay = true;
        }
    }
    else
    {
        JLOG(j.debug()) << "Val for " << hash << " from "
                    << toBase58(TokenType::TOKEN_NODE_PUBLIC, signer)
                    << " not added UNtrusted/";
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
}  // namespace ripple
