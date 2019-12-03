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

namespace ripple {

inline void
DatabaseBody::value_type::close()
{
    {
        std::unique_lock lock(m_);

        // Stop all scheduled and currently
        // executing handlers before closing.
        if (handler_count_)
        {
            closing_ = true;

            auto predicate = [&] { return !handler_count_; };
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

    auto setup = setup_DatabaseCon(config);
    setup.dataDir = path.parent_path();

    conn_ = std::make_unique<DatabaseCon>(
        setup, "Download", DownloaderDBPragma, DatabaseBodyDBInit);

    path_ = path;

    auto db = conn_->checkoutDb();

    boost::optional<std::string> pathFromDb;

    *db << "SELECT Path FROM Download WHERE Part=0;", soci::into(pathFromDb);

    // Try to reuse preexisting
    // database.
    if (pathFromDb)
    {
        // Can't resuse - database was
        // from a different file download.
        if (pathFromDb != path.string())
        {
            *db << "DROP TABLE Download;";
        }

        // Continuing a file download.
        else
        {
            boost::optional<uint64_t> size;

            *db << "SELECT SUM(LENGTH(Data)) FROM Download;", soci::into(size);

            if (size)
                file_size_ = size.get();
        }
    }
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

                if (body_.handler_count_ >= MAX_HANDLERS)
                    post = false;
                else
                    ++body_.handler_count_;
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
            if (--body_.handler_count_ == 0)
            {
                lock.unlock();
                body_.c_.notify_one();
            }

            return;
        }
    }

    auto path = body_.path_.string();
    uint64_t rowSize;
    soci::indicator rti;

    uint64_t remainingInRow;

    auto db = body_.conn_->checkoutDb();

    auto be = dynamic_cast<soci::sqlite3_session_backend*>(db->get_backend());
    BOOST_ASSERT(be);

    // This limits how large we can make the blob
    // in each row. Also subtract a pad value to
    // account for the other values in the row.
    auto const blobMaxSize =
        sqlite_api::sqlite3_limit(be->conn_, SQLITE_LIMIT_LENGTH, -1) -
        MAX_ROW_SIZE_PAD;

    auto rowInit = [&] {
        *db << "INSERT INTO Download VALUES (:path, zeroblob(0), 0, :part)",
            soci::use(path), soci::use(body_.part_);

        remainingInRow = blobMaxSize;
        rowSize = 0;
    };

    *db << "SELECT Path,Size,Part FROM Download ORDER BY Part DESC "
           "LIMIT 1",
        soci::into(path), soci::into(rowSize), soci::into(body_.part_, rti);

    if (!db->got_data())
        rowInit();
    else
        remainingInRow = blobMaxSize - rowSize;

    auto insert = [&db, &rowSize, &part = body_.part_, &fs = body_.file_size_](
                      auto const& data) {
        uint64_t updatedSize = rowSize + data.size();

        *db << "UPDATE Download SET Data = CAST(Data || :data AS blob), "
               "Size = :size WHERE Part = :part;",
            soci::use(data), soci::use(updatedSize), soci::use(part);

        fs += data.size();
    };

    while (remainingInRow < data.size())
    {
        if (remainingInRow)
        {
            insert(data.substr(0, remainingInRow));
            data.erase(0, remainingInRow);
        }

        ++body_.part_;
        rowInit();
    }

    insert(data);

    bool const notify = [this] {
        std::lock_guard lock(body_.m_);
        return --body_.handler_count_ == 0;
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
        if (body_.handler_count_)
        {
            auto predicate = [&] { return !body_.handler_count_; };
            body_.c_.wait(lock, predicate);
        }
    }

    auto db = body_.conn_->checkoutDb();

    soci::rowset<std::string> rs =
        (db->prepare << "SELECT Data FROM Download ORDER BY PART ASC;");

    std::ofstream fout;
    fout.open(body_.path_.string(), std::ios::binary | std::ios::out);

    // iteration through the resultset:
    for (auto it = rs.begin(); it != rs.end(); ++it)
        fout.write(it->data(), it->size());

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
