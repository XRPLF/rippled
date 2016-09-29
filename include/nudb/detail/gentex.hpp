//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_GENTEX_HPP
#define NUDB_DETAIL_GENTEX_HPP

#include <boost/assert.hpp>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <system_error>

namespace nudb {
namespace detail {

//  Generation counting mutex
//
template<class = void>
class gentex_t
{
private:
    std::mutex m_;
    std::size_t gen_ = 0;
    std::size_t cur_ = 0;
    std::size_t prev_ = 0;
    std::condition_variable cond_;

public:
    gentex_t() = default;
    gentex_t(gentex_t const&) = delete;
    gentex_t& operator=(gentex_t const&) = delete;

    void
    start();

    void
    finish();

    std::size_t
    lock_gen();

    void
    unlock_gen(std::size_t gen);
};

template<class _>
void
gentex_t<_>::
start()
{
    std::unique_lock<std::mutex> l{m_};
    prev_ += cur_;
    cur_ = 0;
    ++gen_;
}

template<class _>
void
gentex_t<_>::
finish()
{
    std::unique_lock<std::mutex> l{m_};
    while(prev_ > 0)
        cond_.wait(l);
}

template<class _>
std::size_t
gentex_t<_>::
lock_gen()
{
    std::lock_guard<
        std::mutex> l{m_};
    ++cur_;
    return gen_;
}

template<class _>
void
gentex_t<_>::
unlock_gen(std::size_t gen)
{
    std::unique_lock<std::mutex> l{m_};
    if(gen == gen_)
    {
        --cur_;
    }
    else
    {
        --prev_;
        if(prev_ == 0)
            cond_.notify_all();
    }
}

using gentex = gentex_t<>;

//------------------------------------------------------------------------------

template<class GenerationLockable>
class genlock
{
private:
    bool owned_ = false;
    GenerationLockable* g_ = nullptr;
    std::size_t gen_;

public:
    using mutex_type = GenerationLockable;

    genlock() = default;
    genlock(genlock const&) = delete;
    genlock& operator=(genlock const&) = delete;

    genlock(genlock&& other);

    genlock& operator=(genlock&& other);

    explicit
    genlock(mutex_type& g);

    genlock(mutex_type& g, std::defer_lock_t);

    ~genlock();

    mutex_type*
    mutex() noexcept
    {
        return g_;
    }

    bool
    owns_lock() const noexcept
    {
        return g_ && owned_;
    }

    explicit
    operator bool() const noexcept
    {
        return owns_lock();
    }

    void
    lock();

    void
    unlock();

    mutex_type*
    release() noexcept;

    template<class U>
    friend
    void
    swap(genlock<U>& lhs, genlock<U>& rhs) noexcept;
};

template<class G>
genlock<G>::
genlock(genlock&& other)
    : owned_(other.owned_)
    , g_(other.g_)
{
    other.owned_ = false;
    other.g_ = nullptr;
}

template<class G>
genlock<G>&
genlock<G>::
operator=(genlock&& other)
{
    if(owns_lock())
        unlock();
    owned_ = other.owned_;
    g_ = other.g_;
    other.owned_ = false;
    other.g_ = nullptr;
    return *this;
}

template<class G>
genlock<G>::
genlock(mutex_type& g)
    : g_(&g)
{
    lock();
}

template<class G>
genlock<G>::
genlock(mutex_type& g, std::defer_lock_t)
    : g_(&g)
{
}

template<class G>
genlock<G>::
~genlock()
{
    if(owns_lock())
        unlock();
}

template<class G>
void
genlock<G>::
lock()
{
    // no associated gentex
    BOOST_ASSERT(g_ != nullptr);
    // gentex is already owned
    BOOST_ASSERT(! owned_);
    gen_ = g_->lock_gen();
    owned_ = true;
}

template<class G>
void
genlock<G>::
unlock()
{
    // no associated gentex
    BOOST_ASSERT(g_ != nullptr);
    // gentex is not owned
    BOOST_ASSERT(owned_);
    g_->unlock_gen(gen_);
    owned_ = false;
}

template<class G>
auto
genlock<G>::
release() noexcept ->
    mutex_type*
{
    mutex_type* const g = g_;
    g_ = nullptr;
    return g;
}

template<class G>
void
swap(genlock<G>& lhs, genlock<G>& rhs) noexcept
{
    using namespace std;
    swap(lhs.owned_, rhs.owned_);
    swap(lhs.g_, rhs.g_);
}

} // detail
} // nudb

#endif
