//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/rdb/RelationalDBInterface_global.h>

namespace ripple {

inline void
DatabaseBody::value_type::close()
{
    {
        std::unique_lock lock(m_);

        // Stop all scheduled and currently
        // executing handlers before closing.
        if (handlerCount_)
        {
            closing_ = true;

            auto predicate = [&] { return !handlerCount_; };
            c_.wait(lock, predicate);
        }

        conn_.reset();
    }
}

inline void
DatabaseBody::value_type::open(
    boost::filesystem::path path,
    Config const& config,
    boost::asio::io_service& io_service,
    boost::system::error_code& ec)
{
    strand_.reset(new boost::asio::io_service::strand(io_service));
    path_ = path;

    auto setup = setup_DatabaseCon(config);
    setup.dataDir = path.parent_path();
    setup.useGlobalPragma = false;

    auto [conn, size] = openDatabaseBodyDb(setup, path);
    conn_ = std::move(conn);
    if (size)
        fileSize_ = *size;
}

// This is called from message::payload_size
inline std::uint64_t
DatabaseBody::size(value_type const& body)
{
    // Forward the call to the body
    return body.size();
}

// We don't do much in the reader constructor since the
// database is already open.
//
template <bool isRequest, class Fields>
DatabaseBody::reader::reader(
    boost::beast::http::header<isRequest, Fields>&,
    value_type& body)
    : body_(body)
{
}

// We don't do anything with content_length but a sophisticated
// application might check available space on the device
// to see if there is enough room to store the body.
inline void
DatabaseBody::reader::init(
    boost::optional<std::uint64_t> const& /*content_length*/,
    boost::system::error_code& ec)
{
    // The connection must already be available for writing
    assert(body_.conn_);

    // The error_code specification requires that we
    // either set the error to some value, or set it
    // to indicate no error.
    //
    // We don't do anything fancy so set "no error"
    ec = {};
}

// This will get called one or more times with body buffers
//
template <class ConstBufferSequence>
std::size_t
DatabaseBody::reader::put(
    ConstBufferSequence const& buffers,
    boost::system::error_code& ec)
{
    // This function must return the total number of
    // bytes transferred from the input buffers.
    std::size_t nwritten = 0;

    // Loop over all the buffers in the sequence,
    // and write each one to the database.
    for (auto it = buffer_sequence_begin(buffers);
         it != buffer_sequence_end(buffers);
         ++it)
    {
        boost::asio::const_buffer buffer = *it;

        body_.batch_.append(
            static_cast<const char*>(buffer.data()), buffer.size());

        // Write this buffer to the database
        if (body_.batch_.size() > FLUSH_SIZE)
        {
            bool post = true;

            {
                std::lock_guard lock(body_.m_);

                if (body_.handlerCount_ >= MAX_HANDLERS)
                    post = false;
                else
                    ++body_.handlerCount_;
            }

            if (post)
            {
                body_.strand_->post(
                    [data = body_.batch_, this] { this->do_put(data); });

                body_.batch_.clear();
            }
        }

        nwritten += it->size();
    }

    // Indicate success
    // This is required by the error_code specification
    ec = {};

    return nwritten;
}

inline void
DatabaseBody::reader::do_put(std::string data)
{
    using namespace boost::asio;

    {
        std::unique_lock lock(body_.m_);

        // The download is being halted.
        if (body_.closing_)
        {
            if (--body_.handlerCount_ == 0)
            {
                lock.unlock();
                body_.c_.notify_one();
            }

            return;
        }
    }

    auto path = body_.path_.string();

    {
        auto db = body_.conn_->checkoutDb();
        body_.part_ = databaseBodyDoPut(
            *db, data, path, body_.fileSize_, body_.part_, MAX_ROW_SIZE_PAD);
    }

    bool const notify = [this] {
        std::lock_guard lock(body_.m_);
        return --body_.handlerCount_ == 0;
    }();

    if (notify)
        body_.c_.notify_one();
}

// Called after writing is done when there's no error.
inline void
DatabaseBody::reader::finish(boost::system::error_code& ec)
{
    {
        std::unique_lock lock(body_.m_);

        // Wait for scheduled DB writes
        // to complete.
        if (body_.handlerCount_)
        {
            auto predicate = [&] { return !body_.handlerCount_; };
            body_.c_.wait(lock, predicate);
        }
    }

    std::ofstream fout;
    fout.open(body_.path_.string(), std::ios::binary | std::ios::out);

    {
        auto db = body_.conn_->checkoutDb();
        databaseBodyFinish(*db, fout);
    }

    // Flush any pending data that hasn't
    // been been written to the DB.
    if (body_.batch_.size())
    {
        fout.write(body_.batch_.data(), body_.batch_.size());
        body_.batch_.clear();
    }

    fout.close();
}

}  // namespace ripple
