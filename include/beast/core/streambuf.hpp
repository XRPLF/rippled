//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STREAMBUF_HPP
#define BEAST_STREAMBUF_HPP

#include <beast/config.hpp>
#include <beast/core/detail/empty_base_optimization.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive/list.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

namespace beast {

/** A @b `DynamicBuffer` that uses multiple buffers internally.

    The implementation uses a sequence of one or more character arrays
    of varying sizes. Additional character array objects are appended to
    the sequence to accommodate changes in the size of the character
    sequence.

    @note Meets the requirements of @b DynamicBuffer.

    @tparam Allocator The allocator to use for managing memory.
*/
template<class Allocator>
class basic_streambuf
#if ! GENERATING_DOCS
    : private detail::empty_base_optimization<
        typename std::allocator_traits<Allocator>::
            template rebind_alloc<char>>
#endif
{
public:
#if GENERATING_DOCS
    /// The type of allocator used.
    using allocator_type = Allocator;
#else
    using allocator_type = typename
        std::allocator_traits<Allocator>::
            template rebind_alloc<char>;
#endif

private:
    // Storage for the list of buffers representing the input
    // and output sequences. The allocation for each element
    // contains `element` followed by raw storage bytes.
    class element;

    using alloc_traits = std::allocator_traits<allocator_type>;
    using list_type = typename boost::intrusive::make_list<element,
        boost::intrusive::constant_time_size<true>>::type;
    using iterator = typename list_type::iterator;
    using const_iterator = typename list_type::const_iterator;

    using size_type = typename std::allocator_traits<Allocator>::size_type;
    using const_buffer = boost::asio::const_buffer;
    using mutable_buffer = boost::asio::mutable_buffer;

    static_assert(std::is_base_of<std::bidirectional_iterator_tag,
        typename std::iterator_traits<iterator>::iterator_category>::value,
            "BidirectionalIterator requirements not met");

    static_assert(std::is_base_of<std::bidirectional_iterator_tag,
        typename std::iterator_traits<const_iterator>::iterator_category>::value,
            "BidirectionalIterator requirements not met");

    list_type list_;        // list of allocated buffers
    iterator out_;          // element that contains out_pos_
    size_type alloc_size_;  // min amount to allocate
    size_type in_size_ = 0; // size of the input sequence
    size_type in_pos_ = 0;  // input offset in list_.front()
    size_type out_pos_ = 0; // output offset in *out_
    size_type out_end_ = 0; // output end offset in list_.back()

public:
#if GENERATING_DOCS
    /// The type used to represent the input sequence as a list of buffers.
    using const_buffers_type = implementation_defined;

    /// The type used to represent the output sequence as a list of buffers.
    using mutable_buffers_type = implementation_defined;

#else
    class const_buffers_type;

    class mutable_buffers_type;

#endif

    /// Destructor.
    ~basic_streambuf();

    /** Move constructor.

        The new object will have the input sequence of
        the other stream buffer, and an empty output sequence.

        @note After the move, the moved-from object will have
        an empty input and output sequence, with no internal
        buffers allocated.
    */
    basic_streambuf(basic_streambuf&&);

    /** Move constructor.

        The new object will have the input sequence of
        the other stream buffer, and an empty output sequence.

        @note After the move, the moved-from object will have
        an empty input and output sequence, with no internal
        buffers allocated.

        @param alloc The allocator to associate with the
        stream buffer.
    */
    basic_streambuf(basic_streambuf&&,
        allocator_type const& alloc);

    /** Move assignment.

        This object will have the input sequence of
        the other stream buffer, and an empty output sequence.

        @note After the move, the moved-from object will have
        an empty input and output sequence, with no internal
        buffers allocated.
    */
    basic_streambuf&
    operator=(basic_streambuf&&);

    /** Copy constructor.

        This object will have a copy of the other stream
        buffer's input sequence, and an empty output sequence.
    */
    basic_streambuf(basic_streambuf const&);

    /** Copy constructor.

        This object will have a copy of the other stream
        buffer's input sequence, and an empty output sequence.

        @param alloc The allocator to associate with the
        stream buffer.
    */
    basic_streambuf(basic_streambuf const&,
        allocator_type const& alloc);

    /** Copy assignment.

        This object will have a copy of the other stream
        buffer's input sequence, and an empty output sequence.
    */
    basic_streambuf& operator=(basic_streambuf const&);

    /** Copy constructor.

        This object will have a copy of the other stream
        buffer's input sequence, and an empty output sequence.
    */
    template<class OtherAlloc>
    basic_streambuf(basic_streambuf<OtherAlloc> const&);

    /** Copy constructor.

        This object will have a copy of the other stream
        buffer's input sequence, and an empty output sequence.

        @param alloc The allocator to associate with the
        stream buffer.
    */
    template<class OtherAlloc>
    basic_streambuf(basic_streambuf<OtherAlloc> const&,
        allocator_type const& alloc);

    /** Copy assignment.

        This object will have a copy of the other stream
        buffer's input sequence, and an empty output sequence.
    */
    template<class OtherAlloc>
    basic_streambuf& operator=(basic_streambuf<OtherAlloc> const&);

    /** Construct a stream buffer.

        @param alloc_size The size of buffer to allocate. This is a
        soft limit, calls to prepare for buffers exceeding this size
        will allocate the larger size. The default allocation size
        is 1KB (1024 bytes).

        @param alloc The allocator to use. If this parameter is
        unspecified, a default constructed allocator will be used.
    */
    explicit
    basic_streambuf(std::size_t alloc_size = 1024,
        Allocator const& alloc = allocator_type{});

    /// Returns a copy of the associated allocator.
    allocator_type
    get_allocator() const
    {
        return this->member();
    }

    /** Returns the default allocation size.

        This is the smallest size that the stream buffer will allocate.
        The size of the allocation can influence capacity, which will
        affect algorithms that use capacity to efficiently read from
        streams.
    */
    std::size_t
    alloc_size() const
    {
        return alloc_size_;
    }

    /** Set the default allocation size.

        This is the smallest size that the stream buffer will allocate.
        The size of the allocation can influence capacity, which will
        affect algorithms that use capacity to efficiently read from
        streams.

        @note This will not affect any already-existing allocations.

        @param n The number of bytes.
    */
    void
    alloc_size(std::size_t n)
    {
        alloc_size_ = n;
    }

    /// Returns the size of the input sequence.
    size_type
    size() const
    {
        return in_size_;
    }

    /// Returns the permitted maximum sum of the sizes of the input and output sequence.
    size_type
    max_size() const
    {
        return (std::numeric_limits<std::size_t>::max)();
    }

    /// Returns the maximum sum of the sizes of the input sequence and output sequence the buffer can hold without requiring reallocation.
    std::size_t
    capacity() const;

    /** Get a list of buffers that represents the input sequence.

        @note These buffers remain valid across subsequent calls to `prepare`.
    */
    const_buffers_type
    data() const;

    /** Get a list of buffers that represents the output sequence, with the given size.

        @note Buffers representing the input sequence acquired prior to
        this call remain valid.
    */
    mutable_buffers_type
    prepare(size_type n);

    /** Move bytes from the output sequence to the input sequence.

        @note Buffers representing the input sequence acquired prior to
        this call remain valid.
    */
    void
    commit(size_type n);

    /// Remove bytes from the input sequence.
    void
    consume(size_type n);

    // Helper for boost::asio::read_until
    template<class OtherAllocator>
    friend
    std::size_t
    read_size_helper(basic_streambuf<
        OtherAllocator> const& streambuf, std::size_t max_size);

private:
    void
    clear();

    void
    move_assign(basic_streambuf& other, std::false_type);

    void
    move_assign(basic_streambuf& other, std::true_type);

    void
    copy_assign(basic_streambuf const& other, std::false_type);

    void
    copy_assign(basic_streambuf const& other, std::true_type);

    void
    delete_list();

    void
    debug_check() const;
};

/** A @b `DynamicBuffer` that uses multiple buffers internally.

    The implementation uses a sequence of one or more character arrays
    of varying sizes. Additional character array objects are appended to
    the sequence to accommodate changes in the size of the character
    sequence.

    @note Meets the requirements of @b `DynamicBuffer`.
*/
using streambuf = basic_streambuf<std::allocator<char>>;

/** Format output to a @ref basic_streambuf.

    @param streambuf The @ref basic_streambuf to write to.

    @param t The object to write.

    @return A reference to the @ref basic_streambuf.
*/
template<class Allocator, class T>
basic_streambuf<Allocator>&
operator<<(basic_streambuf<Allocator>& streambuf, T const& t);

} // beast

#include <beast/core/impl/streambuf.ipp>

#endif
