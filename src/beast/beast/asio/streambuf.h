//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_ASIO_STREAMBUF_H_INCLUDED
#define BEAST_ASIO_STREAMBUF_H_INCLUDED

#include <boost/asio/buffer.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <cassert>
#include <memory>
#include <exception>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace asio {

/** Implements asio::streambuf interface using multiple buffers. */
template <class Allocator>
class basic_streambuf
{
public:
    using size_type = typename std::allocator_traits<Allocator>::size_type;
    using const_buffer = boost::asio::const_buffer;
    using mutable_buffer = boost::asio::mutable_buffer;

private:
    class element;

    using alloc_traits = std::allocator_traits<Allocator>;
    using list_type = typename boost::intrusive::make_list <element,
        boost::intrusive::constant_time_size <true>>::type;
    using iterator = typename list_type::iterator;
    using const_iterator = typename list_type::const_iterator;

    /*  These diagrams illustrate the layout and state variables.

        Input and output sequences are contained entirely in one element:

                             out_
        |<-----+----------+-------------+-------->|
        0    in_pos_   out_pos_      out_end_


        Output sequence is entirely contained in the second element:

                                                     out_
        |<------------+------------>|   |<----+----------+--------->|
        0           in_pos_                out_pos_   out_end_


        Output sequence occupies the second and third elements:

                                     out_
        |<-----+-------->|   |<-------+------>|   |<-----+--------->|
        0    in_pos_               out_pos_           out_end_


        Input sequence is empty:

                     out_
        |<------+------------------>|   |<-----------+------------->|
             out_pos_                             out_end_
              in_pos_


        Output sequence is empty:

                                                     out_
        |<------+------------------>|   |<------+------------------>|
              in_pos_                        out_pos_
                                             out_end_


        Normally if the output is empty but there is an element in out_,
        out_pos_ and out_end_ will be set to zero. Except after comitting
        everything, and causing the output sequence to start at the
        last element. In this case out_pos_ becomes out_->size(),
        and the result looks like this:

                                                  out_
        |<------+------------------>|   |<------------------------->|
              in_pos_                                           out_pos_  
                                                                out_end_
    */

    list_type list_;
    Allocator alloc_;
    size_type block_size_;
    size_type block_size_next_;
    size_type in_size_ = 0; // size of the input sequence
    iterator out_;          // element that contains out_pos_
    size_type in_pos_ = 0;  // input offset in list_.front()
    size_type out_pos_ = 0; // output offset in *out_
    size_type out_end_ = 0; // output end offset in list_.back()

public:
    class const_buffers_type;
    class mutable_buffers_type;

    basic_streambuf (basic_streambuf const& other) = delete;
    basic_streambuf& operator= (basic_streambuf const& other) = delete;
    basic_streambuf& operator= (basic_streambuf&& other) = delete;

    ~basic_streambuf();

    explicit
    basic_streambuf(std::size_t block_size = 16*1024,
        Allocator const& alloc = Allocator{});

    basic_streambuf (basic_streambuf&& other);

    /** Get the maximum size of the basic_streambuf. */
    size_type
    max_size() const
    {
        return std::numeric_limits<std::size_t>::max();
    }

    /** Get the size of the input sequence. */
    size_type
    size() const
    {
        return in_size_;
    }

    /** Get a list of buffers that represents the output sequence, with the given size. */
    mutable_buffers_type
    prepare (size_type n);

    /** Move bytes from the output sequence to the input sequence. */
    void
    commit (size_type n);

    /** Get a list of buffers that represents the input sequence. */
    const_buffers_type
    data() const;

    /** Remove bytes from the input sequence. */
    void
    consume (size_type n);

private:
    void
    debug_check() const;
};

//------------------------------------------------------------------------------

template <class Allocator>
class basic_streambuf<Allocator>::element
    : public boost::intrusive::list_base_hook <
        boost::intrusive::link_mode <boost::intrusive::normal_link>>
{
private:
    size_type const size_;  // size of the allocation minus sizeof(element)

public:
    element (element const&) = delete;
    element& operator= (element const&) = delete;

    explicit
    element (size_type block_size)
        : size_(block_size)
        { }

    size_type
    size() const
    {
        return size_;
    }

    size_type
    alloc_size() const
    {
        return size_ + sizeof(*this);
    }

    char*
    data()
    {
        return reinterpret_cast<char*>(this+1);
    }

    char const*
    data() const
    {
        return reinterpret_cast<char const*>(this+1);
    }
};

//------------------------------------------------------------------------------

template <class Allocator>
class basic_streambuf<Allocator>::const_buffers_type
{
public:
    using value_type = const_buffer;

private:
    struct transform
    {
        using argument_type = element;
        using result_type = value_type;

        const_buffers_type const& buffers_;

        explicit
        transform (const_buffers_type const& buffers)
            : buffers_(buffers)
            { }

        value_type
        operator() (element const& e) const;
    };

