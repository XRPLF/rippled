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

#ifndef RIPPLE_SERVER_IO_LIST_H_INCLUDED

#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace ripple {

/** Manages a set of objects performing asycnhronous I/O. */
class io_list
{
public:
    class work
    {
        friend class io_list;
        io_list* list_;

    public:
        ~work()
        {
            list_->erase(*this);
        }

        virtual void close() = 0;
    };

private:
    std::mutex m_;
    std::size_t n_ = 0;
    bool closed_ = false;
    std::condition_variable cv_;
    std::unordered_map<work*,
        std::weak_ptr<work>> map_;

public:

    io_list() = default;

    ~io_list()
    {
    }

    /** Returns `true` if I/O has been canceled. */
    bool
    closed()
    {
        return closed_;
    }

    /** Insert a new active I/O object. */
    template <class T, class... Args>
    std::shared_ptr<T>
    emplace(Args&&... args)
    {
        static_assert(std::is_baseof<work, T>::value,
            "T must derive from io_list::work");
        auto sp = std::make_shared<T>(
            std::forward<Args>(args)...);
        sp->work::map_ = this;
        std::lock_guard<std::mutex> lock(m_);
        if(closed_)
            throw std::logic_error("io_list closed");
        ++n_;
        map_.emplace(sp.get(), sp);
        return sp;
    }

    /** Cancel active I/O. */
    void
    close()
    {
        decltype(map_) map;
        {
            std::lock_guard<std::mutex> lock(m_);
            if(closed_)
                return;
            closed_ = true;
            map = std::move(map_);
        }
        for(auto const& p : map)
            if(auto sp = p.second.lock())
                sp->close();
    }

    /** Block until all active I/O complete. */
    void
    join()
    {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&]{ return true; });
    }

private:
    void
    erase(work& w)
    {
        std::lock_guard<std::mutex> lock(m_);
        map_.erase(&w);
        if(--n_ == 0)
            cv_.notify_all();
    }
};

} // ripple

#endif
