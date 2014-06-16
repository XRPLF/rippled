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

#ifndef RIPPLE_VALIDATORS_MANAGER_H_INCLUDED
#define RIPPLE_VALIDATORS_MANAGER_H_INCLUDED

#include <beast/threads/Stoppable.h>
#include <beast/module/core/files/File.h>

namespace ripple {
namespace Validators {

/** Maintains the list of chosen validators.
    The algorithm for acquiring, building, and calculating metadata on
    the list of chosen validators is critical to the health of the network.
    All operations are performed asynchronously on an internal thread.
*/
class Manager : public beast::PropertyStream::Source
{
protected:
    Manager();

public:
    /** Create a new Manager object.
        @param parent The parent Stoppable.
        @param pathToDbFileOrDirectory The directory where our database is stored
        @param journal Where to send log output.
    */
    static Manager* New (
        beast::Stoppable& stoppableParent, 
        beast::File const& pathToDbFileOrDirectory,
        beast::Journal journal);

    /** Destroy the object.
        Any pending source fetch operations are aborted. This will block
        until any pending database I/O has completed and the thread has
        stopped.
    */
    virtual ~Manager () { }

    /** Add a static source of validators.
        The validators added using these methods will always be chosen when
        constructing the UNL regardless of statistics. The fetch operation
        is performed asynchronously, so this call returns immediately. A
        failed fetch (depending on the source) is not retried. The caller
        loses ownership of any dynamic objects.
        Thread safety:
            Can be called from any thread.
    */
    /** @{ */
    virtual void addStrings (beast::String name,
                             std::vector <std::string> const& strings) = 0;
    virtual void addStrings (beast::String name,
                             beast::StringArray const& stringArray) = 0;
    virtual void addFile (beast::File const& file) = 0;
    virtual void addStaticSource (Validators::Source* source) = 0;
    /** @} */

    /** Add a live source of validators from a trusted URL.
        The URL will be contacted periodically to update the list. The fetch
        operation is performed asynchronously, this call doesn't block.
        Thread safety:
            Can be called from any thread.
    */
    virtual void addURL (beast::URL const& url) = 0;

    /** Add a live source of validators.
        The caller loses ownership of the object. The fetch is performed
        asynchronously, this call doesn't block.
        Thread safety:
            Can be called from any thread.
    */
    virtual void addSource (Validators::Source* source) = 0;

    //--------------------------------------------------------------------------

    //virtual bool isPublicKeyTrusted (RipplePublicKey const& publicKey) = 0;

    /** Called when a validation with a proper signature is received. */
    virtual void receiveValidation (ReceivedValidation const& rv) = 0;
    
    /** Called when a ledger is closed. */
    virtual void ledgerClosed (RippleLedgerHash const& ledgerHash) = 0;
};

}
}

#endif
