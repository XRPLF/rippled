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
#define RIPPLE_SERVER_IO_LIST_H_INCLUDED

#include <boost/container/flat_map.hpp>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ripple {

/** Manages a set of objects performing asynchronous I/O. */
class io_list final
{
public:
    class work
    {
        template <class = void>
        void
        destroy();

        friend class io_list;
        io_list* ios_ = nullptr;

    public:
        virtual ~work()
        {
            destroy();
        }

        /** Return the io_list associated with the work.

            Requirements:
                The call to io_list::emplace to
                create the work has already returned.
        */
        io_list&
        ios()
        {
            return *ios_;
        }

        virtual void
        close() = 0;
    };

private:
    template <class = void>
    void
    destroy();

    std::mutex m_;
    std::size_t n_ = 0;
    bool closed_ = false;
    std::condition_variable cv_;
    boost::container::flat_map<work*, std::weak_ptr<work>> map_;
    std::function<void(void)> f_;

public:
    io_list() = default;

    /** Destroy the list.

        Effects:
            Closes the io_list if it was not previously
                closed. No finisher is invoked in this case.

            Blocks until all work is destroyed.
    */
    ~io_list()
    {
        destroy();
    }

    /** Return `true` if the list is closed.

        Thread Safety:
            Undefined result if called concurrently
            with close().
    */
    bool
    closed() const
    {
        return closed_;
    }

    /** Create associated work if not closed.

        Requirements:
            `std::is_base_of_v<work, T> == true`

        Thread Safety:
            May be called concurrently.

        Effects:
            Atomically creates, inserts, and returns new
            work T, or returns nullptr if the io_list is
            closed,

        If the call succeeds and returns a new object,
        it is guaranteed that a subsequent call to close
        will invoke work::close on the object.

    */
    template <class T, class... Args>
    std::shared_ptr<T>
    emplace(Args&&... args);

    /** Cancel active I/O.

        Thread Safety:
            May not be called concurrently.

        Effects:
            Associated work is closed.

            Finisher if provided, will be called when
            all associated work is destroyed. The finisher
            may be called from a foreign thread, or within
            the call to this function.

            Only the first call to close will set the
            finisher.

            No effect after the first call.
    */
    template <class Finisher>
    void
    close(Finisher&& f);

    void
    close()
    {
        close([] {});
    }

    /** Block until the io_list stops.

        Effects:
            The caller is blocked until the io_list is
            closed and all associated work is destroyed.

        Thread safety:
            May be called concurrently.

        Preconditions:
            No call to io_service::run on any io_service
            used by work objects associated with this io_list
            exists in the caller's call stack.
    */
    template <class = void>
    void
    join();
};

//------------------------------------------------------------------------------

template <class>
void
io_list::work::destroy()
{
    if (!ios_)
        return;
    std::function<void(void)> f;
    {
        std::lock_guard lock(ios_->m_);
        ios_->map_.erase(this);
        if (--ios_->n_ == 0 && ios_->closed_)
        {
            std::swap(f, ios_->f_);
            ios_->cv_.notify_all();
        }
    }
    if (f)
        f();
}

template <class>
void
io_list::destroy()
{
    close();
    join();
}

template <class T, class... Args>
std::shared_ptr<T>
io_list::emplace(Args&&... args)
{
    static_assert(
        std::is_base_of<work, T>::value, "T must derive from io_list::work");
    if (closed_)
        return nullptr;
    auto sp = std::make_shared<T>(std::forward<Args>(args)...);
    decltype(sp) dead;

    std::lock_guard lock(m_);
    if (!closed_)
    {
        ++n_;
        sp->work::ios_ = this;
        map_.emplace(sp.get(), sp);
    }
    else
    {
        std::swap(sp, dead);
    }
    return sp;
}

template <class Finisher>
void
io_list::close(Finisher&& f)
{
    std::unique_lock<std::mutex> lock(m_);
    if (closed_)
        return;
    closed_ = true;
    auto map = std::move(map_);
    if (!map.empty())
    {
        f_ = std::forward<Finisher>(f);
        lock.unlock();
        for (auto const& p : map)
            if (auto sp = p.second.lock())
                sp->close();
    }
    else
    {
        lock.unlock();
        f();
    }
}

template <class>
void
io_list::join()
{
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [&] { return closed_ && n_ == 0; });
}

}  // namespace ripple

#endif
