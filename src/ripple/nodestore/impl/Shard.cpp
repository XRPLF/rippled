//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#include <ripple/nodestore/impl/Shard.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/nodestore/impl/DatabaseShardImp.h>
#include <ripple/nodestore/Manager.h>

#include <fstream>

namespace ripple {
namespace NodeStore {

Shard::Shard(DatabaseShard const& db, std::uint32_t index,
    int cacheSz, std::chrono::seconds cacheAge, beast::Journal& j)
    : index_(index)
    , firstSeq_(db.firstLedgerSeq(index))
    , lastSeq_(std::max(firstSeq_, db.lastLedgerSeq(index)))
    , maxLedgers_(index == db.earliestShardIndex() ?
        lastSeq_ - firstSeq_ + 1 : db.ledgersPerShard())
    , pCache_(std::make_shared<PCache>(
        "shard " + std::to_string(index_),
        cacheSz, cacheAge, stopwatch(), j))
    , nCache_(std::make_shared<NCache>(
        "shard " + std::to_string(index_),
        stopwatch(), cacheSz, cacheAge))
    , j_(j)
{
    if (index_ < db.earliestShardIndex())
        Throw<std::runtime_error>("Shard: Invalid index");
}

bool
Shard::open(Section config, Scheduler& scheduler,
    boost::filesystem::path dir)
{
    assert(!backend_);
    using namespace boost::filesystem;
    dir_ = dir / std::to_string(index_);
    config.set("path", dir_.string());
    auto newShard {!is_directory(dir_) || is_empty(dir_)};
    try
    {
        backend_ = Manager::instance().make_Backend(
            config, scheduler, j_);
        backend_->open();
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "shard " << index_ <<
            " exception: " << e.what();
        return false;
    }

    if (backend_->fdlimit() == 0)
        return true;

    control_ = dir_ / controlFileName;
    if (newShard)
    {
        if (!saveControl())
            return false;
    }
    else if (is_regular_file(control_))
    {
        std::ifstream ifs(control_.string());
        if (!ifs.is_open())
        {
            JLOG(j_.error()) <<
                "shard " << index_ <<
                " unable to open control file";
            return false;
        }
        boost::archive::text_iarchive ar(ifs);
        ar & storedSeqs_;
        if (!storedSeqs_.empty())
        {
            if (boost::icl::first(storedSeqs_) < firstSeq_ ||
                boost::icl::last(storedSeqs_) > lastSeq_)
            {
                JLOG(j_.error()) <<
                    "shard " << index_ <<
                    " invalid control file";
                return false;
            }
            if (boost::icl::length(storedSeqs_) >= maxLedgers_)
            {
                JLOG(j_.error()) <<
                    "shard " << index_ <<
                    " found control file for complete shard";
                storedSeqs_.clear();
                remove(control_);
                complete_ = true;
            }
        }
    }
    else
        complete_ = true;
    updateFileSize();
    return true;
}

bool
Shard::setStored(std::shared_ptr<Ledger const> const& l)
{
    assert(backend_&& !complete_);
    if (boost::icl::contains(storedSeqs_, l->info().seq))
    {
        JLOG(j_.debug()) <<
            "shard " << index_ <<
            " ledger seq " << l->info().seq <<
            " already stored";
        return false;
    }
    if (boost::icl::length(storedSeqs_) >= maxLedgers_ - 1)
    {
        if (backend_->fdlimit() != 0)
        {
            remove(control_);
            updateFileSize();
        }
        complete_ = true;
        storedSeqs_.clear();

        JLOG(j_.debug()) <<
            "shard " << index_ <<
            " ledger seq " << l->info().seq <<
            " stored. Shard complete";
    }
    else
    {
        storedSeqs_.insert(l->info().seq);
        lastStored_ = l;
        if (backend_->fdlimit() != 0 && !saveControl())
            return false;

        JLOG(j_.debug()) <<
            "shard " << index_ <<
            " ledger seq " << l->info().seq <<
            " stored";
    }

    return true;
}

boost::optional<std::uint32_t>
Shard::prepare()
{
    if (storedSeqs_.empty())
         return lastSeq_;
    return prevMissing(storedSeqs_, 1 + lastSeq_, firstSeq_);
}

bool
Shard::contains(std::uint32_t seq) const
{
    if (seq < firstSeq_ || seq > lastSeq_)
        return false;
    if (complete_)
        return true;
    return boost::icl::contains(storedSeqs_, seq);
}

void
Shard::validate(Application& app)
{
    uint256 hash;
    std::uint32_t seq;
    std::shared_ptr<Ledger> l;
    // Find the hash of the last ledger in this shard
    {
        std::tie(l, seq, hash) = loadLedgerHelper(
            "WHERE LedgerSeq >= " + std::to_string(lastSeq_) +
            " order by LedgerSeq desc limit 1", app, false);
        if (!l)
        {
            JLOG(j_.fatal()) <<
                "shard " << index_ <<
                " unable to validate. No lookup data";
            return;
        }
        if (seq != lastSeq_)
        {
            l->setImmutable(app.config());
            boost::optional<uint256> h;
            try
            {
                h = hashOfSeq(*l, lastSeq_, j_);
            }
            catch (std::exception const& e)
            {
                JLOG(j_.fatal()) <<
                    "exception: " << e.what();
                return;
            }
            if (!h)
            {
                JLOG(j_.fatal()) <<
                    "shard " << index_ <<
                    " No hash for last ledger seq " << lastSeq_;
                return;
            }
            hash = *h;
            seq = lastSeq_;
        }
    }

    JLOG(j_.fatal()) <<
        "Validating shard " << index_ <<
        " ledgers " << firstSeq_ <<
        "-" << lastSeq_;

    // Use a short age to keep memory consumption low
    auto const savedAge {pCache_->getTargetAge()};
    pCache_->setTargetAge(1s);

    // Validate every ledger stored in this shard
    std::shared_ptr<Ledger const> next;
    while (seq >= firstSeq_)
    {
        auto nObj = valFetch(hash);
        if (!nObj)
            break;
        l = std::make_shared<Ledger>(
            InboundLedger::deserializeHeader(makeSlice(nObj->getData()),
                true), app.config(), *app.shardFamily());
        if (l->info().hash != hash || l->info().seq != seq)
        {
            JLOG(j_.fatal()) <<
                "ledger seq " << seq <<
                " hash " << hash <<
                " cannot be a ledger";
            break;
        }
        l->stateMap().setLedgerSeq(seq);
        l->txMap().setLedgerSeq(seq);
        l->setImmutable(app.config());
        if (!l->stateMap().fetchRoot(
            SHAMapHash {l->info().accountHash}, nullptr))
        {
            JLOG(j_.fatal()) <<
                "ledger seq " << seq <<
                " missing Account State root";
            break;
        }
        if (l->info().txHash.isNonZero())
        {
            if (!l->txMap().fetchRoot(
                SHAMapHash {l->info().txHash}, nullptr))
            {
                JLOG(j_.fatal()) <<
                    "ledger seq " << seq <<
                    " missing TX root";
                break;
            }
        }
        if (!valLedger(l, next))
            break;
        hash = l->info().parentHash;
        --seq;
        next = l;
        if (seq % 128 == 0)
            pCache_->sweep();
    }
    if (seq < firstSeq_)
    {
        JLOG(j_.fatal()) <<
            "shard " << index_ <<
            " is complete.";
    }
    else if (complete_)
    {
        JLOG(j_.fatal()) <<
            "shard " << index_ <<
            " is invalid, failed on seq " << seq <<
            " hash " << hash;
    }
    else
    {
        JLOG(j_.fatal()) <<
            "shard " << index_ <<
            " is incomplete, stopped at seq " << seq <<
            " hash " << hash;
    }

    pCache_->reset();
    nCache_->reset();
    pCache_->setTargetAge(savedAge);
}

bool
Shard::valLedger(std::shared_ptr<Ledger const> const& l,
    std::shared_ptr<Ledger const> const& next)
{
    if (l->info().hash.isZero() || l->info().accountHash.isZero())
    {
        JLOG(j_.fatal()) <<
            "invalid ledger";
        return false;
    }
    bool error {false};
    auto f = [&, this](SHAMapAbstractNode& node) {
        if (!valFetch(node.getNodeHash().as_uint256()))
            error = true;
        return !error;
    };
    // Validate the state map
    if (l->stateMap().getHash().isNonZero())
    {
        if (!l->stateMap().isValid())
        {
            JLOG(j_.error()) <<
                "invalid state map";
            return false;
        }
        try
        {
            if (next && next->info().parentHash == l->info().hash)
                l->stateMap().visitDifferences(&next->stateMap(), f);
            else
                l->stateMap().visitNodes(f);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) <<
                "exception: " << e.what();
            return false;
        }
        if (error)
            return false;
    }
    // Validate the tx map
    if (l->info().txHash.isNonZero())
    {
        if (!l->txMap().isValid())
        {
            JLOG(j_.error()) <<
                "invalid transaction map";
            return false;
        }
        try
        {
            l->txMap().visitNodes(f);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) <<
                "exception: " << e.what();
            return false;
        }
        if (error)
            return false;
    }
    return true;
};

