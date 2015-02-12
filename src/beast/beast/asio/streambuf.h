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

#include <beast/utility/empty_base_optimization.h>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <algorithm>
#include <cassert>
#include <memory>
#include <exception>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <string>
#include <utility>

namespace beast {
namespace asio {

/** Implements asio::streambuf interface using multiple buffers. */
template <class Allocator>
class basic_streambuf
    : private empty_base_optimization<Allocator>
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

        Input and output contained entirely in one element:

        0                            out_
        |<-------------+------------------------------------------->|
      in_pos_       out_pos_                                     out_end_


        Output contained in first and second elements:

                     out_
        |<------+----------+------->|   |<----------+-------------->|
              in_pos_   out_pos_                 out_end_


        Output contained in the second element:

                                                     out_
        |<------------+------------>|   |<----+-------------------->|
                    in_pos_                out_pos_              out_end_


        Output contained in second and third elements:

                                     out_
        |<-----+-------->|   |<-------+------>|   |<--------------->|
             in_pos_               out_pos_                      out_end_


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


        The end of output can point to the end of an element.
        But out_pos_ should never point to the end:

                                                     out_
        |<------+------------------>|   |<------+------------------>|
              in_pos_                        out_pos_            out_end_


        When the input sequence entirely fills the last element and
        the output sequence is empty, out_ will point to the end of
        the list of buffers, and out_pos_ and out_end_ will be 0:

                                                     
        |<------+------------------>|   out_     == list_.end()
              in_pos_                   out_pos_ == 0
                                        out_end_ == 0
    */

    list_type list_;
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
    data() const
    {
        return const_cast<char*>(
            reinterpret_cast<char const*>(this+1));
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

        basic_streambuf const* streambuf_ = nullptr;

        transform() = default;

        explicit
        transform (basic_streambuf const& streambuf)
            : streambuf_ (&streambuf)
        {
        }

        value_type const
        operator() (element const& e) const;
    };

    basic_streambuf const* streambuf_ = nullptr;

public:
    using const_iterator = boost::transform_iterator<
        transform, typename list_type::const_iterator,
            value_type, value_type>;

    const_buffers_type() = default;
    const_buffers_type (const_buffers_type const&) = default;
    const_buffers_type& operator= (const_buffers_type const&) = default;

    const_iterator
    begin() const
    {
        return const_iterator (streambuf_->list_.begin(),
            transform(*streambuf_));
    }

    const_iterator
    end() const
    {
        return const_iterator (streambuf_->out_ ==
            streambuf_->list_.end() ? streambuf_->list_.end() :
                std::next(streambuf_->out_), transform(*streambuf_));
    }

private:
    friend class basic_streambuf;

    explicit
    const_buffers_type (basic_streambuf const& streambuf);
};

template <class Allocator>
basic_streambuf<Allocator>::const_buffers_type::const_buffers_type (
    basic_streambuf const& streambuf)
    : streambuf_ (&streambuf)
{
}

template <class Allocator>
auto
basic_streambuf<Allocator>::const_buffers_type::
    transform::operator() (element const& e) const ->
        value_type const
{
    return value_type (e.data(),
        (streambuf_->out_ == streambuf_->list_.end() ||
            &e != &*streambuf_->out_) ? e.size() : streambuf_->out_pos_) +
                (&e == &*streambuf_->list_.begin() ?
                    streambuf_->in_pos_ : 0);
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

        basic_streambuf const* streambuf_ = nullptr;

        transform() = default;

        explicit
        transform (basic_streambuf const& streambuf)
            : streambuf_ (&streambuf)
        {
        }

        value_type const
        operator() (element const& e) const;
    };

    basic_streambuf const* streambuf_;

public:
    using const_iterator = boost::transform_iterator<
        transform, typename list_type::const_iterator,
            value_type, value_type>;

    mutable_buffers_type() = default;
    mutable_buffers_type (mutable_buffers_type const&) = default;
    mutable_buffers_type& operator= (mutable_buffers_type const&) = default;

    const_iterator
    begin() const
    {
        return const_iterator (streambuf_->out_,
            transform(*streambuf_));
    }

    const_iterator
    end() const
    {
        return const_iterator (streambuf_->list_.end(),
            transform(*streambuf_));
    }

private:
    friend class basic_streambuf;
    mutable_buffers_type (basic_streambuf const& streambuf);
};

template <class Allocator>
basic_streambuf<Allocator>::mutable_buffers_type::mutable_buffers_type (
    basic_streambuf const& streambuf)
    : streambuf_ (&streambuf)
{
}

template <class Allocator>
auto
basic_streambuf<Allocator>::mutable_buffers_type::
    transform::operator() (element const& e) const ->
        value_type const
{
    return value_type (e.data(), &e == &*std::prev(streambuf_->list_.end()) ?
        streambuf_->out_end_ : e.size()) + (&e == &*streambuf_->out_ ?
            streambuf_->out_pos_ : 0);
}

//------------------------------------------------------------------------------

template <class Allocator>
basic_streambuf<Allocator>::~basic_streambuf()
{
    for(auto iter = list_.begin(); iter != list_.end();)
    {
        auto& e = *iter++;
        size_type const n = e.alloc_size();
        alloc_traits::destroy(this->member(), &e);
        alloc_traits::deallocate(this->member(),
            reinterpret_cast<char*>(&e), n);
    }
}

template <class Allocator>
basic_streambuf<Allocator>::basic_streambuf(std::size_t block_size,
        Allocator const& alloc)
    : empty_base_optimization<Allocator>(alloc)
    , block_size_ (block_size)
    , block_size_next_ (block_size)
    , out_ (list_.end())
{
    if (! (block_size > 0))
        throw std::invalid_argument(
            "basic_streambuf: invalid block_size");
}

