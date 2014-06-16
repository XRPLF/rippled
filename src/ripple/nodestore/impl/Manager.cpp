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

#include <ripple/nodestore/Manager.h>

namespace ripple {
namespace NodeStore {

class ManagerImp : public Manager
{
public:
    typedef std::vector <std::unique_ptr <Factory>> List;
    List m_list;

    explicit ManagerImp (std::vector <std::unique_ptr <Factory>>&& factories)
        : m_list (std::move (factories))
    {
        add_known_factories ();
    }

    ~ManagerImp ()
    {
    }
    
    void
    add_factory (std::unique_ptr <Factory> factory)
    {
        m_list.emplace_back (std::move (factory));
    }

    void
    add_known_factories ()
    {
        // This is part of the ripple_app module since it has dependencies
        //addFactory (make_SqliteFactory ());

        add_factory (make_LevelDBFactory ());

        add_factory (make_MemoryFactory ());
        add_factory (make_NullFactory ());

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        add_factory (make_HyperDBFactory ());
    #endif

    #if RIPPLE_ROCKSDB_AVAILABLE
        add_factory (make_RocksDBFactory ());
    #endif
    }

    Factory*
    find (std::string const& name) const
    {
        for (List::const_iterator iter (m_list.begin ());
            iter != m_list.end (); ++iter)
            if ((*iter)->getName().compareIgnoreCase (name) == 0)
                return iter->get();
        return nullptr;
    }

    static void
    missing_backend ()
    {
        throw std::runtime_error (
            "Your rippled.cfg is missing a [node_db] entry, "
            "please see the rippled-example.cfg file!"
            );
    }

    std::unique_ptr <Backend>
    make_Backend (
        Parameters const& parameters,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        std::unique_ptr <Backend> backend;

        std::string const type (parameters ["type"].toStdString ());

        if (! type.empty ())
        {
            Factory* const factory (find (type));

            if (factory != nullptr)
            {
                backend = factory->createInstance (
                    NodeObject::keyBytes, parameters, scheduler, journal);
            }
            else
            {
                missing_backend ();
            }
        }
        else
        {
            missing_backend ();
        }

        return backend;
    }

    std::unique_ptr <Database>
    make_Database (
        std::string const& name,
        Scheduler& scheduler,
        beast::Journal journal,
        int readThreads,
        Parameters const& backendParameters,
        Parameters fastBackendParameters)
    {
        std::unique_ptr <Backend> backend (make_Backend (
            backendParameters, scheduler, journal));

        std::unique_ptr <Backend> fastBackend (
            (fastBackendParameters.size () > 0)
                ? make_Backend (fastBackendParameters, scheduler, journal)
                : nullptr);

        return std::make_unique <DatabaseImp> (name, scheduler, readThreads,
            std::move (backend), std::move (fastBackend), journal);
    }
};

//------------------------------------------------------------------------------

Manager::~Manager ()
{
}

std::unique_ptr <Manager>
make_Manager (std::vector <std::unique_ptr <Factory>> factories)
{
    return std::make_unique <ManagerImp> (std::move (factories));
}

}
}