std::shared_ptr<NodeObject>
Shard::valFetch(uint256 const& hash)
{
    assert(backend_);
    std::shared_ptr<NodeObject> nObj;
    try
    {
        switch (backend_->fetch(hash.begin(), &nObj))
        {
        case ok:
            break;
        case notFound:
        {
            JLOG(j_.fatal()) <<
                "NodeObject not found. hash " << hash;
            break;
        }
        case dataCorrupt:
        {
            JLOG(j_.fatal()) <<
                "NodeObject is corrupt. hash " << hash;
            break;
        }
        default:
        {
            JLOG(j_.fatal()) <<
                "unknown error. hash " << hash;
        }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) <<
            "exception: " << e.what();
    }
    return nObj;
}

void
Shard::updateFileSize()
{
    fileSize_ = 0;
    using namespace boost::filesystem;
    for (auto const& d : directory_iterator(dir_))
        if (is_regular_file(d))
            fileSize_ += file_size(d);
}

bool
Shard::saveControl()
{
    std::ofstream ofs {control_.string(), std::ios::trunc};
    if (!ofs.is_open())
    {
        JLOG(j_.fatal()) <<
            "shard " << index_ <<
            " unable to save control file";
        return false;
    }
    boost::archive::text_oarchive ar(ofs);
    ar & storedSeqs_;
    return true;
}

} // NodeStore
} // ripple