template <class Allocator>
basic_streambuf<Allocator>::basic_streambuf (basic_streambuf&& other)
    : empty_base_optimization<Allocator>(other.member())
    , list_ (std::move(other.list_))
    , block_size_ (other.block_size_)
    , block_size_next_ (other.block_size_next_)
    , in_size_ (other.in_size_)
    , out_ (other.out_)
    , in_pos_ (other.in_pos_)
    , out_pos_ (other.out_pos_)
    , out_end_ (other.out_end_)
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
    iterator pos = out_;
    if (pos != list_.end())
    {
        auto const avail = pos->size() - out_pos_;
        if (n > avail)
        {
            n -= avail;
            while (++pos != list_.end())
            {
                if (n < pos->size())
                {
                    out_end_ = n;
                    n = 0;
                    ++pos;
                    break;
                }
                n -= pos->size();
            }
        }
        else
        {
            ++pos;
            out_end_ = out_pos_ + n;
            n = 0;
        }
        debug_check();
    }

    if (n > 0)
    {
        assert(pos == list_.end());
        for(;;)
        {
            auto const avail = block_size_next_;
            auto& e = *reinterpret_cast<element*>(alloc_traits::allocate(
                this->member(), avail + sizeof(element)));
            alloc_traits::construct(this->member(), &e, avail);
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
                break;
            }
            n -= avail;
        }
    }
    else
    {
        while (pos != list_.end())
        {
            auto& e = *pos++;
            list_.erase(list_.iterator_to(e));
            auto const len = e.alloc_size();
            alloc_traits::destroy(this->member(), &e);
            alloc_traits::deallocate(this->member(),
                reinterpret_cast<char*>(&e), len);
        }
        debug_check();
    }

    return mutable_buffers_type (*this);
}

template <class Allocator>
void
basic_streambuf<Allocator>::commit (size_type n)
{
    if (list_.empty())
        return;
    if (out_ == list_.end())
        return;
    auto const last = std::prev(list_.end());
    while (out_ != last)
    {
        auto const avail =
            out_->size() - out_pos_;
        if (n < avail)
        {
            out_pos_ += n;
            in_size_ += n;
            debug_check();
            return;
        }
        ++out_;
        n -= avail;
        out_pos_ = 0;
        in_size_ += avail;
        debug_check();
    }

    n = std::min (n, out_end_ - out_pos_);
    out_pos_ += n;
    in_size_ += n;
    if (out_pos_ == out_->size())
    {
        ++out_;
        out_pos_ = 0;
        out_end_ = 0;
    }
    debug_check();
}

template <class Allocator>
auto
basic_streambuf<Allocator>::data() const ->
    const_buffers_type
{
    return const_buffers_type(*this);
}

template <class Allocator>
void
basic_streambuf<Allocator>::consume (size_type n)
{
    if (list_.empty())
        return;

    auto pos = list_.begin();
    for(;;)
    {
        if (pos != out_)
        {
            auto const avail = pos->size() - in_pos_;
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

            element& e = *pos++;
            list_.erase(list_.iterator_to(e));
            size_type const len = e.alloc_size();
            alloc_traits::destroy(this->member(), &e);
            alloc_traits::deallocate(this->member(),
                reinterpret_cast<char*>(&e), len);
        }
        else
        {
            auto const avail = out_pos_ - in_pos_;
            if (n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
            }
            else
            {
                in_size_ -= avail;
                if (out_pos_ != out_end_||
                    out_ != list_.iterator_to(list_.back()))
                {
                    in_pos_ = out_pos_;
                }
                else
                {
                    // Use the whole buffer now.
                    // Alternatively we could deallocate it.
                    in_pos_ = 0;
                    out_pos_ = 0;
                    out_end_ = 0;
                }
            }
            debug_check();
            break;
        }
    }
}

template <class Allocator>
void
basic_streambuf<Allocator>::debug_check() const
{
#ifndef NDEBUG
    if (list_.empty())
    {
        assert(in_pos_ == 0);
        assert(in_size_ == 0);
        assert(out_pos_ == 0);
        assert(out_end_ == 0);
        assert(out_ == list_.end());
        return;
    }

    auto const& front = list_.front();

    assert(in_pos_ < front.size());

    if (out_ == list_.end())
    {
        assert(out_pos_ == 0);
        assert(out_end_ == 0);
    }
    else
    {
        auto const& out = *out_;
        auto const& back = list_.back();

        assert(out_end_ <= back.size());
        assert(out_pos_ <  out.size());
        assert(&out != &front || out_pos_ >= in_pos_);
        assert(&out != &front || out_pos_ - in_pos_ == in_size_);
        assert(&out != &back  || out_pos_ <= out_end_);
    }
#endif
}

template <class Alloc, class T>
basic_streambuf<Alloc>&
operator<< (basic_streambuf<Alloc>& buf, T const& t)
{
    std::stringstream ss;
    ss << t;
    auto const& s = ss.str();
    buf.commit(boost::asio::buffer_copy(
        buf.prepare(s.size()), boost::asio::buffer(s)));
    return buf;
}

//------------------------------------------------------------------------------

using streambuf = basic_streambuf<std::allocator<char>>;

/** Convert the entire basic_streambuf to a string.
    @note It is more efficient to deal directly in the streambuf instead.
*/
template <class Allocator>
std::string
to_string (basic_streambuf<Allocator> const& buf)
{
    std::string s;
    s.resize(buf.size());
    boost::asio::buffer_copy(boost::asio::buffer(
        &s[0], s.size()), buf.data());
    return s;
}

}
}

#endif
