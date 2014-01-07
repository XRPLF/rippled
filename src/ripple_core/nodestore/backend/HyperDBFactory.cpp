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

#if RIPPLE_HYPERLEVELDB_AVAILABLE

namespace NodeStore
{

class HyperDBFactory::BackendImp
    : public Backend
    , public BatchWriter::Callback
    , public LeakChecked <HyperDBFactory::BackendImp>
{
public:
    typedef RecycledObjectPool <std::string> StringPool;

    BackendImp (size_t keyBytes,
             Parameters const& keyValues,
             Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_name (keyValues ["path"].toStdString ())
    {
        if (m_name.empty ())
            Throw (std::runtime_error ("Missing path in LevelDBFactory backend"));

        hyperleveldb::Options options;
        options.create_if_missing = true;

        if (keyValues ["cache_mb"].isEmpty ())
        {
            options.block_cache = hyperleveldb::NewLRUCache (getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);
        }
        else
        {
            options.block_cache = hyperleveldb::NewLRUCache (keyValues["cache_mb"].getIntValue() * 1024L * 1024L);
        }

        if (keyValues ["filter_bits"].isEmpty())
        {
            if (getConfig ().NODE_SIZE >= 2)
                options.filter_policy = hyperleveldb::NewBloomFilterPolicy (10);
        }
        else if (keyValues ["filter_bits"].getIntValue() != 0)
        {
            options.filter_policy = hyperleveldb::NewBloomFilterPolicy (keyValues ["filter_bits"].getIntValue ());
        }

        if (! keyValues["open_files"].isEmpty ())
        {
            options.max_open_files = keyValues ["open_files"].getIntValue();
        }

        hyperleveldb::DB* db = nullptr;
        hyperleveldb::Status status = hyperleveldb::DB::Open (options, m_name, &db);
        if (!status.ok () || !db)
            Throw (std::runtime_error (std::string("Unable to open/create leveldb: ") + status.ToString()));

        m_db = db;
    }

    ~BackendImp ()
    {
    }

    std::string getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        hyperleveldb::ReadOptions const options;
        hyperleveldb::Slice const slice (static_cast <char const*> (key), m_keyBytes);

        {
            // These are reused std::string objects,
            // required for leveldb's funky interface.
            //
            StringPool::ScopedItem item (m_stringPool);
            std::string& string = item.getObject ();

            hyperleveldb::Status getStatus = m_db->Get (options, slice, &string);

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
        }

        return status;
    }

    void store (NodeObject::ref object)
    {
        m_batch.store (object);
    }

    void storeBatch (Batch const& batch)
    {
        hyperleveldb::WriteBatch wb;

        {
            EncodedBlob::Pool::ScopedItem item (m_blobPool);

            BOOST_FOREACH (NodeObject::ref object, batch)
            {
                item.getObject ().prepare (object);

                wb.Put (
                    hyperleveldb::Slice (reinterpret_cast <char const*> (
                        item.getObject ().getKey ()), m_keyBytes),
                    hyperleveldb::Slice (reinterpret_cast <char const*> (
                        item.getObject ().getData ()), item.getObject ().getSize ()));
            }
        }

        hyperleveldb::WriteOptions const options;

        m_db->Write (options, &wb).ok ();
    }

    void visitAll (VisitCallback& callback)
    {
        hyperleveldb::ReadOptions const options;

        ScopedPointer <hyperleveldb::Iterator> it (m_db->NewIterator (options));

        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == m_keyBytes)
            {
                DecodedBlob decoded (it->key ().data (),
                                                it->value ().data (),
                                                it->value ().size ());

                if (decoded.wasOk ())
                {
                    NodeObject::Ptr object (decoded.createObject ());

                    callback.visitObject (object);
                }
                else
                {
                    // Uh oh, corrupted data!
                    WriteLog (lsFATAL, NodeObject) << "Corrupt NodeObject #" << uint256::fromVoid (it->key ().data ());
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                WriteLog (lsFATAL, NodeObject) << "Bad key size = " << it->key ().size ();
            }
        }
    }

    int getWriteLoad ()
    {
        return m_batch.getWriteLoad ();
    }

    //--------------------------------------------------------------------------

    void writeBatch (Batch const& batch)
    {
        storeBatch (batch);
    }

private:
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    BatchWriter m_batch;
    StringPool m_stringPool;
    EncodedBlob::Pool m_blobPool;
    std::string m_name;
    ScopedPointer <hyperleveldb::DB> m_db;
};

//------------------------------------------------------------------------------

HyperDBFactory::HyperDBFactory ()
{
}

HyperDBFactory::~HyperDBFactory ()
{
}

HyperDBFactory* HyperDBFactory::getInstance ()
{
    return new HyperDBFactory;
}

String HyperDBFactory::getName () const
{
    return "HyperLevelDB";
}

Backend* HyperDBFactory::createInstance (
    size_t keyBytes,
    Parameters const& keyValues,
    Scheduler& scheduler,
    Journal journal)
{
    return new HyperDBFactory::BackendImp (keyBytes, keyValues, scheduler);
}

}

#endif
