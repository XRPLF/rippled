//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_STREAMBUF_IPP
#define BEAST_IMPL_STREAMBUF_IPP

#include <beast/core/detail/type_traits.hpp>
#include <beast/core/detail/write_dynabuf.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <exception>
#include <sstream>
#include <string>
#include <utility>

namespace beast {

/*  These diagrams illustrate the layout and state variables.

1   Input and output contained entirely in one element:

    0                            out_
    |<-------------+------------------------------------------->|
    in_pos_     out_pos_                                     out_end_


2   Output contained in first and second elements:

                    out_
    |<------+----------+------->|   |<----------+-------------->|
          in_pos_   out_pos_                 out_end_


3   Output contained in the second element:

                                                    out_
    |<------------+------------>|   |<----+-------------------->|
                in_pos_                out_pos_              out_end_


4   Output contained in second and third elements:

                                    out_
    |<-----+-------->|   |<-------+------>|   |<--------------->|
         in_pos_               out_pos_                      out_end_


5   Input sequence is empty:

                    out_
    |<------+------------------>|   |<-----------+------------->|
         out_pos_                             out_end_
          in_pos_


6   Output sequence is empty:

                                                    out_
    |<------+------------------>|   |<------+------------------>|
          in_pos_                        out_pos_
                                         out_end_


7   The end of output can point to the end of an element.
    But out_pos_ should never point to the end:

                                                    out_
    |<------+------------------>|   |<------+------------------>|
          in_pos_                        out_pos_            out_end_


8   When the input sequence entirely fills the last element and
    the output sequence is empty, out_ will point to the end of
    the list of buffers, and out_pos_ and out_end_ will be 0:


    |<------+------------------>|   out_     == list_.end()
          in_pos_                   out_pos_ == 0
                                    out_end_ == 0
*/

template<class Allocator>
class basic_streambuf<Allocator>::element
    : public boost::intrusive::list_base_hook<
        boost::intrusive::link_mode<
            boost::intrusive::normal_link>>
{
    using size_type = typename std::allocator_traits<Allocator>::size_type;

    size_type const size_;

public:
    element(element const&) = delete;
    element& operator=(element const&) = delete;

    explicit
    element(size_type n)
        : size_(n)
    {
    }

    size_type
    size() const
    {
        return size_;
    }

    char*
    data() const
    {
        return const_cast<char*>(
            reinterpret_cast<char const*>(this+1));
    }
};

template<class Allocator>
class basic_streambuf<Allocator>::const_buffers_type
{
    basic_streambuf const* sb_;

    friend class basic_streambuf;

    explicit
    const_buffers_type(basic_streambuf const& sb);

public:
    // Why?
    using value_type = boost::asio::const_buffer;

    class const_iterator;

    const_buffers_type() = delete;
    const_buffers_type(const_buffers_type const&) = default;
    const_buffers_type& operator=(const_buffers_type const&) = default;

    const_iterator
    begin() const;

    const_iterator
    end() const;
};

template<class Allocator>
class basic_streambuf<Allocator>::mutable_buffers_type
{
    basic_streambuf const* sb_;

    friend class basic_streambuf;

    explicit
    mutable_buffers_type(basic_streambuf const& sb);

public:
    using value_type = mutable_buffer;

    class const_iterator;

    mutable_buffers_type() = delete;
    mutable_buffers_type(mutable_buffers_type const&) = default;
    mutable_buffers_type& operator=(mutable_buffers_type const&) = default;

    const_iterator
    begin() const;

    const_iterator
    end() const;
};

//------------------------------------------------------------------------------

template<class Allocator>
class basic_streambuf<Allocator>::const_buffers_type::const_iterator
{
    basic_streambuf const* sb_ = nullptr;
    typename list_type::const_iterator it_;

public:
    using value_type =
        typename const_buffers_type::value_type;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other) = default;
    const_iterator(const_iterator const& other) = default;
    const_iterator& operator=(const_iterator&& other) = default;
    const_iterator& operator=(const_iterator const& other) = default;