    typename list_type::const_iterator begin_;
    typename list_type::const_iterator end_;
    size_type in_pos_ = 0;
    size_type out_pos_ = 0;

public:
    using const_iterator = boost::transform_iterator<
        transform, typename list_type::const_iterator,
            value_type, value_type>;

    const_iterator
    begin() const
    {
        return const_iterator(begin_,transform(*this));
    }

    const_iterator
    end() const
    {
        return const_iterator(end_,transform(*this));
    }

private:
    friend class basic_streambuf;
    const_buffers_type (typename list_type::const_iterator first,
        typename list_type::const_iterator last, size_type in_pos,
            size_type out_pos);
};

template <class Allocator>
basic_streambuf<Allocator>::const_buffers_type::const_buffers_type (
    typename list_type::const_iterator first,
        typename list_type::const_iterator last, size_type in_pos,
            size_type out_pos)
    : begin_(first)
    , end_(last)
    , in_pos_(in_pos)
    , out_pos_(out_pos)
    { }

template <class Allocator>
auto
basic_streambuf<Allocator>::const_buffers_type::
    transform::operator() (element const& e) const ->
        value_type
{
    return value_type(e.data(),
        (&e == &*std::prev(buffers_.end_)) ? buffers_.out_pos_ : e.size()) +
        ((&e == &*buffers_.begin_) ? buffers_.in_pos_ : 0);
}

//------------------------------------------------------------------------------

template <class Allocator>
class basic_streambuf<Allocator>::mutable_buffers_type
{
public:
    using value_type = mutable_buffer;

private:
    struct transform
    {
        using argument_type = element;
        using result_type = value_type;

        mutable_buffers_type const& buffers_;

        explicit
        transform (mutable_buffers_type const& buffers)
            : buffers_(buffers)
        {
        }

        value_type
        operator() (element& e) const;
    };

    typename list_type::iterator begin_;
    typename list_type::iterator end_;
    size_type out_pos_ = 0;
    size_type out_end_ = 0;

public:
    using const_iterator = boost::transform_iterator<
        transform, typename list_type::iterator,
            value_type, value_type>;

    const_iterator
    begin() const
    {
        return const_iterator(begin_,transform(*this));
    }

    const_iterator
    end() const
    {
        return const_iterator(end_,transform(*this));
    }

private:
    friend class basic_streambuf;
    mutable_buffers_type (typename list_type::iterator first,
        typename list_type::iterator last, size_type out_pos,
            size_type out_end);
};

template <class Allocator>
basic_streambuf<Allocator>::mutable_buffers_type::mutable_buffers_type (
    typename list_type::iterator first,
        typename list_type::iterator last, size_type out_pos,
            size_type out_end)
    : begin_(first)
    , end_(last)
    , out_pos_(out_pos)
    , out_end_(out_end)
    { }

template <class Allocator>
auto
basic_streambuf<Allocator>::mutable_buffers_type::
    transform::operator() (element& e) const ->
        value_type
{
    return value_type(e.data(),
        (&e == &*std::prev(buffers_.end_)) ? buffers_.out_end_ : e.size()) +
        ((&e == &*buffers_.begin_) ? buffers_.out_pos_ : 0);
}

//------------------------------------------------------------------------------

template <class Allocator>
basic_streambuf<Allocator>::~basic_streambuf()
{
    for(auto iter = list_.begin(); iter != list_.end();)
    {
        auto& e = *iter++;
        size_type const n = e.alloc_size();
        e.~element();
        alloc_traits::deallocate(alloc_,
            reinterpret_cast<char*>(&e), n);
    }
}

template <class Allocator>
basic_streambuf<Allocator>::basic_streambuf(std::size_t block_size,
        Allocator const& alloc)
    : alloc_(alloc)
    , block_size_(block_size)
    , block_size_next_(block_size)
    , out_(list_.end())
{
    if (! (block_size > 0))
        throw std::invalid_argument(
            "basic_streambuf: invalid block_size");
}

template <class Allocator>
basic_streambuf<Allocator>::basic_streambuf (basic_streambuf&& other)
    : list_(std::move(other.list_))
    , alloc_(std::move(other.alloc_))
    , block_size_(other.block_size_)
    , block_size_next_(other.block_size_next_)
    , in_size_(other.in_size_)
    , out_(other.out_)
    , in_pos_(other.in_pos_)
    , out_pos_(other.out_pos_)
    , out_end_(other.out_end_)
{
    other.in_size_ = 0;
    other.out_ = other.list_.end();
    other.in_pos_ = 0;
    other.out_pos_ = 0;
    other.out_end_ = 0;
}

