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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#endif

#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <boost/filesystem.hpp>
#include <memory>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

static auto checkpointPageCount = 1000;

namespace detail {

std::pair<std::string, soci::backend_factory const&>
getSociSqliteInit(
    std::string const& name,
    std::string const& dir,
    std::string const& ext)
{
    if (name.empty())
    {
        Throw<std::runtime_error>(
            "Sqlite databases must specify a dir and a name. Name: " + name +
            " Dir: " + dir);
    }
    boost::filesystem::path file(dir);
    if (is_directory(file))
        file /= name + ext;
    return std::make_pair(file.string(), std::ref(soci::sqlite3));
}

std::pair<std::string, soci::backend_factory const&>
getSociInit(BasicConfig const& config, std::string const& dbName)
{
    auto const& section = config.section("sqdb");
    auto const backendName = get(section, "backend", "sqlite");

    if (backendName != "sqlite")
        Throw<std::runtime_error>("Unsupported soci backend: " + backendName);

    auto const path = config.legacy("database_path");
    auto const ext =
        dbName == "validators" || dbName == "peerfinder" ? ".sqlite" : ".db";
    return detail::getSociSqliteInit(dbName, path, ext);
}

}  // namespace detail

SociConfig::SociConfig(
    std::pair<std::string, soci::backend_factory const&> init)
    : connectionString_(std::move(init.first)), backendFactory_(init.second)
{
}

SociConfig::SociConfig(BasicConfig const& config, std::string const& dbName)
    : SociConfig(detail::getSociInit(config, dbName))
{
}

std::string
SociConfig::connectionString() const
{
    return connectionString_;
}

void
SociConfig::open(soci::session& s) const
{
    s.open(backendFactory_, connectionString());
}

void
open(soci::session& s, BasicConfig const& config, std::string const& dbName)
{
    SociConfig(config, dbName).open(s);
}

void
open(
    soci::session& s,
    std::string const& beName,
    std::string const& connectionString)
{
    if (beName == "sqlite")
        s.open(soci::sqlite3, connectionString);
    else
        Throw<std::runtime_error>("Unsupported soci backend: " + beName);
}

static sqlite_api::sqlite3*
getConnection(soci::session& s)
{
    sqlite_api::sqlite3* result = nullptr;
    auto be = s.get_backend();
    if (auto b = dynamic_cast<soci::sqlite3_session_backend*>(be))
        result = b->conn_;

    if (!result)
        Throw<std::logic_error>("Didn't get a database connection.");

    return result;
}

size_t
getKBUsedAll(soci::session& s)
{
    if (!getConnection(s))
        Throw<std::logic_error>("No connection found.");
    return static_cast<size_t>(
        sqlite_api::sqlite3_memory_used() / kilobytes(1));
}

size_t
getKBUsedDB(soci::session& s)
{
    // This function will have to be customized when other backends are added
    if (auto conn = getConnection(s))
    {
        int cur = 0, hiw = 0;
        sqlite_api::sqlite3_db_status(
            conn, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiw, 0);
        return cur / kilobytes(1);
    }
    Throw<std::logic_error>("");
    return 0;  // Silence compiler warning.
}

void
convert(soci::blob& from, std::vector<std::uint8_t>& to)
{
    to.resize(from.get_len());
    if (to.empty())
        return;
    from.read(0, reinterpret_cast<char*>(&to[0]), from.get_len());
}

void
convert(soci::blob& from, std::string& to)
{
    std::vector<std::uint8_t> tmp;
    convert(from, tmp);
    to.assign(tmp.begin(), tmp.end());
}

void
convert(std::vector<std::uint8_t> const& from, soci::blob& to)
{
    if (!from.empty())
        to.write(0, reinterpret_cast<char const*>(&from[0]), from.size());
    else
        to.trim(0);
}

void
convert(std::string const& from, soci::blob& to)
{
    if (!from.empty())
        to.write(0, from.data(), from.size());
    else
        to.trim(0);
}

