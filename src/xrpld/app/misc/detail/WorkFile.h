//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_DETAIL_WORKFILE_H_INCLUDED
#define RIPPLE_APP_MISC_DETAIL_WORKFILE_H_INCLUDED

#include <xrpld/app/misc/detail/Work.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <cerrno>

namespace ripple {

namespace detail {

// Work with files
class WorkFile : public Work, public std::enable_shared_from_this<WorkFile>
{
protected:
    using error_code = boost::system::error_code;
    // Override the definition in Work.h
    using response_type = std::string;

public:
    using callback_type =
        std::function<void(error_code const&, response_type const&)>;

public:
    WorkFile(
        std::string const& path,
        boost::asio::io_service& ios,
        callback_type cb);
    ~WorkFile();

    void
    run() override;

    void
    cancel() override;

private:
    std::string path_;
    callback_type cb_;
    boost::asio::io_service& ios_;
    boost::asio::io_service::strand strand_;
};

//------------------------------------------------------------------------------

WorkFile::WorkFile(
    std::string const& path,
    boost::asio::io_service& ios,
    callback_type cb)
    : path_(path), cb_(std::move(cb)), ios_(ios), strand_(ios)
{
}

WorkFile::~WorkFile()
{
    if (cb_)
        cb_(make_error_code(boost::system::errc::interrupted), {});
}

void
WorkFile::run()
{
    if (!strand_.running_in_this_thread())
        return ios_.post(
            strand_.wrap(std::bind(&WorkFile::run, shared_from_this())));

    error_code ec;
    auto const fileContents = getFileContents(ec, path_, megabytes(1));

    ASSERT(cb_ != nullptr, "ripple::detail::WorkFile::run : callback is set");
    cb_(ec, fileContents);
    cb_ = nullptr;
}

void
WorkFile::cancel()
{
    // Nothing to do. Either it finished in run, or it didn't start.
}

}  // namespace detail

}  // namespace ripple

#endif