    const_iterator(basic_streambuf const& sb,
            typename list_type::const_iterator const& it)
        : sb_(&sb)
        , it_(it)
    {
    }

    bool
    operator==(const_iterator const& other) const
    {
        return sb_ == other.sb_ && it_ == other.it_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        auto const& e = *it_;
        return value_type{e.data(),
           (sb_->out_ == sb_->list_.end() ||
                &e != &*sb_->out_) ? e.size() : sb_->out_pos_} +
                   (&e == &*sb_->list_.begin() ? sb_->in_pos_ : 0);
    }

    pointer
    operator->() const = delete;

    const_iterator&
    operator++()
    {
        ++it_;
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--()
    {
        --it_;
        return *this;
    }

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }
};

template<class Allocator>
basic_streambuf<Allocator>::const_buffers_type::const_buffers_type(
    basic_streambuf const& sb)
    : sb_(&sb)
{
}

template<class Allocator>
auto
basic_streambuf<Allocator>::const_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{*sb_, sb_->list_.begin()};
}

template<class Allocator>
auto
basic_streambuf<Allocator>::const_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{*sb_, sb_->out_ ==
        sb_->list_.end() ? sb_->list_.end() :
            std::next(sb_->out_)};
}

//------------------------------------------------------------------------------

template<class Allocator>
class basic_streambuf<Allocator>::mutable_buffers_type::const_iterator
{
    basic_streambuf const* sb_ = nullptr;
    typename list_type::const_iterator it_;

public:
    using value_type =
        typename mutable_buffers_type::value_type;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other) = default;
    const_iterator(const_iterator const& other) = default;
    const_iterator& operator=(const_iterator&& other) = default;
    const_iterator& operator=(const_iterator const& other) = default;

    const_iterator(basic_streambuf const& sb,
            typename list_type::const_iterator const& it)
        : sb_(&sb)
        , it_(it)
    {
    }

    bool
    operator==(const_iterator const& other) const
    {
        return sb_ == other.sb_ && it_ == other.it_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        auto const& e = *it_;
        return value_type{e.data(),
            &e == &*std::prev(sb_->list_.end()) ?
                sb_->out_end_ : e.size()} +
                   (&e == &*sb_->out_ ? sb_->out_pos_ : 0);
    }

    pointer
    operator->() const = delete;

    const_iterator&
    operator++()
    {
        ++it_;
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--()
    {
        --it_;
        return *this;
    }

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }
};

template<class Allocator>
basic_streambuf<Allocator>::mutable_buffers_type::mutable_buffers_type(
    basic_streambuf const& sb)
    : sb_(&sb)
{
}

template<class Allocator>
auto
basic_streambuf<Allocator>::mutable_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{*sb_, sb_->out_};
}

template<class Allocator>
auto
basic_streambuf<Allocator>::mutable_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{*sb_, sb_->list_.end()};
}

//------------------------------------------------------------------------------

template<class Allocator>
basic_streambuf<Allocator>::~basic_streambuf()
{
    delete_list();
}

template<class Allocator>
basic_streambuf<Allocator>::
basic_streambuf(basic_streambuf&& other)
    : detail::empty_base_optimization<allocator_type>(
        std::move(other.member()))
    , alloc_size_(other.alloc_size_)
    , in_size_(other.in_size_)
    , in_pos_(other.in_pos_)
    , out_pos_(other.out_pos_)
    , out_end_(other.out_end_)
{
    auto const at_end =
        other.out_ == other.list_.end();
    list_ = std::move(other.list_);
    out_ = at_end ? list_.end() : other.out_;
    other.in_size_ = 0;
    other.out_ = other.list_.end();
    other.in_pos_ = 0;
    other.out_pos_ = 0;
    other.out_end_ = 0;
}

