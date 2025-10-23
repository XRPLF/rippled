#ifndef XRPL_APP_MISC_DETAIL_WORKFILE_H_INCLUDED
#define XRPL_APP_MISC_DETAIL_WORKFILE_H_INCLUDED

#include <xrpld/app/misc/detail/Work.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

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
        boost::asio::io_context& ios,
        callback_type cb);
    ~WorkFile();

    void
    run() override;

    void
    cancel() override;

private:
    std::string path_;
    callback_type cb_;
    boost::asio::io_context& ios_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
};

//------------------------------------------------------------------------------

WorkFile::WorkFile(
    std::string const& path,
    boost::asio::io_context& ios,
    callback_type cb)
    : path_(path)
    , cb_(std::move(cb))
    , ios_(ios)
    , strand_(boost::asio::make_strand(ios))
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
        return boost::asio::post(
            ios_,
            boost::asio::bind_executor(
                strand_, std::bind(&WorkFile::run, shared_from_this())));

    error_code ec;
    auto const fileContents = getFileContents(ec, path_, megabytes(1));

    XRPL_ASSERT(cb_, "ripple::detail::WorkFile::run : callback is set");
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
