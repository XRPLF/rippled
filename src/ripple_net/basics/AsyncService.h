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


#ifndef RIPPLE_NET_ASYNCSERVICE_H_INCLUDED
#define RIPPLE_NET_ASYNCSERVICE_H_INCLUDED

/** Stoppable subclass that helps with managing asynchronous stopping. */
class AsyncService : public Stoppable
{
public:
    /** Create the service with the specified name and parent. */
    AsyncService (char const* name, Stoppable& parent);

    ~AsyncService ();

    /** Increments the count of pending I/O for the service.

        This should be called every time an asynchronous initiating
        function is called by the derived clas.

        Thread safety:
            Safe to call from any thread at any time.
    */
    void serviceCountIoPending ();

    /** Decrements the count of pending I/O for the service.

        This should be called at the very beginning of every completion
        handler function in the derived class.
        
        Thread safety:
            Safe to call from any thread at any time.

        @param The error_code of the completed asynchronous opereation.
        @return `true` if the handler should return immediately.
    */
    bool serviceCountIoComplete (boost::system::error_code const& ec);

    /** Called after a stop notification when all pending I/O is complete.
        The default implementation calls stopped.
        @see stopped
    */
    virtual void onServiceIoComplete ();

private:
    Atomic <int> m_pendingIo;
};

#endif