template<class Allocator>
basic_streambuf<Allocator>::
basic_streambuf(basic_streambuf&& other,
        allocator_type const& alloc)
    : basic_streambuf(other.alloc_size_, alloc)
{
    using boost::asio::buffer_copy;
    if(this->member() != other.member())
        commit(buffer_copy(prepare(other.size()), other.data()));
    else
        move_assign(other, std::true_type{});
}

template<class Allocator>
auto
basic_streambuf<Allocator>::operator=(
    basic_streambuf&& other) -> basic_streambuf&
{
    if(this == &other)
        return *this;
    // VFALCO If any memory allocated we could use it first?
    clear();
    alloc_size_ = other.alloc_size_;
    move_assign(other, std::integral_constant<bool,
        alloc_traits::propagate_on_container_move_assignment::value>{});
    return *this;
}

template<class Allocator>
basic_streambuf<Allocator>::
basic_streambuf(basic_streambuf const& other)
    : basic_streambuf(other.alloc_size_,
        alloc_traits::select_on_container_copy_construction(other.member()))
{
    commit(boost::asio::buffer_copy(prepare(other.size()), other.data()));
}

template<class Allocator>
basic_streambuf<Allocator>::
basic_streambuf(basic_streambuf const& other,
        allocator_type const& alloc)
    : basic_streambuf(other.alloc_size_, alloc)
{
    commit(boost::asio::buffer_copy(prepare(other.size()), other.data()));
}

template<class Allocator>
auto
basic_streambuf<Allocator>::operator=(
    basic_streambuf const& other) ->
        basic_streambuf&
{
    if(this == &other)
        return *this;
    using boost::asio::buffer_copy;
    clear();
    copy_assign(other, std::integral_constant<bool,
        alloc_traits::propagate_on_container_copy_assignment::value>{});
    commit(buffer_copy(prepare(other.size()), other.data()));
    return *this;
}

template<class Allocator>
template<class OtherAlloc>
basic_streambuf<Allocator>::basic_streambuf(
    basic_streambuf<OtherAlloc> const& other)
    : basic_streambuf(other.alloc_size_)
{
    using boost::asio::buffer_copy;
    commit(buffer_copy(prepare(other.size()), other.data()));
}

template<class Allocator>
template<class OtherAlloc>
basic_streambuf<Allocator>::basic_streambuf(
    basic_streambuf<OtherAlloc> const& other,
        allocator_type const& alloc)
    : basic_streambuf(other.alloc_size_, alloc)
{
    using boost::asio::buffer_copy;
    commit(buffer_copy(prepare(other.size()), other.data()));
}

template<class Allocator>
template<class OtherAlloc>
auto
basic_streambuf<Allocator>::operator=(
    basic_streambuf<OtherAlloc> const& other) ->
        basic_streambuf&
{
    using boost::asio::buffer_copy;
    clear();
    commit(buffer_copy(prepare(other.size()), other.data()));
    return *this;
}

template<class Allocator>
basic_streambuf<Allocator>::basic_streambuf(
        std::size_t alloc_size, Allocator const& alloc)
    : detail::empty_base_optimization<allocator_type>(alloc)
    , out_(list_.end())
    , alloc_size_(alloc_size)
{
    if(alloc_size <= 0)
        throw detail::make_exception<std::invalid_argument>(
            "invalid alloc_size", __FILE__, __LINE__);
}

template<class Allocator>
std::size_t
basic_streambuf<Allocator>::capacity() const
{
    auto pos = out_;
    if(pos == list_.end())
        return in_size_;
    auto n = pos->size() - out_pos_;
    while(++pos != list_.end())
        n += pos->size();
    return in_size_ + n;
}

template<class Allocator>
auto
basic_streambuf<Allocator>::
data() const ->
    const_buffers_type
{
    return const_buffers_type(*this);
}

