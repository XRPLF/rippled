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

#ifndef RIPPLE_APP_DATA_DATABASECON_H_INCLUDED
#define RIPPLE_APP_DATA_DATABASECON_H_INCLUDED

#include <xrpld/app/main/DBInit.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/SociDB.h>
#include <xrpld/perflog/PerfLog.h>

#include <boost/filesystem/path.hpp>

#include <mutex>
#include <optional>
#include <string>

namespace soci {
class session;
}

namespace ripple {

class LockedSociSession
{
public:
    using mutex = std::recursive_mutex;

private:
    std::shared_ptr<soci::session> session_;
    std::unique_lock<mutex> lock_;

public:
    LockedSociSession(std::shared_ptr<soci::session> it, mutex& m)
        : session_(std::move(it)), lock_(m)
    {
    }
    LockedSociSession(LockedSociSession&& rhs) noexcept
        : session_(std::move(rhs.session_)), lock_(std::move(rhs.lock_))
    {
    }
    LockedSociSession() = delete;
    LockedSociSession(LockedSociSession const& rhs) = delete;
    LockedSociSession&
    operator=(LockedSociSession const& rhs) = delete;

    soci::session*
    get()
    {
        return session_.get();
    }
    soci::session&
    operator*()
    {
        return *session_;
    }
    soci::session*
    operator->()
    {
        return session_.get();
    }
    explicit
    operator bool() const
    {
        return bool(session_);
    }
};

class DatabaseCon
{
public:
    struct Setup
    {
        explicit Setup() = default;

        Config::StartUpType startUp = Config::NORMAL;
        bool standAlone = false;
        boost::filesystem::path dataDir;
        // Indicates whether or not to return the `globalPragma`
        // from commonPragma()
        bool useGlobalPragma = false;

        std::vector<std::string> const*
        commonPragma() const
        {
            XRPL_ASSERT(
                !useGlobalPragma || globalPragma,
                "ripple::DatabaseCon::Setup::commonPragma : consistent global "
                "pragma");
            return useGlobalPragma && globalPragma ? globalPragma.get()
                                                   : nullptr;
        }

        static std::unique_ptr<std::vector<std::string> const> globalPragma;
        std::array<std::string, 4> txPragma;
        std::array<std::string, 1> lgrPragma;
    };

    struct CheckpointerSetup
    {
        JobQueue* jobQueue;
        Logs* logs;
    };

    template <std::size_t N, std::size_t M>
    DatabaseCon(
        Setup const& setup,
        std::string const& dbName,
        std::array<std::string, N> const& pragma,
        std::array<char const*, M> const& initSQL,
        beast::Journal journal)
        // Use temporary files or regular DB files?
        : DatabaseCon(
              setup.standAlone && setup.startUp != Config::LOAD &&
                      setup.startUp != Config::LOAD_FILE &&
                      setup.startUp != Config::REPLAY
                  ? ""
                  : (setup.dataDir / dbName),
              setup.commonPragma(),
              pragma,
              initSQL,
              journal)
    {
    }

    // Use this constructor to setup checkpointing
    template <std::size_t N, std::size_t M>
    DatabaseCon(
        Setup const& setup,
        std::string const& dbName,
        std::array<std::string, N> const& pragma,
        std::array<char const*, M> const& initSQL,
        CheckpointerSetup const& checkpointerSetup,
        beast::Journal journal)
        : DatabaseCon(setup, dbName, pragma, initSQL, journal)
    {
        setupCheckpointing(checkpointerSetup.jobQueue, *checkpointerSetup.logs);
    }

    template <std::size_t N, std::size_t M>
    DatabaseCon(
        boost::filesystem::path const& dataDir,
        std::string const& dbName,
        std::array<std::string, N> const& pragma,
        std::array<char const*, M> const& initSQL,
        beast::Journal journal)
        : DatabaseCon(dataDir / dbName, nullptr, pragma, initSQL, journal)
    {
    }

    // Use this constructor to setup checkpointing
    template <std::size_t N, std::size_t M>
    DatabaseCon(
        boost::filesystem::path const& dataDir,
        std::string const& dbName,
        std::array<std::string, N> const& pragma,
        std::array<char const*, M> const& initSQL,
        CheckpointerSetup const& checkpointerSetup,
        beast::Journal journal)
        : DatabaseCon(dataDir, dbName, pragma, initSQL, journal)
    {
        setupCheckpointing(checkpointerSetup.jobQueue, *checkpointerSetup.logs);
    }

    ~DatabaseCon();

    soci::session&
    getSession()
    {
        return *session_;
    }

    LockedSociSession
    checkoutDb()
    {
        using namespace std::chrono_literals;
        LockedSociSession session = perf::measureDurationAndLog(
            [&]() { return LockedSociSession(session_, lock_); },
            "checkoutDb",
            10ms,
            j_);

        return session;
    }

private:
    void
    setupCheckpointing(JobQueue*, Logs&);

    template <std::size_t N, std::size_t M>
    DatabaseCon(
        boost::filesystem::path const& pPath,
        std::vector<std::string> const* commonPragma,
        std::array<std::string, N> const& pragma,
        std::array<char const*, M> const& initSQL,
        beast::Journal journal)
        : session_(std::make_shared<soci::session>()), j_(journal)
    {
        open(*session_, "sqlite", pPath.string());

        for (auto const& p : pragma)
        {
            soci::statement st = session_->prepare << p;
            st.execute(true);
        }

        if (commonPragma)
        {
            for (auto const& p : *commonPragma)
            {
                soci::statement st = session_->prepare << p;
                st.execute(true);
            }
        }

        for (auto const& sql : initSQL)
        {
            soci::statement st = session_->prepare << sql;
            st.execute(true);
        }
    }

    LockedSociSession::mutex lock_;

    // checkpointer may outlive the DatabaseCon when the checkpointer jobQueue
    // callback locks a weak pointer and the DatabaseCon is then destroyed. In
    // this case, the checkpointer needs to make sure it doesn't use an already
    // destroyed session. Thus this class keeps a shared_ptr to the session (so
    // the checkpointer can keep a weak_ptr) and the checkpointer is a
    // shared_ptr in this class. session_ will never be null.
    std::shared_ptr<soci::session> const session_;
    std::shared_ptr<Checkpointer> checkpointer_;

    beast::Journal const j_;
};

// Return the checkpointer from its id. If the checkpointer no longer exists, an
// nullptr is returned
std::shared_ptr<Checkpointer>
checkpointerFromId(std::uintptr_t id);

DatabaseCon::Setup
setup_DatabaseCon(
    Config const& c,
    std::optional<beast::Journal> j = std::nullopt);

}  // namespace ripple

#endif
