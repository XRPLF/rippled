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

#include <ripple/basics/contract.h>
#include <ripple/nodestore/Factory.h>
#include <ripple/nodestore/Manager.h>
#include <boost/beast/core/string.hpp>
#include <map>
#include <memory>
#include <mutex>

namespace ripple {
namespace NodeStore {

struct MemoryDB
{
    explicit MemoryDB() = default;

    std::mutex mutex;
    bool open = false;
    std::map <uint256 const, std::shared_ptr<NodeObject>> table;
};

class MemoryFactory : public Factory
{
private:
    std::mutex mutex_;
    std::map <std::string, MemoryDB, boost::beast::iless> map_;

public:
    MemoryFactory();
    ~MemoryFactory();

    std::string
    getName() const;

    std::unique_ptr <Backend>
    createInstance (
        size_t keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal);

    MemoryDB&
    open (std::string const& path)
    {
        std::lock_guard<std::mutex> _(mutex_);
        auto const result = map_.emplace (std::piecewise_construct,
            std::make_tuple(path), std::make_tuple());
        MemoryDB& db = result.first->second;
        if (db.open)
            Throw<std::runtime_error> ("already open");
        return db;
    }
};

static MemoryFactory memoryFactory;

//------------------------------------------------------------------------------

class MemoryBackend : public Backend
{
private:
    using Map = std::map <uint256 const, std::shared_ptr<NodeObject>>;

    std::string name_;
    beast::Journal journal_;
    MemoryDB* db_ {nullptr};

public:
    MemoryBackend (size_t keyBytes, Section const& keyValues,
        Scheduler& scheduler, beast::Journal journal)
        : name_ (get<std::string>(keyValues, "path"))
        , journal_ (journal)
    {
        if (name_.empty())
            Throw<std::runtime_error> ("Missing path in Memory backend");
    }

    ~MemoryBackend ()
    {
        close();
    }

    std::string
    getName () override
    {
        return name_;
    }

    void
    open() override
    {
        db_ = &memoryFactory.open(name_);
    }

    void
    close() override
    {
        db_ = nullptr;
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, std::shared_ptr<NodeObject>* pObject) override
    {
        assert(db_);
        uint256 const hash (uint256::fromVoid (key));

        std::lock_guard<std::mutex> _(db_->mutex);

        Map::iterator iter = db_->table.find (hash);
        if (iter == db_->table.end())
        {
            pObject->reset();
            return notFound;
        }
        *pObject = iter->second;
        return ok;
    }

    bool
    canFetchBatch() override
    {
        return false;
    }

    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch (std::size_t n, void const* const* keys) override
    {
        Throw<std::runtime_error> ("pure virtual called");
        return {};
    }

    void
    store (std::shared_ptr<NodeObject> const& object) override
    {
        assert(db_);
        std::lock_guard<std::mutex> _(db_->mutex);
        db_->table.emplace (object->getHash(), object);
    }

    void
    storeBatch (Batch const& batch) override
    {
        for (auto const& e : batch)
            store (e);
    }

    void
    for_each (std::function <void(std::shared_ptr<NodeObject>)> f) override
    {
        assert(db_);
        for (auto const& e : db_->table)
            f (e.second);
    }

    int
    getWriteLoad() override
    {
        return 0;
    }

    void
    setDeletePath() override
    {
    }

    void
    verify() override
    {
    }

    int
    fdlimit() const override
    {
        return 0;
    }
};

//------------------------------------------------------------------------------

MemoryFactory::MemoryFactory()
{
    Manager::instance().insert(*this);
}

MemoryFactory::~MemoryFactory()
{
    Manager::instance().erase(*this);
}

std::string
MemoryFactory::getName() const
{
    return "Memory";
}

std::unique_ptr <Backend>
MemoryFactory::createInstance (
    size_t keyBytes,
    Section const& keyValues,
    Scheduler& scheduler,
    beast::Journal journal)
{
    return std::make_unique <MemoryBackend> (
        keyBytes, keyValues, scheduler, journal);
}

}
}
