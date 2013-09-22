//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_PEER_H_INCLUDED
#define RIPPLE_HTTP_PEER_H_INCLUDED

namespace ripple {
namespace HTTP {

using namespace beast;

/** Represents an active connection. */
class Peer
    : public SharedObject
    , public AsyncObject <Peer>
    , public List <Peer>::Node
    , public LeakChecked <Peer>
{
public:
    enum
    {
        // Size of our receive buffer
        bufferSize = 8192,

        // Largest HTTP request allowed
        maxRequestBytes = 32 * 1024,

        // Max seconds without receiving a byte
        dataTimeoutSeconds = 10,

        // Max seconds without completing the request
        requestTimeoutSeconds = 30

    };

    typedef SharedPtr <Peer> Ptr;

    ServerImpl& m_impl;
    boost::asio::io_service::strand m_strand;
    boost::asio::deadline_timer m_data_timer;
    boost::asio::deadline_timer m_request_timer;
    ScopedPointer <MultiSocket> m_socket;
    MemoryBlock m_buffer;
    HTTPParser m_parser;
    SessionImpl m_session;
    int m_writesPending;
    bool m_closed;
    bool m_callClose;

    Peer (ServerImpl& impl, Port const& port);
    ~Peer ();
    socket& get_socket();
    SessionImpl& session ();
    void close ();
    void cancel ();
    void failed (error_code ec);
    void asyncHandlersComplete ();
    void write (void const* buffer, std::size_t bytes);
    void handle_write (SharedBuffer const& buf, CompletionCounter);
    void async_write (SharedBuffer const& buf);

    template <typename BufferSequence>
    void async_write (BufferSequence const& buffers)
    {
        // Iterate over each linear vector in the BufferSequence.
        for (typename BufferSequence::const_iterator iter (buffers.begin());
            iter != buffers.end(); ++iter)
        {
            typename BufferSequence::value_type const& buffer (*iter);

            // Put a copy of this section of the buffer sequence into
            // a reference counted, shared container.
            //
            async_write (SharedBuffer (
                boost::asio::buffer_cast <char const*> (buffer),
                    boost::asio::buffer_size (buffer)));
        }
    }

    void async_read_some ();

    void handle_accept ();
    void handle_handshake (error_code ec, CompletionCounter);
    void handle_data_timer (error_code ec, CompletionCounter);
    void handle_request_timer (error_code ec, CompletionCounter);
    void handle_close (CompletionCounter);

    void handle_write (error_code ec, std::size_t bytes_transferred,
        SharedBuffer buf, CompletionCounter);

    void handle_read (error_code ec, std::size_t bytes_transferred,
        CompletionCounter);

    void handle_headers ();
    void handle_request ();
};

}
}

#endif