template<class Allocator>
auto
basic_streambuf<Allocator>::prepare(size_type n) ->
    mutable_buffers_type
{
    list_type reuse;
    if(out_ != list_.end())
    {
        if(out_ != list_.iterator_to(list_.back()))
        {
            out_end_ = out_->size();
            reuse.splice(reuse.end(), list_,
                std::next(out_), list_.end());
            debug_check();
        }
        auto const avail = out_->size() - out_pos_;
        if(n > avail)
        {
            out_end_ = out_->size();
            n -= avail;
        }
        else
        {
            out_end_ = out_pos_ + n;
            n = 0;
        }
        debug_check();
    }
    while(n > 0 && ! reuse.empty())
    {
        auto& e = reuse.front();
        reuse.erase(reuse.iterator_to(e));
        list_.push_back(e);
        if(n > e.size())
        {
            out_end_ = e.size();
            n -= e.size();
        }
        else
        {
            out_end_ = n;
            n = 0;
        }
        debug_check();
    }
    while(n > 0)
    {
        auto const size = std::max(alloc_size_, n);
        auto& e = *reinterpret_cast<element*>(static_cast<
            void*>(alloc_traits::allocate(this->member(),
                sizeof(element) + size)));
        alloc_traits::construct(this->member(), &e, size);
        list_.push_back(e);
        if(out_ == list_.end())
            out_ = list_.iterator_to(e);
        if(n >= e.size())
        {
            out_end_ = e.size();
            n -= e.size();
        }
        else
        {
            out_end_ = n;
            n = 0;
        }
        debug_check();
    }
    for(auto it = reuse.begin(); it != reuse.end();)
    {
        auto& e = *it++;
        reuse.erase(list_.iterator_to(e));
        auto const len = e.size() + sizeof(e);
        alloc_traits::destroy(this->member(), &e);
        alloc_traits::deallocate(this->member(),
            reinterpret_cast<char*>(&e), len);
    }
    return mutable_buffers_type(*this);
}

