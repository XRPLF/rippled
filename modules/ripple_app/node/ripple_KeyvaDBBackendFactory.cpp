//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class KeyvaDBBackendFactory::Backend : public NodeStore::Backend
{
public:
    Backend (size_t keyBytes, StringPairArray const& keyValues)
        : m_keyBytes (keyBytes)
        , m_path (keyValues ["path"])
        , m_db (KeyvaDB::New (
                    keyBytes,
                    File::getCurrentWorkingDirectory().getChildFile (m_path).withFileExtension ("key"),
                    File::getCurrentWorkingDirectory().getChildFile (m_path).withFileExtension ("val"),
                    false))
    {
    }

    ~Backend ()
    {
    }

    std::string getDataBaseName ()
    {
        return m_path.toStdString ();
    }

    //--------------------------------------------------------------------------

    Status get (void const* key, GetCallback* callback)
    {
        Status status (ok);

        struct ForwardingGetCallback : KeyvaDB::GetCallback
        {
            ForwardingGetCallback (Backend::GetCallback* callback)
                : m_callback (callback)
            {
            }

            void* getStorageForValue (int valueBytes)
            {
                return m_callback->getStorageForValue (valueBytes);
            }

        private:
            Backend::GetCallback* const m_callback;
        };

        ForwardingGetCallback cb (callback);

        // VFALCO TODO Can't we get KeyvaDB to provide a proper status?
        //
        bool const found = m_db->get (key, &cb);

        if (found)
        {
            status = ok;
        }
        else
        {
            status = notFound;
        }

        return status;
    }

    //--------------------------------------------------------------------------

    void writeObject (NodeObject::ref object)
    {
        Blob blob (toBlob (object));
        m_db->put (object->getHash ().begin (), &blob [0], blob.size ());
    }

    bool bulkStore (std::vector <NodeObject::pointer> const& objs)
    {
        for (size_t i = 0; i < objs.size (); ++i)
        {
            writeObject (objs [i]);
        }

        return true;
    }

    struct MyGetCallback : KeyvaDB::GetCallback
    {
        int valueBytes;
        HeapBlock <char> data;

        void* getStorageForValue (int valueBytes_)
        {
            valueBytes = valueBytes_;

            data.malloc (valueBytes);

            return data.getData ();
        }
    };

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        NodeObject::pointer result;

        MyGetCallback cb;

        bool const found = m_db->get (hash.begin (), &cb);

        if (found)
        {
            result = fromBinary (hash, cb.data.getData (), cb.valueBytes);
        }

        return result;
    }

    void visitAll (FUNCTION_TYPE<void (NodeObject::pointer)> func)
    {
        bassertfalse;
    }

    Blob toBlob (NodeObject::ref obj)
    {
        Blob rawData (9 + obj->getData ().size ());
        unsigned char* bufPtr = &rawData.front();

        *reinterpret_cast<uint32*> (bufPtr + 0) = ntohl (obj->getIndex ());
        *reinterpret_cast<uint32*> (bufPtr + 4) = ntohl (obj->getIndex ());
        * (bufPtr + 8) = static_cast<unsigned char> (obj->getType ());
        memcpy (bufPtr + 9, &obj->getData ().front (), obj->getData ().size ());

        return rawData;
    }

    NodeObject::pointer fromBinary (uint256 const& hash, char const* data, int size)
    {
        if (size < 9)
            throw std::runtime_error ("undersized object");

        uint32 index = htonl (*reinterpret_cast <const uint32*> (data));
        
        int htype = data[8];

        return boost::make_shared <NodeObject> (static_cast<NodeObjectType> (htype), index,
            data + 9, size - 9, hash);
    }

private:
    size_t const m_keyBytes;
    String m_path;
    ScopedPointer <KeyvaDB> m_db;
};

//------------------------------------------------------------------------------

KeyvaDBBackendFactory::KeyvaDBBackendFactory ()
{
}

KeyvaDBBackendFactory::~KeyvaDBBackendFactory ()
{
}

KeyvaDBBackendFactory& KeyvaDBBackendFactory::getInstance ()
{
    static KeyvaDBBackendFactory instance;

    return instance;
}

String KeyvaDBBackendFactory::getName () const
{
    return "KeyvaDB";
}

NodeStore::Backend* KeyvaDBBackendFactory::createInstance (size_t keyBytes, StringPairArray const& keyValues)
{
    return new KeyvaDBBackendFactory::Backend (keyBytes, keyValues);
}

//------------------------------------------------------------------------------

