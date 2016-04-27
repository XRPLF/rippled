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

#ifndef BEAST_EXAMPLE_FILE_BODY_H_INCLUDED
#define BEAST_EXAMPLE_FILE_BODY_H_INCLUDED

#include <beast/http/message.hpp>
#include <beast/http/resume_context.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/filesystem.hpp>
#include <cstdio>
#include <cstdint>

namespace beast {
namespace http {

struct file_body
{
    using value_type = std::string;

    class writer
    {
        std::size_t size_;
        std::size_t offset_ = 0;
        std::string const& path_;
        FILE* file_ = nullptr;
        char buf_[4096];
        std::size_t buf_len_;

    public:
        static bool constexpr is_single_pass = false;

        template<bool isRequest, class Headers>
        writer(message<isRequest, file_body, Headers> const& m) noexcept
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

        std::size_t
        content_length() const
        {
            return size_;
        }

        template<class Write>
        boost::tribool
        operator()(resume_context&&, error_code&, Write&& write)
        {
            buf_len_ = std::min(size_ - offset_, sizeof(buf_));
            auto const nread = fread(buf_, 1, sizeof(buf_), file_);
            (void)nread;
            offset_ += buf_len_;
            write(boost::asio::buffer(buf_, buf_len_));
            return offset_ >= size_;
        }
    };
};

} // http
} // beast

#endif
