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

#if RIPPLE_LEVELDB_AVAILABLE

#include <ripple/module/core/functional/Config.h>

namespace ripple {
namespace NodeStore {

class LevelDBBackend
    : public Backend
    , public BatchWriter::Callback
    , public beast::LeakChecked <LevelDBBackend>
{
public:
    beast::Journal m_journal;
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    BatchWriter m_batch;
    std::string m_name;
    std::unique_ptr <leveldb::DB> m_db;

    LevelDBBackend (int keyBytes, Parameters const& keyValues,
        Scheduler& scheduler, beast::Journal journal)
        : m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_name (keyValues ["path"].toStdString ())
    {
        if (m_name.empty())
            throw std::runtime_error ("Missing path in LevelDBFactory backend");

        leveldb::Options options;
        options.create_if_missing = true;

        if (keyValues["cache_mb"].isEmpty())
        {
            options.block_cache = leveldb::NewLRUCache (getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);
        }
        else
        {
            options.block_cache = leveldb::NewLRUCache (keyValues["cache_mb"].getIntValue() * 1024L * 1024L);
        }

        if (keyValues["filter_bits"].isEmpty())
        {
            if (getConfig ().NODE_SIZE >= 2)
                options.filter_policy = leveldb::NewBloomFilterPolicy (10);
        }
        else if (keyValues["filter_bits"].getIntValue() != 0)
        {
            options.filter_policy = leveldb::NewBloomFilterPolicy (keyValues["filter_bits"].getIntValue());
        }

        if (! keyValues["open_files"].isEmpty())
        {
            options.max_open_files = keyValues["open_files"].getIntValue();
        }

        if (! keyValues["compression"].isEmpty ())
        {
            if (keyValues["compression"].getIntValue () == 0)
            {
                options.compression = leveldb::kNoCompression;
            }
        }

        leveldb::DB* db = nullptr;
        leveldb::Status status = leveldb::DB::Open (options, m_name, &db);
        if (!status.ok () || !db)
            throw std::runtime_error (std::string("Unable to open/create leveldb: ") + status.ToString());

        m_db.reset (db);
    }

    std::string
    getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        leveldb::ReadOptions const options;
        leveldb::Slice const slice (static_cast <char const*> (key), m_keyBytes);
        std::string string;

        leveldb::Status getStatus = m_db->Get (options, slice, &string);

        if (getStatus.ok ())
        {
            DecodedBlob decoded (key, string.data (), string.size ());

            if (decoded.wasOk ())
            {
                *pObject = decoded.createObject ();
            }
            else
            {
                // Decoding failed, probably corrupted!
                //
                status = dataCorrupt;
            }
        }
        else
        {
            if (getStatus.IsCorruption ())
            {
                status = dataCorrupt;
            }
            else if (getStatus.IsNotFound ())
            {
                status = notFound;
            }
            else
            {
                status = unknown;
            }
        }

        return status;
    }

    void
    store (NodeObject::ref object)
    {
        m_batch.store (object);
    }

    void
    storeBatch (Batch const& batch)
    {
        leveldb::WriteBatch wb;

        EncodedBlob encoded;

        for (auto const& e : batch)
        {
            encoded.prepare (e);

            wb.Put (
                leveldb::Slice (reinterpret_cast <char const*> (
                    encoded.getKey ()), m_keyBytes),
                leveldb::Slice (reinterpret_cast <char const*> (
                    encoded.getData ()), encoded.getSize ()));
        }

        leveldb::WriteOptions const options;

        m_db->Write (options, &wb).ok ();
    }

    void
    for_each (std::function <void(NodeObject::Ptr)> f)
    {
        leveldb::ReadOptions const options;

        std::unique_ptr <leveldb::Iterator> it (m_db->NewIterator (options));

        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == m_keyBytes)
            {
                DecodedBlob decoded (it->key ().data (),
                                                it->value ().data (),
                                                it->value ().size ());

                if (decoded.wasOk ())
                {
                    f (decoded.createObject ());
                }
                else
                {
                    // Uh oh, corrupted data!
                    if (m_journal.fatal) m_journal.fatal <<
                        "Corrupt NodeObject #" << uint256(it->key ().data ());
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                if (m_journal.fatal) m_journal.fatal <<
                    "Bad key size = " << it->key().size();
            }
        }
    }

    int
    getWriteLoad ()
    {
        return m_batch.getWriteLoad ();
    }

    //--------------------------------------------------------------------------

    void
    writeBatch (Batch const& batch)
    {
        storeBatch (batch);
    }
};

//------------------------------------------------------------------------------

class LevelDBFactory : public Factory
{
public:
    std::unique_ptr <leveldb::Cache> m_lruCache;

    class BackendImp;

    LevelDBFactory ()
        : m_lruCache (nullptr)
    {
        leveldb::Options options;
        options.create_if_missing = true;
        options.block_cache = leveldb::NewLRUCache (
            getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);
        m_lruCache.reset (options.block_cache);
    }

    ~LevelDBFactory()
    {
    }

    beast::String
    getName () const
    {
        return "LevelDB";
    }

    std::unique_ptr <Backend>    
    createInstance(
        size_t keyBytes,
        Parameters const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <LevelDBBackend> (
            keyBytes, keyValues, scheduler, journal);
    }
};

//------------------------------------------------------------------------------

std::unique_ptr <Factory>
make_LevelDBFactory ()
{
    return std::make_unique <LevelDBFactory> ();
}

}
}

#endif
