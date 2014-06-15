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

#ifndef RIPPLE_NODESTORE_MANAGER_H_INCLUDED
#define RIPPLE_NODESTORE_MANAGER_H_INCLUDED

namespace ripple {
namespace NodeStore {

/** Singleton for managing NodeStore factories and back ends.
    Create a Manager like this:
    @code
    NodeStore::DummyScheduler scheduler;
    std::unique_ptr <NodeStore::Manager> m (NodeStore::make_Manager());

    std::unique_ptr <NodeStore::Database> nodeStore (m->make_Database ("nodeStore",
        scheduler, LogPartition::getJournal <NodeObject> (), 4,
        getConfig ().nodeDatabase));
    @endcode
*/
class Manager
{
public:
    virtual ~Manager () = 0;

    /** Add the specified factory to the manager.
        Thread safety:
            Not thread-safe.
    */
    virtual void add_factory (std::unique_ptr <Factory> factory) = 0;

    /** Return a pointer to the matching factory if it exists.
        @param  name The name to match, performed case-insensitive.
        @return `nullptr` if a match was not found.
    */
    virtual Factory* find (std::string const& name) const = 0;

    /** Create a backend. */
    virtual std::unique_ptr <Backend> make_Backend (Parameters const& parameters,
        Scheduler& scheduler, beast::Journal journal) = 0;

    /** Construct a NodeStore database.

        The parameters are key value pairs passed to the backend. The
        'type' key must exist, it defines the choice of backend. Most
        backends also require a 'path' field.
        
        Some choices for 'type' are:
            HyperLevelDB, LevelDBFactory, SQLite, MDB

        If the fastBackendParameter is omitted or empty, no ephemeral database
        is used. If the scheduler parameter is omited or unspecified, a
        synchronous scheduler is used which performs all tasks immediately on
        the caller's thread.

        @note If the database cannot be opened or created, an exception is thrown.

        @param name A diagnostic label for the database.
        @param scheduler The scheduler to use for performing asynchronous tasks.
        @param readThreads The number of async read threads to create
        @param backendParameters The parameter string for the persistent backend.
        @param fastBackendParameters [optional] The parameter string for the ephemeral backend.

        @return The opened database.
    */
    virtual std::unique_ptr <Database> make_Database (std::string const& name,
        Scheduler& scheduler, beast::Journal journal, int readThreads,
            Parameters const& backendParameters,
                Parameters fastBackendParameters = Parameters ()) = 0;
};

//------------------------------------------------------------------------------

/** Create a Manager.
    @param factories An optional array of additional factories to add.
*/
std::unique_ptr <Manager> make_Manager (
    std::vector <std::unique_ptr <Factory>> factories =
        std::vector <std::unique_ptr <Factory>>() );

}
}

#endif
