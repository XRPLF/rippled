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
#include <beast/http/message.h>
#include <cstdint>
#include <memory>

namespace beast {
namespace wsproto {
namespace detail {

template<class T, std::size_t Size>
class small_object_ptr
{
    T* t_ = nullptr;
    std::unique_ptr<T> p_;
    std::array<std::uint8_t, Size> buf_;

public:
    small_object_ptr() = default;

    ~small_object_ptr()
    {
        clear();
    }

    template<class U, class... Args>
    void
    emplace(Args&&... args)
    {
        clear();
        if(sizeof(U) <= Size)
        {
            p_ = nullptr;
            t_ = new(buf_.data()) U(
                std::forward<Args>(args)...);
            return;
        }
        auto u = std::make_unique<U>(
            std::forward<Args>(args)...);
        t_ = u.get();
        p_ = std::move(u);
    }

    T* get()
    {
        return t_;
    }

    T const* get() const
    {
        return t_;
    }

    T* operator->()
    {
        return get();
    }

    T const* operator->() const
    {
        return get();
    }

    explicit
    operator bool() const
    {
        return get() != nullptr;
    }

private:
    void
    clear()
    {
        if(! t_)
            return;
        if(p_)
            p_ = nullptr;
        else
            t_->~T();
        t_ = nullptr;
    }
};

//------------------------------------------------------------------------------

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

struct invokable
{
    virtual ~invokable() = default;
    virtual void operator()() = 0;
};

template<class Op>
struct invokable_op : invokable
{
    Op op;

    template<class... Args>
    explicit
    invokable_op(Args&&... args)
        : op(std::forward<Args>(args)...)
    {
    }

    void
    operator()() override
    {
        op();
    }
};

//------------------------------------------------------------------------------

class socket_base
{
protected:
    detail::maskgen maskgen_;
    std::unique_ptr<abstract_decorator> decorate_;
    bool keep_alive_ = false;
    role_type role_;

    // read state
    frame_header rd_fh_;
    detail::prepared_key_type rd_key_;
    std::size_t rd_need_ = 0;
    opcode::value rd_op_;
    bool rd_cont_ = false;
    bool rd_active_ = false;

    // write state
    bool wr_cont_ = false;
    std::size_t wr_frag_ = 0;
    bool wr_active_ = false;

    bool closing_ = false;

protected:
    template<class = void>
    error_code
    prepare_fh();

    template<class Streambuf>
    void
    write_close(Streambuf& sb,
        close::value code, std::string reason);

    template<class Streambuf>
    void
    write_ping(Streambuf& sb,
        opcode::value op, std::string data);
};

} // detail
} // wsproto
} // beast

#endif