template <class Allocator>
auto
basic_streambuf<Allocator>::prepare (size_type n) ->
    mutable_buffers_type
{
    debug_check();

    iterator last = out_;

    if (last != list_.end())
    {
        size_type const avail = last->size() - out_pos_;
        if (n > avail)
        {
            n -= avail;
            while (++last != list_.end())
            {
                if (n <= last->size())
                {
                    ++last;
                    out_end_ = n;
                    n = 0;
                    debug_check();
                    break;
                }
                n -= last->size();
            }
        }
        else
        {
            ++last;
            out_end_ = out_pos_ + n;
            n = 0;
            debug_check();
        }
    }

    if (n > 0)
    {
        assert(last == list_.end());
        for(;;)
        {
            size_type const avail = block_size_next_;
            element& e = *reinterpret_cast<element*>(alloc_traits::allocate(
                alloc_, avail + sizeof(element)));
            ::new(&e) element(avail);
            list_.push_back(e);
            if (out_ == list_.end())
            {
                out_ = list_.iterator_to(e);
                debug_check();
            }
            if (n <= avail)
            {
                out_end_ = n;
                debug_check();
                n = 0;
                break;
            }
            n -= avail;
        }
        last = list_.end();
    }
    else
    {
        while (last != list_.end())
        {
            element& e = *last++;
            list_.erase(list_.iterator_to(e));
            size_type const len = e.alloc_size();
            e.~element();
            alloc_traits::deallocate(alloc_,
                reinterpret_cast<char*>(&e), len);
            // do we set out_ to list_.end() if empty?
        }
    }

    return mutable_buffers_type(out_, last, out_pos_, out_end_);
}

template <class Allocator>
void
basic_streambuf<Allocator>::commit (size_type n)
{
    debug_check();

    if (! list_.empty())
    {
        auto const last = std::prev(list_.end());
        while(out_ != last)
        {
            size_type const avail =
                out_->size() - out_pos_;
            if (n < avail)
            {
                in_size_ += n;
                out_pos_ += n;
                debug_check();
                return;
            }
            n -= avail;
            in_size_ += avail;
            out_pos_ = 0;
            ++out_;
            debug_check();
        }

        assert(out_ != list_.end());
        size_type const avail =
            out_end_ - out_pos_;
        if (n > avail)
            n = avail;
        // out_pos_ can become out_->size() here (*)
        in_size_ += n;
        out_pos_ += n;
        debug_check();
    }
}

template <class Allocator>
auto
basic_streambuf<Allocator>::data() const ->
    const_buffers_type
{
    debug_check();
    if (out_ == list_.end())
        return const_buffers_type(list_.begin(), list_.end(),
            in_pos_, out_pos_);
    return const_buffers_type(list_.begin(), std::next(out_),
        in_pos_, out_pos_);
}

template <class Allocator>
void
basic_streambuf<Allocator>::consume (size_type n)
{
    debug_check();
    if (out_ == list_.end())
        return;

    auto iter = list_.begin();
    for(;;)
    {
        if (iter != out_)
        {
            size_type const avail = iter->size() - in_pos_;
            if (n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
                debug_check();
                break;
            }
            n -= avail;
            in_size_ -= avail;
            in_pos_ = 0;
            debug_check();

            element& e = *iter++;
            list_.erase(list_.iterator_to(e));
            size_type const len = e.alloc_size();
            e.~element();
            alloc_traits::deallocate(alloc_,
                reinterpret_cast<char*>(&e), len);
        }
        else
        {
            size_type const avail = out_pos_ - in_pos_;
            if (n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
                debug_check();
            }
            else
            {
                auto const back = list_.iterator_to(list_.back());
                if (out_ != back && out_pos_ == out_->size())
                {
                    element& e = *out_++;
                    list_.erase(list_.iterator_to(e));
                    size_type const len = e.alloc_size();
                    e.~element();
                    alloc_traits::deallocate(alloc_,
                        reinterpret_cast<char*>(&e), len);
                    out_pos_ = 0;
                }
                else if (out_ == back && out_pos_ == out_end_)
                {
                    element& e = *out_++;
                    list_.erase(list_.iterator_to(e));
                    size_type const len = e.alloc_size();
                    e.~element();
                    alloc_traits::deallocate(alloc_,
                        reinterpret_cast<char*>(&e), len);
                    out_pos_ = 0;
                    out_end_ = 0;
                }
                in_size_ -= avail;
                in_pos_ = out_pos_;
                debug_check();
            }
            break;
        }
    }
}

template <class Allocator>
void
basic_streambuf<Allocator>::debug_check() const
{
#ifndef NDEBUG
    if (out_ == list_.end())
    {
        assert(in_size_ == 0);
        assert(in_pos_ == 0);
        assert(out_pos_ == 0);
        assert(out_end_ == 0);
        return;
    }

    assert(! list_.empty());

    auto const& out = *out_;
    auto const& back = list_.back();
    auto const& front = list_.front();

    assert(in_pos_ < front.size());
    assert(out_end_ <= back.size());

    assert(&out != &front || out_pos_ >= in_pos_);
    assert(&out != &back  || out_end_ <= back.size());
    assert(&out != &back  || out_pos_ <= back.size());
    assert(&out == &back  || out_pos_ <  back.size());
#endif
}

//------------------------------------------------------------------------------

using streambuf = basic_streambuf<std::allocator<char>>;

}
}

#endif
