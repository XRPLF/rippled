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

#ifndef BEAST_WSPROTO_SOCKET_BASE_H_INCLUDED
#define BEAST_WSPROTO_SOCKET_BASE_H_INCLUDED

#include <beast/wsproto/error.h>
#include <beast/wsproto/role.h>
#include <beast/wsproto/detail/frame.h>
#include <beast/wsproto/detail/mask.h>
#include <beast/wsproto/detail/utf8_checker.h>
#include <beast/http/message.h>
#include <beast/is_call_possible.h>
#include <cassert>
#include <cstdint>
#include <memory>

namespace beast {
namespace wsproto {
namespace detail {

template<class Handler, class Signature>
using is_handler = std::integral_constant<bool,
    std::is_copy_constructible<std::decay_t<Handler>>::value &&
        beast::is_call_possible<Handler, Signature>::value>;

struct at_most
{
    std::size_t n;
    
    explicit
    at_most(std::size_t n_)
        : n(n_)
    {
    }

    std::size_t
    operator()(error_code,
        std::size_t bytes_transferred)
    {
        if(bytes_transferred >= n)
            return 0;
        return n - bytes_transferred;
    }
};

template<class String>
inline
void
maybe_throw(error_code const& ec, String const&)
{
    if(ec)
        throw boost::system::system_error{ec};
}

struct abstract_decorator
{
    virtual ~abstract_decorator() = default;
        
    virtual
    void
    operator()(beast::http::message& m) const = 0;
};

template<class Decorator>
struct decorator : abstract_decorator
{
    Decorator d;

    template<class DeducedDecorator>
    decorator(DeducedDecorator&& d_)
        : d(std::forward<DeducedDecorator>(d_))
    {
    }

    void
    operator()(beast::http::message& m) const override
    {
        d(m);
    }
};

class invokable
{
    struct base
    {
        virtual ~base() = default;
        virtual void operator()() = 0;
    };

    template<class F>
    struct holder : base
    {
        F f;

        holder(holder&&) = default;
        holder(holder const&) = default;

        template<class U>
        explicit
        holder(U&& u)
            : f(std::forward<U>(u))
        {
        }

        void
        operator()() override
        {
            F f_(std::move(f));
            this->~holder();
            // invocation of f_() can
            // assign a new invokable.
            f_();
        }
    };

    struct exemplar
    {
        std::shared_ptr<int> _;
        void operator()() {}
    };

    using buf_type = std::array<std::uint8_t,
        sizeof(holder<exemplar>)>;

    bool b_ = false;
    buf_type buf_;

public:
    invokable() = default;

    ~invokable()
    {
        if(b_)
            get().~base();
    }

    explicit
    operator bool() const
    {
        return b_;
    }

    template<class F>
    void
    emplace(F&& f);

    void
    maybe_invoke()
    {
        if(b_)
        {
            b_ = false;
            get()();
        }
    }

private:
    base&
    get()
    {
        return *reinterpret_cast<base*>(buf_.data());
    }
};

template<class F>
void
invokable::emplace(F&& f)
{
    static_assert(sizeof(buf_type) >= sizeof(holder<F>),
        "static buffer too small");
    assert(! b_);
    ::new(buf_.data()) holder<F>(
        std::forward<F>(f));
    b_ = true;
}

//------------------------------------------------------------------------------

struct socket_base
{
    detail::maskgen maskgen_;
    std::unique_ptr<abstract_decorator> decorate_;
    bool keep_alive_ = false;
    role_type role_;

    // buffer for reading
    asio::streambuf rd_sb_;

    // current frame header
    frame_header rd_fh_;

    // prepared masking key
    detail::prepared_key_type rd_key_;

    // utf8 check state for current text msg
    detail::utf8_checker rd_utf8_check_;

    // bytes remaining in binary/text frame payload
    std::size_t rd_need_ = 0;

    // opcode of current binary or text message
    opcode::value rd_op_;

    // expecting a continuation frame
    bool rd_cont_ = false;

    bool wr_cont_ = false;

    std::size_t wr_frag_ = 0;

    // true when async write is pending
    bool wr_active_ = false;

    invokable wr_invoke_;
    invokable rd_invoke_;

    bool closing_ = false;

    template<class = void>
    void
    prepare_fh(close::value& code);

    template<class Streambuf>
    void
    write_close(Streambuf& sb,
        close::value code, std::string reason = "");

    template<class Streambuf>
    void
    write_ping(Streambuf& sb,
        opcode::value op, std::string data);
};

} // detail
} // wsproto
} // beast

#endif