template<class Allocator>
void
basic_streambuf<Allocator>::commit(size_type n)
{
    if(list_.empty())
        return;
    if(out_ == list_.end())
        return;
    auto const back =
        list_.iterator_to(list_.back());
    while(out_ != back)
    {
        auto const avail =
            out_->size() - out_pos_;
        if(n < avail)
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

    n = (std::min)(n, out_end_ - out_pos_);
    out_pos_ += n;
    in_size_ += n;
    if(out_pos_ == out_->size())
    {
        ++out_;
        out_pos_ = 0;
        out_end_ = 0;
    }
    debug_check();
}

template<class Allocator>
void
basic_streambuf<Allocator>::consume(size_type n)
{
    if(list_.empty())
        return;

    for(;;)
    {
        if(list_.begin() != out_)
        {
            auto const avail = list_.front().size() - in_pos_;
            if(n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
                debug_check();
                break;
            }
            n -= avail;
            in_size_ -= avail;
            in_pos_ = 0;
            auto& e = list_.front();
            list_.erase(list_.iterator_to(e));
            auto const len = e.size() + sizeof(e);
            alloc_traits::destroy(this->member(), &e);
            alloc_traits::deallocate(this->member(),
                reinterpret_cast<char*>(&e), len);
            debug_check();
        }
        else
        {
            auto const avail = out_pos_ - in_pos_;
            if(n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
            }
            else
            {
                in_size_ = 0;
                if(out_ != list_.iterator_to(list_.back()) ||
                    out_pos_ != out_end_)
                {
                    in_pos_ = out_pos_;
                }
                else
                {
                    // Input and output sequences are empty, reuse buffer.
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

template<class Allocator>
void
basic_streambuf<Allocator>::
clear()
{
    delete_list();
    list_.clear();
    out_ = list_.begin();
    in_size_ = 0;
    in_pos_ = 0;
    out_pos_ = 0;
    out_end_ = 0;
}

template<class Allocator>
void
basic_streambuf<Allocator>::
move_assign(basic_streambuf& other, std::false_type)
{
    using boost::asio::buffer_copy;
    if(this->member() != other.member())
    {
        commit(buffer_copy(prepare(other.size()), other.data()));
        other.clear();
    }
    else
        move_assign(other, std::true_type{});
}

template<class Allocator>
void
basic_streambuf<Allocator>::
move_assign(basic_streambuf& other, std::true_type)
{
    this->member() = std::move(other.member());
    auto const at_end =
        other.out_ == other.list_.end();
    list_ = std::move(other.list_);
    out_ = at_end ? list_.end() : other.out_;

    in_size_ = other.in_size_;
    in_pos_ = other.in_pos_;
    out_pos_ = other.out_pos_;
    out_end_ = other.out_end_;

    other.in_size_ = 0;
    other.out_ = other.list_.end();
    other.in_pos_ = 0;
    other.out_pos_ = 0;
    other.out_end_ = 0;
}

template<class Allocator>
void
basic_streambuf<Allocator>::
copy_assign(basic_streambuf const& other, std::false_type)
{
    beast::detail::ignore_unused(other);
}

template<class Allocator>
void
basic_streambuf<Allocator>::
copy_assign(basic_streambuf const& other, std::true_type)
{
    this->member() = other.member();
}

template<class Allocator>
void
basic_streambuf<Allocator>::delete_list()
{
    for(auto iter = list_.begin(); iter != list_.end();)
    {
        auto& e = *iter++;
        auto const n = e.size() + sizeof(e);
        alloc_traits::destroy(this->member(), &e);
        alloc_traits::deallocate(this->member(),
            reinterpret_cast<char*>(&e), n);
    }
}

template<class Allocator>
void
basic_streambuf<Allocator>::debug_check() const
{
#ifndef NDEBUG
    using boost::asio::buffer_size;
    BOOST_ASSERT(buffer_size(data()) == in_size_);
    if(list_.empty())
    {
        BOOST_ASSERT(in_pos_ == 0);
        BOOST_ASSERT(in_size_ == 0);
        BOOST_ASSERT(out_pos_ == 0);
        BOOST_ASSERT(out_end_ == 0);
        BOOST_ASSERT(out_ == list_.end());
        return;
    }

    auto const& front = list_.front();

    BOOST_ASSERT(in_pos_ < front.size());

    if(out_ == list_.end())
    {
        BOOST_ASSERT(out_pos_ == 0);
        BOOST_ASSERT(out_end_ == 0);
    }
    else
    {
        auto const& out = *out_;
        auto const& back = list_.back();

        BOOST_ASSERT(out_end_ <= back.size());
        BOOST_ASSERT(out_pos_ <  out.size());
        BOOST_ASSERT(&out != &front || out_pos_ >= in_pos_);
        BOOST_ASSERT(&out != &front || out_pos_ - in_pos_ == in_size_);
        BOOST_ASSERT(&out != &back  || out_pos_ <= out_end_);
    }
#endif
}

template<class Allocator>
std::size_t
read_size_helper(basic_streambuf<
    Allocator> const& streambuf, std::size_t max_size)
{
    BOOST_ASSERT(max_size >= 1);
    // If we already have an allocated
    // buffer, try to fill that up first
    auto const avail = streambuf.capacity() - streambuf.size();
    if (avail > 0)
        return (std::min)(avail, max_size);
    // Try to have just one new block allocated
    constexpr std::size_t low = 512;
    if (streambuf.alloc_size_ > low)
        return (std::min)(max_size, streambuf.alloc_size_);
    // ...but enforce a 512 byte minimum.
    return (std::min)(max_size, low);
}

template<class Alloc, class T>
basic_streambuf<Alloc>&
operator<<(basic_streambuf<Alloc>& streambuf, T const& t)
{
    detail::write_dynabuf(streambuf, t);
    return streambuf;
}

} // beast

#endif
