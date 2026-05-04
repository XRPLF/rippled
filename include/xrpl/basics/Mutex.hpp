/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2024, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.
*/

#pragma once

#include <mutex>
#include <type_traits>

namespace xrpl {

template <typename ProtectedDataType, typename MutexType>
class Mutex;

/**
 * @brief A lock on a mutex that provides access to the protected data.
 *
 * @tparam ProtectedDataType data type to hold
 * @tparam LockType type of lock
 * @tparam MutexType type of mutex
 */
template <typename ProtectedDataType, template <typename...> typename LockType, typename MutexType>
class Lock
{
    LockType<MutexType> lock_;
    ProtectedDataType& data_;

public:
    /** @cond */
    ProtectedDataType const&
    operator*() const
    {
        return data_;
    }

    ProtectedDataType&
    operator*()
    {
        return data_;
    }

    [[nodiscard]] ProtectedDataType const&
    get() const
    {
        return data_;
    }

    ProtectedDataType&
    get()
    {
        return data_;
    }

    ProtectedDataType const*
    operator->() const
    {
        return &data_;
    }

    ProtectedDataType*
    operator->()
    {
        return &data_;
    }

    operator LockType<MutexType>&() &
    {
        return lock_;
    }

    operator LockType<MutexType> const&() const&
    {
        return lock_;
    }
    /** @endcond */

private:
    friend class Mutex<std::remove_const_t<ProtectedDataType>, MutexType>;

    Lock(MutexType& mutex, ProtectedDataType& data) : lock_(mutex), data_(data)
    {
    }
};

/**
 * @brief A container for data that is protected by a mutex. Inspired by Mutex in Rust.
 *
 * @tparam ProtectedDataType data type to hold
 * @tparam MutexType type of mutex
 */
template <typename ProtectedDataType, typename MutexType = std::mutex>
class Mutex
{
    mutable MutexType mutex_;
    ProtectedDataType data_{};

public:
    Mutex() = default;

    /**
     * @brief Construct a new Mutex object with the given data
     *
     * @param data The data to protect
     */
    explicit Mutex(ProtectedDataType data) : data_(std::move(data))
    {
    }

    /**
     * @brief Make a new Mutex object with the given data
     *
     * @tparam Args The types of the arguments to forward to the constructor of the protected data
     * @param args The arguments to forward to the constructor of the protected data
     * @return The Mutex object that protects the given data
     */
    template <typename... Args>
    static Mutex
    make(Args&&... args)
    {
        return Mutex{ProtectedDataType{std::forward<Args>(args)...}};
    }

    /**
     * @brief Lock the mutex and get a lock object allowing access to the protected data
     *
     * @tparam LockType The type of lock to use
     * @return A lock on the mutex and a reference to the protected data
     */
    template <template <typename...> typename LockType = std::scoped_lock>
    Lock<ProtectedDataType const, LockType, MutexType>
    lock() const
    {
        return {mutex_, data_};
    }

    /**
     * @brief Lock the mutex and get a lock object allowing access to the protected data
     *
     * @tparam LockType The type of lock to use
     * @return A lock on the mutex and a reference to the protected data
     */
    template <template <typename...> typename LockType = std::scoped_lock>
    Lock<ProtectedDataType, LockType, MutexType>
    lock()
    {
        return {mutex_, data_};
    }
};

}  // namespace xrpl
