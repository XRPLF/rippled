//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef BEAST_UTILITY_ATOMIC_SHARED_PTR_INCLUDED
#define BEAST_UTILITY_ATOMIC_SHARED_PTR_INCLUDED

// Temporarily shim in support for atomic<shared_ptr<T>>

#ifndef __cpp_lib_atomic_shared_ptr

namespace std {

template <class T>
struct atomic<shared_ptr<T>>
{
private:
    shared_ptr<T> p_;

public:
    constexpr atomic() noexcept = default;
    atomic(const atomic&) = delete;
    void
    operator=(const atomic&) = delete;

    constexpr atomic(nullptr_t) noexcept;
    atomic(shared_ptr<T> desired) noexcept;

    using value_type = shared_ptr<T>;

    static constexpr bool is_always_lock_free = false;
    bool
    is_lock_free() const noexcept;

    shared_ptr<T>
    load(memory_order order = memory_order_seq_cst) const noexcept;
    operator shared_ptr<T>() const noexcept;
    void
    store(
        shared_ptr<T> desired,
        memory_order order = memory_order_seq_cst) noexcept;
    void
    operator=(shared_ptr<T> desired) noexcept;

    shared_ptr<T>
    exchange(
        shared_ptr<T> desired,
        memory_order order = memory_order_seq_cst) noexcept;
    bool
    compare_exchange_weak(
        shared_ptr<T>& expected,
        shared_ptr<T> desired,
        memory_order order = memory_order_seq_cst) noexcept;
    bool
    compare_exchange_weak(
        shared_ptr<T>& expected,
        shared_ptr<T> desired,
        memory_order success,
        memory_order failure) noexcept;
    bool
    compare_exchange_strong(
        shared_ptr<T>& expected,
        shared_ptr<T> desired,
        memory_order order = memory_order_seq_cst) noexcept;
    bool
    compare_exchange_strong(
        shared_ptr<T>& expected,
        shared_ptr<T> desired,
        memory_order success,
        memory_order failure) noexcept;

    // Not supported:
    //     void wait(shared_ptr<T> old,
    //               memory_order order = memory_order_seq_cst) const noexcept;
    //     void notify_one() noexcept;
    //     void notify_all() noexcept;

private:
    static constexpr memory_order
    transform(memory_order order) noexcept;
};

template <class T>
inline constexpr atomic<shared_ptr<T>>::atomic(nullptr_t) noexcept : atomic()
{
}

template <class T>
inline atomic<shared_ptr<T>>::atomic(shared_ptr<T> desired) noexcept
    : p_{desired}
{
}

template <class T>
inline bool
atomic<shared_ptr<T>>::is_lock_free() const noexcept
{
    return atomic_is_lock_free(&p_);
}

template <class T>
inline shared_ptr<T>
atomic<shared_ptr<T>>::load(memory_order order) const noexcept
{
    return atomic_load_explicit(&p_, order);
}

template <class T>
inline atomic<shared_ptr<T>>::operator shared_ptr<T>() const noexcept
{
    return load();
}

template <class T>
inline void
atomic<shared_ptr<T>>::store(shared_ptr<T> desired, memory_order order) noexcept
{
    atomic_store_explicit(&p_, std::move(desired), order);
}

template <class T>
inline void
atomic<shared_ptr<T>>::operator=(shared_ptr<T> desired) noexcept
{
    store(std::move(desired));
}

template <class T>
inline shared_ptr<T>
atomic<shared_ptr<T>>::exchange(
    shared_ptr<T> desired,
    memory_order order) noexcept
{
    return atomic_exchange_explicit(&p_, std::move(desired), order);
}

template <class T>
inline constexpr memory_order
atomic<shared_ptr<T>>::transform(memory_order order) noexcept
{
    memory_order fail_order = order;
    if (fail_order == memory_order_acq_rel)
        order = memory_order_acquire;
    else if (fail_order == memory_order_release)
        order = memory_order_relaxed;
    return order;
}

template <class T>
inline bool
atomic<shared_ptr<T>>::compare_exchange_weak(
    shared_ptr<T>& expected,
    shared_ptr<T> desired,
    memory_order order) noexcept
{
    return compare_exchange_weak(
        expected, std::move(desired), order, transform(order));
}

template <class T>
inline bool
atomic<shared_ptr<T>>::compare_exchange_weak(
    shared_ptr<T>& expected,
    shared_ptr<T> desired,
    memory_order success,
    memory_order failure) noexcept
{
    return atomic_compare_exchange_weak_explicit(
        &p_, &expected, std::move(desired), success, failure);
}

template <class T>
inline bool
atomic<shared_ptr<T>>::compare_exchange_strong(
    shared_ptr<T>& expected,
    shared_ptr<T> desired,
    memory_order order) noexcept
{
    return compare_exchange_weak(
        expected, std::move(desired), order, transform(order));
}

template <class T>
inline bool
atomic<shared_ptr<T>>::compare_exchange_strong(
    shared_ptr<T>& expected,
    shared_ptr<T> desired,
    memory_order success,
    memory_order failure) noexcept
{
    return atomic_compare_exchange_strong_explicit(
        &p_, &expected, std::move(desired), success, failure);
}

}  // namespace std

#endif  // __cpp_lib_atomic_shared_ptr

#endif
