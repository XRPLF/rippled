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

#include <BeastConfig.h>
#include <ripple/app/data/DatabaseCon.h>
#include <ripple/app/data/SqliteDatabase.h>
#include <ripple/core/ConfigSections.h>

namespace ripple {

DatabaseCon::DatabaseCon (Setup const& setup,
        std::string const& strName,
        const char* initStrings[],
        int initCount)
{
    auto const useTempFiles  // Use temporary files or regular DB files?
        = setup.standAlone &&
          setup.startUp != Config::LOAD &&
          setup.startUp != Config::LOAD_FILE &&
          setup.startUp != Config::REPLAY;
    boost::filesystem::path pPath = useTempFiles
        ? "" : (setup.dataDir / strName);

    mDatabase = new SqliteDatabase (pPath.string ().c_str ());
    mDatabase->connect ();

    for (int i = 0; i < initCount; ++i)
        mDatabase->executeSQL (initStrings[i], true);
}

DatabaseCon::~DatabaseCon ()
{
    mDatabase->disconnect ();
    delete mDatabase;
}

DatabaseCon::Setup
setup_DatabaseCon (Config const& c)
{
    DatabaseCon::Setup setup;

    auto const& sec = c.section (ConfigSection::nodeDatabase ());
    get_if_exists (sec, "online_delete", setup.onlineDelete);
    setup.startUp = c.START_UP;
    setup.standAlone = c.RUN_STANDALONE;
    setup.dataDir = c.legacy ("database_path");

    return setup;
}

} // ripple
