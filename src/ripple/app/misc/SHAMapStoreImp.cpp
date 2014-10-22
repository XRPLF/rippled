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

#include <ripple/app/misc/SHAMapStoreImp.h>
#include <beast/cxx14/memory.h> // <memory>

namespace ripple {

SHAMapStore::Setup
setup_SHAMapStore (Config const& c)
{
    SHAMapStore::Setup setup = {};

    if (c.nodeDatabase["online_delete"].isNotEmpty())
        setup.deleteInterval = c.nodeDatabase["online_delete"].getIntValue();
    if (c.nodeDatabase["advisory_delete"].isNotEmpty() && setup.deleteInterval)
        setup.advisoryDelete = c.nodeDatabase["advisory_delete"].getIntValue();
    setup.ledgerHistory = c.LEDGER_HISTORY;
    setup.nodeDatabase = c.nodeDatabase;
    setup.ephemeralNodeDatabase = c.ephemeralNodeDatabase;
    setup.databasePath = c.DATABASE_PATH;

    return setup;
}

// ApplicationImp initializer: make_SHAMapStore(setup_SHAMapStore(getConfig())
std::unique_ptr<SHAMapStore>
make_SHAMapStore (SHAMapStore::Setup const& s,
        beast::Stoppable& parent,
        NodeStore::Manager& manager,
        NodeStore::Scheduler& scheduler,
        beast::Journal journal,
        beast::Journal nodeStoreJournal)
{
    return std::make_unique<SHAMapStoreImp> (s, parent, manager, scheduler,
            journal, nodeStoreJournal);
}

}
