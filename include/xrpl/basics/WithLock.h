#pragma once

#include <concepts>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace detail {

/**
 * Satisfied by types that meet the BasicLockable requirements.
 *
 * The requirements are partly semantic and we can only check for the
 * syntactic part.
 *
 * Note that we intentionally do not check that unlock is annotated
 * as noexcept; some relevant types do not annotate it as such even
 * though they will not throw (e.g. std::mutex on libstdc++) and so
 * would fail the concept. A throw from unlock() will terminate the
 * program from the destructor of std::lock_guard regardless.
 *
 * @see https://en.cppreference.com/w/cpp/named_req/BasicLockable
 */
template <typename T>
concept BasicLockable = requires(T& t) {
    t.lock();
    t.unlock();
};

}  // namespace detail

/**
 * Invoke a callable while holding a lock, returning its result by value.
 *
 * The lock is acquired before the callable is invoked and released when
 * this function returns, including by exception. The callable's return
 * value is materialized inside the locked region.
 *
 * Callables whose result type is a reference are rejected. If returning
 * a reference is intended, use withLockRef().
 *
 * The function is noexcept when the Lock's lock() operation and the
 * callable invocation are both noexcept.
 */
template <detail::BasicLockable Lock, typename Callable, typename... Args>
    requires std::invocable<Callable, Args...> &&
    (!std::is_reference_v<std::invoke_result_t<Callable, Args...>>)
[[nodiscard]] decltype(auto)
withLock(Lock& lock, Callable&& callable, Args&&... args) noexcept(
    noexcept(lock.lock()) && std::is_nothrow_invocable_v<Callable, Args...>)
{
    std::lock_guard guard(lock);
    return std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
}

/**
 * Invoke a callable while holding a lock, returning its result by reference.
 *
 * This is the opt-in counterpart to withLock() for callables whose result
 * type is a reference. The lock is acquired before the callable is invoked
 * and released when this function returns, including by exception. The
 * returned reference is handed back to the caller after the lock has been
 * released.
 *
 * @note It is the caller's responsibility to ensure that the returned
 *       reference remains valid after the lock is released and, where
 *       necessary, to synchronise access to it.
 *
 * Callables whose result type is not a reference are rejected; use
 * withLock() for those.
 *
 * The function is noexcept when the Lock's lock() operation and the
 * callable invocation are both noexcept.
 */
template <detail::BasicLockable Lock, typename Callable, typename... Args>
    requires std::invocable<Callable, Args...> &&
    std::is_reference_v<std::invoke_result_t<Callable, Args...>>
[[nodiscard]] decltype(auto)
withLockRef(Lock& lock, Callable&& callable, Args&&... args) noexcept(
    noexcept(lock.lock()) && std::is_nothrow_invocable_v<Callable, Args...>)
{
    std::lock_guard guard(lock);
    return std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
}

}  // namespace xrpl