namespace {

/** Run a thread to checkpoint the write ahead log (wal) for
    the given soci::session every 1000 pages. This is only implemented
    for sqlite databases.

    Note: According to: https://www.sqlite.org/wal.html#ckpt this
    is the default behavior of sqlite. We may be able to remove this
    class.
*/

class WALCheckpointer : public Checkpointer
{
public:
    WALCheckpointer(
        std::uintptr_t id,
        std::weak_ptr<soci::session> session,
        JobQueue& q,
        Logs& logs)
        : id_(id)
        , session_(std::move(session))
        , jobQueue_(q)
        , j_(logs.journal("WALCheckpointer"))
    {
        if (auto [conn, keepAlive] = getConnection(); conn)
        {
            (void)keepAlive;
            sqlite_api::sqlite3_wal_hook(
                conn, &sqliteWALHook, reinterpret_cast<void*>(id_));
        }
    }

    std::pair<sqlite_api::sqlite3*, std::shared_ptr<soci::session>>
    getConnection() const
    {
        if (auto p = session_.lock())
        {
            return {ripple::getConnection(*p), p};
        }
        return {nullptr, std::shared_ptr<soci::session>{}};
    }

    std::uintptr_t
    id() const override
    {
        return id_;
    }

    ~WALCheckpointer() override = default;

    void
    schedule() override
    {
        {
            std::lock_guard lock(mutex_);
            if (running_)
                return;
            running_ = true;
        }

        // If the Job is not added to the JobQueue then we're not running_.
        if (!jobQueue_.addJob(
                jtWAL,
                "WAL",
                // If the owning DatabaseCon is destroyed, no need to checkpoint
                // or keep the checkpointer alive so use a weak_ptr to this.
                // There is a separate check in `checkpoint` for a valid
                // connection in the rare case when the DatabaseCon is destroyed
                // after locking this weak_ptr
                [wp = std::weak_ptr<Checkpointer>{shared_from_this()}](Job&) {
                    if (auto self = wp.lock())
                        self->checkpoint();
                }))
        {
            std::lock_guard lock(mutex_);
            running_ = false;
        }
    }

    void
    checkpoint() override
    {
        auto [conn, keepAlive] = getConnection();
        (void)keepAlive;
        if (!conn)
            return;

        int log = 0, ckpt = 0;
        int ret = sqlite3_wal_checkpoint_v2(
            conn, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

        auto fname = sqlite3_db_filename(conn, "main");
        if (ret != SQLITE_OK)
        {
            auto jm = (ret == SQLITE_LOCKED) ? j_.trace() : j_.warn();
            JLOG(jm) << "WAL(" << fname << "): error " << ret;
        }
        else
        {
            JLOG(j_.trace()) << "WAL(" << fname << "): frames=" << log
                             << ", written=" << ckpt;
        }

        std::lock_guard lock(mutex_);
        running_ = false;
    }

protected:
    std::uintptr_t const id_;
    // session is owned by the DatabaseCon parent that holds the checkpointer.
    // It is possible (tho rare) for the DatabaseCon class to be destoryed
    // before the checkpointer.
    std::weak_ptr<soci::session> session_;
    std::mutex mutex_;
    JobQueue& jobQueue_;

    bool running_ = false;
    beast::Journal const j_;

    static int
    sqliteWALHook(
        void* cpId,
        sqlite_api::sqlite3* conn,
        const char* dbName,
        int walSize)
    {
        if (walSize >= checkpointPageCount)
        {
            if (auto checkpointer =
                    checkpointerFromId(reinterpret_cast<std::uintptr_t>(cpId)))
            {
                checkpointer->schedule();
            }
            else
            {
                sqlite_api::sqlite3_wal_hook(conn, nullptr, nullptr);
            }
        }
        return SQLITE_OK;
    }
};

}  // namespace

std::shared_ptr<Checkpointer>
makeCheckpointer(
    std::uintptr_t id,
    std::weak_ptr<soci::session> session,
    JobQueue& queue,
    Logs& logs)
{
    return std::make_shared<WALCheckpointer>(
        id, std::move(session), queue, logs);
}

}  // namespace ripple

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
