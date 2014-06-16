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

#ifndef BEAST_ASIO_TESTS_TESTPEERTYPE_H_INCLUDED
#define BEAST_ASIO_TESTS_TESTPEERTYPE_H_INCLUDED

#include <beast/asio/placeholders.h>

namespace beast {
namespace asio {

template <typename Logic, typename Details>
class TestPeerType
    : public Details
    , public Logic
    , public TestPeer
    , public Thread
{
protected:
    // TestPeerDetails
    using Details::get_socket;
    using Details::get_acceptor;
    using Details::get_io_service;

    // Details
    typedef typename Details::protocol_type protocol_type;
    typedef typename Details::socket_type   socket_type;
    typedef typename Details::acceptor_type acceptor_type;
    typedef typename Details::endpoint_type endpoint_type;
    typedef typename Details::resolver_type resolver_type;

    using Details::get_native_socket;
    using Details::get_native_acceptor;
    using Details::get_endpoint;

    // TestPeerLogic
    typedef typename Logic::error_code error_code;
    using Logic::error;
    using Logic::socket;
    using Logic::get_role;
    using Logic::get_model;
    using Logic::on_connect;
    using Logic::on_connect_async;
    using Logic::pure_virtual;

    typedef TestPeerType <Logic, Details> This;

public:
    // Details
    typedef typename Details::arg_type arg_type;
    typedef typename Details::native_socket_type   native_socket_type;
    typedef typename Details::native_acceptor_type native_acceptor_type;

    TestPeerType (arg_type const& arg)
        : Details (arg)
        , Logic (get_socket ())
        , Thread (name ())
        , m_timer (get_io_service ())
        , m_timer_set (false)
        , m_timed_out (false)
    {
    }

    ~TestPeerType ()
    {
    }

    String name () const
    {
        return get_model ().name () + "_" + get_role ().name ();
    }

    bool is_async () const noexcept
    {
        return get_model () == Model::async;
    }

    void start (int timeoutSeconds)
    {
        if (is_async ())
        {
            if (timeoutSeconds > 0)
            {
                m_timer.expires_from_now (
                    boost::posix_time::seconds (timeoutSeconds));

                m_timer.async_wait (std::bind (&This::on_deadline,
                    this, beast::asio::placeholders::error));

                m_timer_set = true;
            }
            else
            {
                // Don't set the timer, so infinite wait.
            }
        }
        else
        {
            // Save the value for when join() is called later.
            //
            m_timeoutSeconds = timeoutSeconds;
        }

        startThread ();

        // For server roles block until the thread is litening.
        //
        if (get_role () == PeerRole::server)
            m_listening.wait ();
    }

    error_code join ()
    {
        if (is_async ())
        {
            // If the timer expired, then all our i/o should be
            // aborted and the thread will exit. So we will wait
            // for the thread for an infinite amount of time to
            // prevent undefined behavior. If an asynchronous logic
            // fails to end when the deadline timer expires, it
            // means there's a bug in the logic code.
            //
            m_join.wait ();

            // The wait was satisfied but now the thread is still on
            // it's way out of the thread function, so block until
            // we know its done.
            //
            stopThread ();

            // If we timed out then always report the custom error
            if (m_timed_out)
                return error (make_error (errc::timeout));
        }
        else
        {
            if (m_timeoutSeconds > 0)
            {
                // Wait for the thread to finish
                //
                if (! m_join.wait (m_timeoutSeconds * 1000))
                {
                    // Uh oh, we timed out! This is bad.
                    // The synchronous model requires that the thread
                    // be forcibly killed, which can result in undefined
                    // behavior. It's best not to perform tests with
                    // synchronous Logic objects that are supposed to time out.

                    // Force the thread to be killed, without waiting.
                    stopThread (0);

                    error () = make_error (errc::timeout);
                }
                else
                {
                    stopThread ();
                }
            }
            else
            {
                // They requested an infinite wait.
                //
                m_join.wait ();

                stopThread ();
            }
        }

        return error ();
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        if (is_async ())
        {
            if (get_role () == PeerRole::server)
            {
                run_async_server ();
            }
            else if (get_role () == PeerRole::client)
            {
                run_async_client ();
            }
            else
            {
                error () = make_error (errc::unexpected);
            }
        }
        else if (get_model () == Model::sync)
        {
            if (get_role () == PeerRole::server)
            {
                run_sync_server ();
            }
            else if (get_role () == PeerRole::client)
            {
                run_sync_client ();
            }
            else
            {
                error () = make_error (errc::unexpected);
            }
        }
        else
        {
            error () = make_error (errc::unexpected);
        }

        get_io_service ().run ();
    }

    //--------------------------------------------------------------------------

    void run_sync_server ()
    {
        do_listen ();

        if (failure (error ()))
            return finished ();

        if (failure (get_acceptor ().accept (get_socket (), error ())))
            return finished ();

        if (failure (get_acceptor ().close (error ())))
            return finished ();

        this->on_connect ();

        finished ();
    }

    //--------------------------------------------------------------------------

    void on_accept (error_code const& ec)
    {
        if (failure (ec))
            return finished ();

        // Close the acceptor down so we don't block the io_service forever
        //
        // VFALCO NOTE what difference between cancel and close?
#if 0
        if (failure (get_acceptor ().close (error ())))
            return finished ();
#endif

        this->on_connect_async (ec);
    }

    void run_async_server ()
    {
        do_listen ();

        if (failure (error ()))
            return finished ();

        get_acceptor ().async_accept (get_socket (), std::bind (
            &This::on_accept, this, beast::asio::placeholders::error));
    }

    //--------------------------------------------------------------------------

    void run_sync_client ()
    {
        if (failure (get_native_socket ().connect (get_endpoint (get_role ()), error ())))
            return finished ();

        this->on_connect ();

        finished ();
    }

    void run_async_client ()
    {
        get_native_socket ().async_connect (get_endpoint (get_role ()),
            std::bind (&Logic::on_connect_async, this,
                beast::asio::placeholders::error));
    }

    //--------------------------------------------------------------------------

    void do_listen ()
    {
        if (failure (get_native_acceptor ().open (
            get_endpoint (get_role ()).protocol (), error ())))
                return;

        // VFALCO TODO Figure out how to not hard code boost::asio::socket_base
        if (failure (get_native_acceptor ().set_option (
                boost::asio::socket_base::reuse_address (true), error ())))
            return;

        if (failure (get_native_acceptor ().bind (get_endpoint (get_role ()), error ())))
            return;

        // VFALCO TODO Figure out how to not hard code boost::asio::socket_base
        if (failure (get_native_acceptor ().listen (
                boost::asio::socket_base::max_connections, error ())))
            return;

        m_listening.signal ();
    }

    void on_deadline (error_code const& ec)
    {
        m_timer_set = false;

        if (ec != boost::asio::error::operation_aborted)
        {
            // We expect that ec represents no error, since the
            // timer expired and the operation wasn't aborted.
            //
            // If by some chance there is an error in ec we will
            // report that as an unexpected test condition instead
            // of a timeout.
            //
            if (expected (! ec, error ()))
                m_timed_out = true;
        }
        else
        {
            // The timer was canceled because the Logic
            // called finished(), so we do nothing here.
        }

        finished ();
    }

    void finished ()
    {
        // If the server errors out it will come through
        // here so signal the listening event and unblock
        // the main thread.
        //
        if (get_role () == PeerRole::server)
            m_listening.signal ();

        if (m_timer_set)
        {
            error_code ec;
            std::size_t const amount = m_timer.cancel (ec);

            // The Logic should not have any I/O pending when
            // it calls finished, so amount should be zero.
            //
            unexpected (amount == 0, ec);
        }

        // The logic should close the socket at the end of
        // its operations, unless it encounters an error.
        // Therefore, we will clean everything up and squelch
        // any errors, so that io_service::run() will return.
        //
        {
            error_code ec;
            this->get_socket ().close (ec);
        }

        // The acceptor will not have closed if the client
        // never established the connection, so do it here.
        {
            error_code ec;
            this->get_acceptor ().close (ec);
        }

        // Wake up the thread blocked on join()
        m_join.signal ();
    }

private:
    WaitableEvent m_listening;
    WaitableEvent m_join;

    // for async peers
    boost::asio::deadline_timer m_timer;
    bool m_timer_set;
    bool m_timed_out;

    // for sync peers
    int m_timeoutSeconds;
};

}
}

#endif
