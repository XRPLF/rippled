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

#ifndef RIPPLE_PEERFINDER_RESOLVER_H_INCLUDED
#define RIPPLE_PEERFINDER_RESOLVER_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Performs asynchronous domain name resolution. */
class Resolver
{
public:
    /** Create the service.
        This will automatically start the associated thread and io_service.
    */
    static Resolver* New ();

    /** Destroy the service.
        Any pending I/O operations will be canceled. This call blocks until
        all pending operations complete (either with success or with
        operation_aborted) and the associated thread and io_service have
        no more work remaining.
    */
    virtual ~Resolver () { }

    /** Cancel pending I/O.
        This issues cancel orders for all pending I/O operations and then
        returns immediately. Handlers will receive operation_aborted errors,
        or if they were already queued they will complete normally.
    */
    virtual void cancel () = 0;

    struct Result
    {
        Result ()
            { }

        /** The original name string */
        std::string name;

        /** The error code from the operation. */
        boost::system::error_code error;

        /** The resolved address.
            Only defined if there is no error.
            If the original name string contains a port specification,
            it will be set in the resolved IPAddress.
        */
        IPAddress address;
    };

    /** Performs an async resolution on the specified name.
        The port information, if present, will be passed through.
    */
    template <typename Handler>
    void async_resolve (std::string const& name,
        BEAST_MOVE_ARG(Handler) handler)
    {
        async_resolve (name,
            AbstractHandler <void (Result)> (
                BEAST_MOVE_CAST(Handler)(handler)));
    }

    virtual void async_resolve (std::string const& name,
        AbstractHandler <void (Result)> handler) = 0;
};

}
}

#endif
