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

#include <ripple/app/main/DBInit.h>
#include <ripple/core/Config.h>
#include <ripple/core/SociDB.h>
#include <boost/filesystem/path.hpp>
#include <mutex>
#include <string>

namespace soci {
    class session;
}

namespace ripple {

template<class T, class TMutex>
class LockedPointer
{
public:
    using mutex = TMutex;
private:
    T* it_;
    std::unique_lock<mutex> lock_;

public:
    LockedPointer (T* it, mutex& m) : it_ (it), lock_ (m)
    {
    }
    LockedPointer (LockedPointer&& rhs) noexcept
        : it_ (rhs.it_), lock_ (std::move (rhs.lock_))
    {
    }
    LockedPointer () = delete;
    LockedPointer (LockedPointer const& rhs) = delete;
    LockedPointer& operator=(LockedPointer const& rhs) = delete;

    T* get ()
    {
        return it_;
    }
    T& operator*()
    {
        return *it_;
    }
    T* operator->()
    {
        return it_;
    }
    explicit operator bool() const
    {
        return bool (it_);
    }
};

using LockedSociSession = LockedPointer<soci::session, std::recursive_mutex>;

class DatabaseCon
{
public:
    struct Setup
    {
        explicit Setup() = default;

        Config::StartUpType startUp = Config::NORMAL;
        bool standAlone = false;
        boost::filesystem::path dataDir;
    };

    template<std::size_t N, std::size_t M>
    DatabaseCon(
        Setup const& setup,
        std::string const& DBName,
        std::array<char const*, N> const& pragma,
        std::array<char const*, M> const& initSQL)
    {
        // Use temporary files or regular DB files?
        auto const useTempFiles =
            setup.standAlone &&
            setup.startUp != Config::LOAD &&
            setup.startUp != Config::LOAD_FILE &&
            setup.startUp != Config::REPLAY;
        boost::filesystem::path pPath =
            useTempFiles ? "" : (setup.dataDir / DBName);

        open(session_, "sqlite", pPath.string());

        try
        {
            for (auto const& p : pragma)
            {
                soci::statement st = session_.prepare << p;
                st.execute(true);
            }
            for (auto const& sql : initSQL)
            {
                soci::statement st = session_.prepare << sql;
                st.execute(true);
            }
        }
        catch (soci::soci_error&)
        {
            // TODO: We should at least log this error. It is annoying to wire
            // a logger into every context, but there are other solutions.
        }
    }

    soci::session& getSession()
    {
        return session_;
    }

    LockedSociSession checkoutDb ()
    {
        return LockedSociSession (&session_, lock_);
    }

    void setupCheckpointing (JobQueue*, Logs&);

private:
    LockedSociSession::mutex lock_;

    soci::session session_;
    std::unique_ptr<Checkpointer> checkpointer_;
};

DatabaseCon::Setup
setup_DatabaseCon (Config const& c);

} // ripple

#endif
