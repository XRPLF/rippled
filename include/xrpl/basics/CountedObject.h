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

#ifndef RIPPLE_BASICS_COUNTEDOBJECT_H_INCLUDED
#define RIPPLE_BASICS_COUNTEDOBJECT_H_INCLUDED

#include <ripple/beast/type_name.h>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

/** Manages all counted object types. */
class CountedObjects
{
public:
    static CountedObjects&
    getInstance() noexcept;

    using Entry = std::pair<std::string, int>;
    using List = std::vector<Entry>;

    List
    getCounts(int minimumThreshold) const;

public:
    /** Implementation for @ref CountedObject.

        @internal
    */
    class Counter
    {
    public:
        Counter(std::string name) noexcept : name_(std::move(name)), count_(0)
        {
            // Insert ourselves at the front of the lock-free linked list
            CountedObjects& instance = CountedObjects::getInstance();
            Counter* head;

            do
            {
                head = instance.m_head.load();
                next_ = head;
            } while (instance.m_head.exchange(this) != head);

            ++instance.m_count;
        }

        ~Counter() noexcept = default;

        int
        increment() noexcept
        {
            return ++count_;
        }

        int
        decrement() noexcept
        {
            return --count_;
        }

        int
        getCount() const noexcept
        {
            return count_.load();
        }

        Counter*
        getNext() const noexcept
        {
            return next_;
        }

        std::string const&
        getName() const noexcept
        {
            return name_;
        }

    private:
        std::string const name_;
        std::atomic<int> count_;
        Counter* next_;
    };

private:
    CountedObjects() noexcept;
    ~CountedObjects() noexcept = default;

private:
    std::atomic<int> m_count;
    std::atomic<Counter*> m_head;
};

//------------------------------------------------------------------------------

/** Tracks the number of instances of an object.

    Derived classes have their instances counted automatically. This is used
    for reporting purposes.

    @ingroup ripple_basics
*/
template <class Object>
class CountedObject
{
private:
    static auto&
    getCounter() noexcept
    {
        static CountedObjects::Counter c{beast::type_name<Object>()};
        return c;
    }

public:
    CountedObject() noexcept
    {
        getCounter().increment();
    }

    CountedObject(CountedObject const&) noexcept
    {
        getCounter().increment();
    }

    CountedObject&
    operator=(CountedObject const&) noexcept = default;

    ~CountedObject() noexcept
    {
        getCounter().decrement();
    }
};

}  // namespace ripple

#endif
