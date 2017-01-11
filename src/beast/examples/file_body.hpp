//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_FILE_BODY_H_INCLUDED
#define BEAST_EXAMPLE_FILE_BODY_H_INCLUDED

#include <beast/core/error.hpp>
#include <beast/http/message.hpp>
#include <beast/http/resume_context.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/filesystem.hpp>
#include <boost/logic/tribool.hpp>
#include <cstdio>
#include <cstdint>

namespace beast {
namespace http {

struct file_body
{
    using value_type = std::string;

    class writer
    {
        std::uint64_t size_ = 0;
        std::uint64_t offset_ = 0;
        std::string const& path_;
        FILE* file_ = nullptr;
        char buf_[4096];
        std::size_t buf_len_;

    public:
        writer(writer const&) = delete;
        writer& operator=(writer const&) = delete;

        template<bool isRequest, class Fields>
        writer(message<isRequest, file_body, Fields> const& m) noexcept
            : path_(m.body)
        {
        }

        ~writer()
        {
            if(file_)
                fclose(file_);
        }

        void
        init(error_code& ec) noexcept
        {
            file_ = fopen(path_.c_str(), "rb");
            if(! file_)
                ec = boost::system::errc::make_error_code(
                    static_cast<boost::system::errc::errc_t>(errno));
            else
                size_ = boost::filesystem::file_size(path_);
        }

        std::uint64_t
        content_length() const noexcept
        {
            return size_;
        }

        template<class WriteFunction>
        boost::tribool
        write(resume_context&&, error_code&,
            WriteFunction&& wf) noexcept
        {
            if(size_ - offset_ < sizeof(buf_))
                buf_len_ = static_cast<std::size_t>(
                    size_ - offset_);
            else
                buf_len_ = sizeof(buf_);
            auto const nread = fread(buf_, 1, sizeof(buf_), file_);
            (void)nread;
            offset_ += buf_len_;
            wf(boost::asio::buffer(buf_, buf_len_));
            return offset_ >= size_;
        }
    };
};

} // http
} // beast

#endif
