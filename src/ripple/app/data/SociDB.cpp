//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/core/ConfigSections.h>
#include <ripple/app/data/SociDB.h>
#include <ripple/core/Config.h>
#include <beast/cxx14/memory.h>  // <memory>
#include <backends/sqlite3/soci-sqlite3.h>
#include <boost/filesystem.hpp>

namespace ripple {
namespace detail {

std::pair<std::string, soci::backend_factory const&>
getSociSqliteInit (std::string const& name,
                     std::string const& dir,
                     std::string const& ext)
{
    if (dir.empty () || name.empty ())
    {
        throw std::runtime_error (
            "Sqlite databases must specify a dir and a name. Name: " +
            name + " Dir: " + dir);
    }
    boost::filesystem::path file (dir);
    if (is_directory (file))
        file /= name + ext;
    return std::make_pair (file.string (), std::ref(soci::sqlite3));
}

std::pair<std::string, soci::backend_factory const&>
getSociInit (BasicConfig const& config,
             std::string const& dbName)
{
    auto const& section = config.section ("sqdb");
    std::string const backendName(get(section, "backend", "sqlite"));

    if (backendName == "sqlite")
    {
        std::string const path = config.legacy ("database_path");
        std::string const ext =
            (dbName == "validators" || dbName == "peerfinder") ? ".sqlite"
                                                               : ".db";
        return detail::getSociSqliteInit(dbName, path, ext);
    }
    else
    {
        throw std::runtime_error ("Unsupported soci backend: " + backendName);
    }
}
} // detail

SociConfig::SociConfig (std::pair<std::string, soci::backend_factory const&> init)
    : connectionString_ (std::move (init.first)),
      backendFactory_ (init.second)
{
}

SociConfig::SociConfig (BasicConfig const& config, std::string const& dbName)
    : SociConfig (detail::getSociInit (config, dbName))
{
}

std::string SociConfig::connectionString () const
{
    return connectionString_;
}

void SociConfig::open(soci::session& s) const
{
    s.open (backendFactory_, connectionString ());
}

void open(soci::session& s,
          BasicConfig const& config,
          std::string const& dbName)
{
    SociConfig c(config, dbName);
    c.open(s);
}

void open(soci::session& s,
          std::string const& beName,
          std::string const& connectionString)
{
    if (beName == "sqlite")
    {
        s.open(soci::sqlite3, connectionString);
        return;
    }
    else
    {
        throw std::runtime_error ("Unsupported soci backend: " + beName);
    }
}

size_t getKBUsedAll (soci::session& s)
{
    auto be = dynamic_cast<soci::sqlite3_session_backend*>(s.get_backend ());
    assert (be);  // Make sure the backend is sqlite
    return static_cast<int>(sqlite_api::sqlite3_memory_used () / 1024);
}

size_t getKBUsedDB (soci::session& s)
{
    // This function will have to be customized when other backends are added
    auto be = dynamic_cast<soci::sqlite3_session_backend*>(s.get_backend ());
    assert (be);
    int cur = 0, hiw = 0;
    sqlite_api::sqlite3_db_status (
        be->conn_, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiw, 0);
    return cur / 1024;
}

void convert(soci::blob& from, std::vector<std::uint8_t>& to)
{
    to.resize (from.get_len ());
    if (to.empty ())
        return;
    from.read (0, reinterpret_cast<char*>(&to[0]), from.get_len ());
}

void convert(soci::blob& from, std::string& to)
{
    std::vector<std::uint8_t> tmp;
    convert(from, tmp);
    to.assign(tmp.begin (), tmp.end());

}

void convert(std::vector<std::uint8_t> const& from, soci::blob& to)
{
    if (!from.empty ())
        to.write (0, reinterpret_cast<char const*>(&from[0]), from.size ());
}

int SqliteWALHook (void* s,
                   sqlite_api::sqlite3*,
                   const char* dbName,
                   int walSize)
{
    (reinterpret_cast<WALCheckpointer*>(s))->doHook (dbName, walSize);
    return SQLITE_OK;
}

WALCheckpointer::WALCheckpointer (std::shared_ptr<soci::session> const& s,
                                  JobQueue* q)
    : Thread ("sqlitedb"), session_(s)
{
    if (auto session =
            dynamic_cast<soci::sqlite3_session_backend*>(s->get_backend ()))
        conn_ = session->conn_;

    if (!conn_) return;
    startThread ();
    setupCheckpointing (q);
}

WALCheckpointer::~WALCheckpointer ()
{
    if (!conn_) return;
    stopThread ();
}

void WALCheckpointer::setupCheckpointing (JobQueue* q)
{
    if (!conn_) return;
    q_ = q;
    sqlite_api::sqlite3_wal_hook (conn_, SqliteWALHook, this);
}

void WALCheckpointer::doHook (const char* db, int pages)
{
    if (!conn_) return;

    if (pages < 1000)
        return;

    {
        ScopedLockType sl (mutex_);

        if (running_)
            return;

        running_ = true;
    }

    if (q_)
        q_->addJob (jtWAL,
                    std::string ("WAL"),
                    std::bind (&WALCheckpointer::runWal, this));
    else
        notify ();
}

void WALCheckpointer::run ()
{
    if (!conn_) return;

    // Simple thread loop runs Wal every time it wakes up via
    // the call to Thread::notify, unless Thread::threadShouldExit returns
    // true in which case we simply break.
    //
    for (;;)
    {
        wait ();
        if (threadShouldExit ())
            break;
        runWal ();
    }
}

void WALCheckpointer::runWal ()
{
    if (!conn_) return;

    int log = 0, ckpt = 0;
    int ret = sqlite3_wal_checkpoint_v2 (
        conn_, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

    if (ret != SQLITE_OK)
    {
        WriteLog ((ret == SQLITE_LOCKED) ? lsTRACE : lsWARNING, WALCheckpointer)
            << "WAL(" << sqlite3_db_filename (conn_, "main") << "): error "
            << ret;
    }
    else
        WriteLog (lsTRACE, WALCheckpointer)
            << "WAL(" << sqlite3_db_filename (conn_, "main")
            << "): frames=" << log << ", written=" << ckpt;

    {
        ScopedLockType sl (mutex_);
        running_ = false;
    }
}
}
