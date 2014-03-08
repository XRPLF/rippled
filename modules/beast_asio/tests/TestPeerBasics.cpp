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

namespace beast {
namespace asio {

TestPeerBasics::Model::Model (model_t model)
    : m_model (model)
{
}

String TestPeerBasics::Model::name () const noexcept
{
    if (m_model == async)
        return "async";
    return "sync";
}

bool TestPeerBasics::Model::operator== (model_t model) const noexcept
{
    return m_model == model;
}

boost::asio::ssl::stream_base::handshake_type
    TestPeerBasics::to_handshake_type (PeerRole const& role)
{
    if (role == PeerRole::client)
        return boost::asio::ssl::stream_base::client;
    return boost::asio::ssl::stream_base::server;
}

//------------------------------------------------------------------------------

boost::system::error_category const& TestPeerBasics::test_category () noexcept
{
    struct test_category_type : boost::system::error_category
    {
        char const* name () const noexcept
        {
            return "TestPeer";
        }

        std::string message (int ev) const
        {
            switch (ev)
            {
            case errc::none:          return "No error";
            case errc::timeout:       return "The timeout expired before the test could complete";
            case errc::unexpected:    return "An unexpected test result was encountered";
            case errc::exceptioned:   return "An unexpected exception was thrown";
            case errc::skipped:       return "The test was skipped because of previous errors";
            default:
                break;
            };

            return "An unknown error";
        }

        boost::system::error_condition default_error_condition (int ev) const noexcept
        {
            return boost::system::error_condition (ev, *this);
        }

        bool equivalent (int ev, boost::system::error_condition const& condition) const noexcept
        {
            return default_error_condition (ev) == condition;
        }

        bool equivalent (boost::system::error_code const& code, int ev) const noexcept
        {
            return *this == code.category() && code.value() == ev;
        }
    };

    static test_category_type category;

    return category;
}

boost::system::error_code TestPeerBasics::make_error (errc::errc_t ev) noexcept
{
    return boost::system::error_code (ev, test_category ());
}

boost::system::error_code TestPeerBasics::make_error (errc::errc_t ev, boost::system::error_code& ec) noexcept
{
    return ec = make_error (ev);
}

bool TestPeerBasics::success (boost::system::error_code const& ec, bool eofIsOkay) noexcept
{
    if (eofIsOkay && ec == boost::asio::error::eof)
        return true;
    if (! ec)
        return true;
    breakpoint (ec);
    return false;
}

bool TestPeerBasics::failure (boost::system::error_code const& ec, bool eofIsOkay) noexcept
{
    return ! success (ec, eofIsOkay);
}

bool TestPeerBasics::expected (bool condition, boost::system::error_code& ec) noexcept
{
    if (condition)
    {
        ec = boost::system::error_code ();
    }
    else
    {
        make_error (errc::unexpected, ec);
        breakpoint (ec);
    }
    return condition;
}

bool TestPeerBasics::unexpected (bool condition, boost::system::error_code& ec) noexcept
{
    return ! expected (condition, ec);
}

bool TestPeerBasics::aborted (boost::system::error_code const& ec) noexcept
{
    return ec == boost::asio::error::operation_aborted;
}

//------------------------------------------------------------------------------

void TestPeerBasics::breakpoint (boost::system::error_code const& ec)
{
    // Set a breakpoint here to catch a failure
    std::string const& message = ec.message ();
    char const* const c_str = message.c_str ();

    breakpoint (c_str);
}

void TestPeerBasics::breakpoint (char const* const)
{
}

}
}
