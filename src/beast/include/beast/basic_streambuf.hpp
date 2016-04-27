//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BASIC_STREAMBUF_HPP
#define BEAST_BASIC_STREAMBUF_HPP

#include <beast/detail/empty_base_optimization.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive/list.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

namespace beast {

/** A `Streambuf` that uses multiple buffers internally.

    The implementation uses a sequence of one or more character arrays
    of varying sizes. Additional character array objects are appended to
    the sequence to accommodate changes in the size of the character
    sequence.

    @tparam Allocator The allocator to use for managing memory.
*/
template<class Allocator>
class basic_streambuf
#if ! GENERATING_DOCS
    : private detail::empty_base_optimization<
        typename std::allocator_traits<Allocator>::
            template rebind_alloc<std::uint8_t>>
#endif
{
public:
    /// The type of allocator used.
    using allocator_type = typename
        std::allocator_traits<Allocator>::
            template rebind_alloc<std::uint8_t>;

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

        The output sequence of this object will be empty.

        After the move, the moved-from object will have an
        empty input and output sequence, with no internal
        buffers allocated.

        @param other The stream buffer to move from.
    */
    basic_streambuf(basic_streambuf&& other);

    /** Move constructor.

        The output sequence of this object will be empty.

        After the move, the moved-from object will have an
        empty input and output sequence, with no internal
        buffers allocated.

        @param other The stream buffer to move from.

        @param alloc The allocator to associate with the
        stream buffer.
    */
    basic_streambuf(basic_streambuf&& other,
        allocator_type const& alloc);

    /** Move assignment.

        The output sequence of this object will be empty.

        After the move, the moved-from object will have an
        empty input and output sequence, with no internal
        buffers allocated.

        @param other The stream buffer to move from.
    */
    basic_streambuf&
    operator=(basic_streambuf&& other);

    /// Copy constructor.
    basic_streambuf(basic_streambuf const& other);

    /** Copy constructor.

        The output sequence of this object will be empty.

        @param other The stream buffer to copy.

        @param alloc The allocator to associate with the
        stream buffer.
    */
    basic_streambuf(basic_streambuf const& other,
        allocator_type const& alloc);

    /** Copy assignment.

        The output sequence of this object will be empty.

        @param other The stream buffer to copy.
    */
    basic_streambuf& operator=(basic_streambuf const& other);

    /** Copy constructor.

        The output sequence of this object will be empty.

        @param other The stream buffer to copy.
    */
    template<class OtherAlloc>
    basic_streambuf(basic_streambuf<OtherAlloc> const& other);

    /** Copy constructor.

        The output sequence of this object will be empty.

        @param other The stream buffer to copy.

        @param alloc The allocator to associate with the
        stream buffer.
    */
    template<class OtherAlloc>
    basic_streambuf(basic_streambuf<OtherAlloc> const& other,
        allocator_type const& alloc);

    /** Copy assignment.

        The output sequence of this object will be empty.

        @param other The stream buffer to copy.
    */
    template<class OtherAlloc>
    basic_streambuf& operator=(basic_streambuf<OtherAlloc> const& other);

    /** Default constructor.

        @param alloc_size The size of buffer to allocate. This is a soft
        limit, calls to prepare for buffers exceeding this size will allocate
        the larger size.

        @param alloc The allocator to use.
    */
    explicit
    basic_streambuf(std::size_t alloc_size = 1024,
        Allocator const& alloc = allocator_type{});

    /// Get the associated allocator
    allocator_type
    get_allocator() const
    {
        return this->member();
    }

    /// Get the maximum size of the basic_streambuf.
    size_type
    max_size() const
    {
        return std::numeric_limits<std::size_t>::max();
    }

    /// Get the size of the input sequence.
    size_type
    size() const
    {
        return in_size_;
    }

    /// Get a list of buffers that represents the output sequence, with the given size.
    mutable_buffers_type
    prepare(size_type n);

    /// Move bytes from the output sequence to the input sequence.
    void
    commit(size_type n);

    /// Get a list of buffers that represents the input sequence.
    const_buffers_type
    data() const;

    /// Remove bytes from the input sequence.
    void
    consume(size_type n);

    /// Clear everything.
    void
    clear();

    // Helper for read_until
    template<class OtherAllocator>
    friend
    std::size_t
    read_size_helper(basic_streambuf<
        OtherAllocator> const& streambuf, std::size_t max_size);

private:
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

    std::size_t
    prepare_size() const;

    void
    debug_check() const;
};

/** Format output to a stream buffer.

    @param streambuf The streambuf to write to.

    @param t The object to write.

    @return The stream buffer.
*/
template<class Alloc, class T>
basic_streambuf<Alloc>&
operator<<(basic_streambuf<Alloc>& streambuf, T const& t);

/** Convert the entire basic_streambuf to a string.

    @param streambuf The streambuf to convert.

    @return A string representing the contents of the input sequence.
*/
template<class Allocator>
std::string
to_string(basic_streambuf<Allocator> const& streambuf);

} // beast

#include <beast/impl/basic_streambuf.ipp>

#endif
