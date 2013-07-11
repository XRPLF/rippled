//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class MdbBackendFactory::Backend : public NodeStore::Backend
{
public:
    explicit Backend (StringPairArray const& keyValues)
        : m_env (nullptr)
    {
        if (keyValues ["path"].isEmpty ())
            throw std::runtime_error ("Missing path in MDB backend");

        int error = 0;

        error = mdb_env_create (&m_env);

        if (error == 0)
        {
            error = mdb_env_open (
                        m_env,
                        keyValues ["path"].toStdString().c_str (),
                        0,
                        0);

        }

        if (error != 0)
        {
            String s;
            s << "Error #" << error << " creating mdb environment";
            throw std::runtime_error (s.toStdString ());
        }
    }

    ~Backend ()
    {
        if (m_env != nullptr)
            mdb_env_close (m_env);
    }

    std::string getDataBaseName()
    {
        return std::string ();
    }

    bool store (NodeObject::ref obj)
    {
        return false;
    }

    bool bulkStore (std::vector <NodeObject::pointer> const& objs)
    {
        return false;
    }

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        return NodeObject::pointer ();
    }

    void visitAll (FUNCTION_TYPE <void (NodeObject::pointer)> func)
    {
    }

    Blob toBlob (NodeObject::ref obj) const
    {
        Blob rawData (9 + obj->getData ().size ());
        unsigned char* bufPtr = &rawData.front();

        *reinterpret_cast <uint32*> (bufPtr + 0) = ntohl (obj->getIndex ());

        *reinterpret_cast <uint32*> (bufPtr + 4) = ntohl (obj->getIndex ());

        *(bufPtr + 8) = static_cast <unsigned char> (obj->getType ());

        memcpy (bufPtr + 9, &obj->getData ().front (), obj->getData ().size ());

        return rawData;
    }

    NodeObject::pointer fromBinary (uint256 const& hash, char const* data, int size) const
    {
        if (size < 9)
            throw std::runtime_error ("undersized object");

        uint32 const index = htonl (*reinterpret_cast <uint32 const*> (data));

        int const htype = data [8];

        return boost::make_shared <NodeObject> (
                    static_cast <NodeObjectType> (htype),
                    index,
                    data + 9,
                    size - 9,
                    hash);
    }

private:
    MDB_env* m_env;
};

//------------------------------------------------------------------------------

MdbBackendFactory::MdbBackendFactory ()
{
}

MdbBackendFactory::~MdbBackendFactory ()
{
}

MdbBackendFactory& MdbBackendFactory::getInstance ()
{
    static MdbBackendFactory instance;

    return instance;
}

String MdbBackendFactory::getName () const
{
    return "mdb";
}

NodeStore::Backend* MdbBackendFactory::createInstance (StringPairArray const& keyValues)
{
    return new MdbBackendFactory::Backend (keyValues);
}

//------------------------------------------------------------------------------

