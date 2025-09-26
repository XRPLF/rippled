//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef XRPL_BASICS_SHAREDWEAKCACHEPOINTER_H_INCLUDED
#define XRPL_BASICS_SHAREDWEAKCACHEPOINTER_H_INCLUDED

#include <memory>
#include <variant>

namespace ripple {

/** A combination of a std::shared_ptr and a std::weak_pointer.


This class is a wrapper to a `std::variant<std::shared_ptr,std::weak_ptr>`
This class is useful for storing intrusive pointers in tagged caches using less
memory than storing both pointers directly.
*/

template <class T>
class SharedWeakCachePointer
{
public:
    SharedWeakCachePointer() = default;

    SharedWeakCachePointer(SharedWeakCachePointer const& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakCachePointer(std::shared_ptr<TT> const& rhs);

    SharedWeakCachePointer(SharedWeakCachePointer&& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakCachePointer(std::shared_ptr<TT>&& rhs);

    SharedWeakCachePointer&
    operator=(SharedWeakCachePointer const& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakCachePointer&
    operator=(std::shared_ptr<TT> const& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakCachePointer&
    operator=(std::shared_ptr<TT>&& rhs);

    ~SharedWeakCachePointer();

    /** Return a strong pointer if this is already a strong pointer (i.e. don't
        lock the weak pointer. Use the `lock` method if that's what's needed)
     */
    std::shared_ptr<T> const&
    getStrong() const;

    /** Return true if this is a strong pointer and the strong pointer is
        seated.
     */
    explicit
    operator bool() const noexcept;

    /** Set the pointer to null, decrement the appropriate ref count, and run
        the appropriate release action.
     */
    void
    reset();

    /** If this is a strong pointer, return the raw pointer. Otherwise return
        null.
     */
    T*
    get() const;

    /** If this is a strong pointer, return the strong count. Otherwise return 0
     */
    std::size_t
    use_count() const;

    /** Return true if there is a non-zero strong count. */
    bool
    expired() const;

    /** If this is a strong pointer, return the strong pointer. Otherwise
        attempt to lock the weak pointer.
     */
    std::shared_ptr<T>
    lock() const;

    /** Return true is this represents a strong pointer. */
    bool
    isStrong() const;

    /** Return true is this represents a weak pointer. */
    bool
    isWeak() const;

    /** If this is a weak pointer, attempt to convert it to a strong pointer.

        @return true if successfully converted to a strong pointer (or was
                already a strong pointer). Otherwise false.
      */
    bool
    convertToStrong();

    /** If this is a strong pointer, attempt to convert it to a weak pointer.

        @return false if the pointer is null. Otherwise return true.
      */
    bool
    convertToWeak();

private:
    std::variant<std::shared_ptr<T>, std::weak_ptr<T>> combo_;
};
}  // namespace ripple
#endif
