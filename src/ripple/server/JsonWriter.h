//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_JSONWRITER_H_INCLUDED
#define RIPPLE_SERVER_JSONWRITER_H_INCLUDED

#include <ripple/server/Writer.h>
#include <ripple/json/json_value.h>
#include <beast/asio/streambuf.h>
#include <beast/http/message.h>
#include <algorithm>
#include <cstddef>
#include <sstream>

namespace ripple {
namespace HTTP {

namespace detail {

/** Writer that sends to Streambufs sequentially. */
template <class Streambuf>
class message_writer : public Writer
{
private:
    Streambuf prebody_;
    Streambuf body_;
    std::size_t hint_ = 0;
    std::vector<boost::asio::const_buffer> data_;

public:
    message_writer (Streambuf&& prebody, Streambuf&& body);

    bool
    complete() override;

    bool
    prepare (std::size_t n, std::function<void(void)>) override;

    std::vector<boost::asio::const_buffer>
    data() override;

    void
    consume (std::size_t n) override;

private:
    std::vector<boost::asio::const_buffer>
    data(Streambuf& buf);
};

template <class Streambuf>
message_writer<Streambuf>::message_writer (
        Streambuf&& prebody, Streambuf&& body)
    : prebody_(std::move(prebody))
    , body_(std::move(body))
{
}

template <class Streambuf>
bool
message_writer<Streambuf>::complete()
{
    return prebody_.size() == 0 && body_.size() == 0;
}

template <class Streambuf>
bool
message_writer<Streambuf>::prepare (
    std::size_t n, std::function<void(void)>)
{
    hint_ = n;
    return true;
}

template <class Streambuf>
std::vector<boost::asio::const_buffer>
message_writer<Streambuf>::data()
{
    return (prebody_.size() > 0) ?
        data(prebody_) : data(body_);
}

template <class Streambuf>
void
message_writer<Streambuf>::consume (std::size_t n)
{
    if (prebody_.size() > 0)
        return prebody_.consume(n);
    body_.consume(n);
}

template <class Streambuf>
std::vector<boost::asio::const_buffer>
message_writer<Streambuf>::data(Streambuf& buf)
{
    data_.resize(0);
    for (auto iter = buf.data().begin();
        hint_ > 0 && iter != buf.data().end(); ++iter)
    {
        auto const n = std::min(hint_,
            boost::asio::buffer_size(*iter));
        data_.emplace_back(boost::asio::buffer_cast<
            void const*>(*iter), n);
        hint_ -= n;
    }
    return data_;
}

} // detail

using streambufs_writer = detail::message_writer<beast::asio::streambuf>;

//------------------------------------------------------------------------------

/** Write a Json::Value to a Streambuf. */
template <class Streambuf>
void
write(Streambuf& buf, Json::Value const& json)
{
    stream(json,
        [&buf](void const* data, std::size_t n)
        {
            buf.commit(boost::asio::buffer_copy(
                buf.prepare(n), boost::asio::buffer(data, n)));
        });
}

/** Returns a Writer that streams the provided HTTP message and Json body.
    The message is modified to include the correct headers.
*/
template <class = void>
std::shared_ptr<Writer>
make_JsonWriter (beast::http::message& m, Json::Value const& json)
{
    beast::asio::streambuf prebody;
    beast::asio::streambuf body;
    write(body, json);
    // VFALCO TODO Better way to set a field
    m.headers.erase ("Content-Length");
    m.headers.append("Content-Length", std::to_string(body.size()));
    m.headers.erase ("Content-Type");
    m.headers.append("Content-Type", "application/json");
    write(prebody, m);
    return std::make_shared<streambufs_writer>(
        std::move(prebody), std::move(body));
}

}
}

#endif
