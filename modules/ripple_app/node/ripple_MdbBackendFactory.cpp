//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#if RIPPLE_MDB_AVAILABLE

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

        if (error == 0) // Should use the size of the file plus the free space on the disk
       	    error = mdb_env_set_mapsize(m_env, 512L * 1024L * 1024L * 1024L);

        if (error == 0)
            error = mdb_env_open (
                        m_env,
                        keyValues ["path"].toStdString().c_str (),
                        MDB_NOTLS,
                        0664);

        MDB_txn * txn;
	if (error == 0)
            error = mdb_txn_begin(m_env, NULL, 0, &txn);
        if (error == 0)
            error = mdb_dbi_open(txn, NULL, 0, &m_dbi);
        if (error == 0)
            error = mdb_txn_commit(txn);


        if (error != 0)
        {
            String s;
            s << "Error #" << error << " creating mdb environment";
            throw std::runtime_error (s.toStdString ());
        }
        m_name = keyValues ["path"].toStdString();
    }

    ~Backend ()
    {
        if (m_env != nullptr)
        {
            mdb_dbi_close(m_env, m_dbi);
            mdb_env_close (m_env);
        }
    }

    std::string getDataBaseName()
    {
        return m_name;
    }

    bool bulkStore (std::vector <NodeObject::pointer> const& objs)
    {
        MDB_txn *txn = nullptr;
        int rc = 0;

        rc = mdb_txn_begin(m_env, NULL, 0, &txn);

        if (rc == 0)
        {
	    BOOST_FOREACH (NodeObject::ref obj, objs)
	    {
		MDB_val key, data;
		Blob blob (toBlob (obj));

		key.mv_size = (256 / 8);
		key.mv_data = const_cast<unsigned char *>(obj->getHash().begin());

		data.mv_size = blob.size();
		data.mv_data = &blob.front();

                rc = mdb_put(txn, m_dbi, &key, &data, 0);
                if (rc != 0)
                {
                    assert(false);
                    break;
                }
	    }
        }
        else
            assert(false);

        if (rc == 0)
            rc = mdb_txn_commit(txn);
        else if (txn)
            mdb_txn_abort(txn);

        assert(rc == 0);
        return rc == 0;
    }

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        NodeObject::pointer ret;

        MDB_txn *txn = nullptr;
        int rc = 0;

        rc = mdb_txn_begin(m_env, NULL, MDB_RDONLY, &txn);

        if (rc == 0)
        {
            MDB_val key, data;

	    key.mv_size = (256 / 8);
	    key.mv_data = const_cast<unsigned char *>(hash.begin());

	    rc = mdb_get(txn, m_dbi, &key, &data);
	    if (rc == 0)
	        ret = fromBinary(hash, static_cast<char *>(data.mv_data), data.mv_size);
	    else
	        assert(rc == MDB_NOTFOUND);
        }
        else
            assert(false);

        mdb_txn_abort(txn);

        return ret;
    }

    void visitAll (FUNCTION_TYPE <void (NodeObject::pointer)> func)
    { // WRITEME
        assert(false);
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
    std::string m_name;
    MDB_env*    m_env;
    MDB_dbi     m_dbi;
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

#endif
