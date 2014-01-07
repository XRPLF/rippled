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

namespace NodeStore
{

class KeyvaDBFactory::BackendImp : public Backend
{
private:
    typedef RecycledObjectPool <MemoryBlock> MemoryPool;
    typedef RecycledObjectPool <EncodedBlob> EncodedBlobPool;

public:
    BackendImp (size_t keyBytes,
             Parameters const& keyValues,
             Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_path (keyValues ["path"])
        , m_db (KeyvaDB::New (
                    keyBytes,
                    3,
                    File::getCurrentWorkingDirectory().getChildFile (m_path).withFileExtension ("key"),
                    File::getCurrentWorkingDirectory().getChildFile (m_path).withFileExtension ("val")))
    {
    }

    ~BackendImp ()
    {
    }

    std::string getName ()
    {
        return m_path.toStdString ();
    }

    //--------------------------------------------------------------------------

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        struct Callback : KeyvaDB::GetCallback
        {
            explicit Callback (MemoryBlock& block)
                : m_block (block)
            {
            }

            void* getStorageForValue (int valueBytes)
            {
                m_size = valueBytes;
                m_block.ensureSize (valueBytes);

                return m_block.getData ();
            }

            void const* getData () const noexcept
            {
                return m_block.getData ();
            }

            size_t getSize () const noexcept
            {
                return m_size;
            }

        private:
            MemoryBlock& m_block;
            size_t m_size;
        };

        MemoryPool::ScopedItem item (m_memoryPool);
        MemoryBlock& block (item.getObject ());

        Callback cb (block);

        // VFALCO TODO Can't we get KeyvaDB to provide a proper status?
        //
        bool const found = m_db->get (key, &cb);

        if (found)
        {
            DecodedBlob decoded (key, cb.getData (), cb.getSize ());

            if (decoded.wasOk ())
            {
                *pObject = decoded.createObject ();

                status = ok;
            }
            else
            {
                status = dataCorrupt;
            }
        }
        else
        {
            status = notFound;
        }

        return status;
    }

    void store (NodeObject::ref object)
    {
        EncodedBlobPool::ScopedItem item (m_blobPool);
        EncodedBlob& encoded (item.getObject ());

        encoded.prepare (object);

        m_db->put (encoded.getKey (), encoded.getData (), encoded.getSize ());
    }

    void storeBatch (Batch const& batch)
    {
        for (std::size_t i = 0; i < batch.size (); ++i)
            store (batch [i]);
    }

    void visitAll (VisitCallback& callback)
    {
        // VFALCO TODO Implement this!
        //
        bassertfalse;
        //m_db->visitAll ();
    }

    int getWriteLoad ()
    {
        // we dont do pending writes
        return 0;
    }

    //--------------------------------------------------------------------------

private:
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    String m_path;
    ScopedPointer <KeyvaDB> m_db;
    MemoryPool m_memoryPool;
    EncodedBlobPool m_blobPool;
};

//------------------------------------------------------------------------------

KeyvaDBFactory::KeyvaDBFactory ()
{
}

KeyvaDBFactory::~KeyvaDBFactory ()
{
}

KeyvaDBFactory* KeyvaDBFactory::getInstance ()
{
    return new KeyvaDBFactory;
}

String KeyvaDBFactory::getName () const
{
    return "KeyvaDB";
}

Backend* KeyvaDBFactory::createInstance (
    size_t keyBytes,
    Parameters const& keyValues,
    Scheduler& scheduler,
    Journal journal)
{
    return new KeyvaDBFactory::BackendImp (keyBytes, keyValues, scheduler);
}

}
