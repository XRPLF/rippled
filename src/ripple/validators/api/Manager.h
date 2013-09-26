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

namespace ripple {
namespace Validators {

/** Maintains the list of chosen validators.

    The algorithm for acquiring, building, and calculating metadata on
    the list of chosen validators is critical to the health of the network.

    All operations are performed asynchronously on an internal thread.
*/
class Manager : public RPC::Service
{
public:
    /** Create a new Manager object.
    */
    static Manager* New (Stoppable& parent, Journal journal);

    /** Destroy the object.

        Any pending source fetch operations are aborted.

        There may be some listener calls made before the
        destructor returns.
    */
    virtual ~Manager () { }

    /** Add a static source of validators from a string array. */
    /** @{ */
    virtual void addStrings (String name,
                             std::vector <std::string> const& strings) = 0;
    virtual void addStrings (String name,
                             StringArray const& stringArray) = 0;
    /** @} */

    /** Add a static source of validators from a text file. */
    virtual void addFile (File const& file) = 0;

    /** Add a static source of validators.
        The Source is called to fetch once and the results are kept
        permanently. The fetch is performed asynchronously, this call
        returns immediately. If the fetch fails, it is not reattempted.
        The caller loses ownership of the object.
        Thread safety:
            Can be called from any thread.
    */
    virtual void addStaticSource (Source* source) = 0;

    /** Add a live source of validators from a trusted URL.
        The URL will be contacted periodically to update the list.
    */
    virtual void addURL (URL const& url) = 0;

    /** Add a live source of validators.
        The caller loses ownership of the object.
        Thread safety:
            Can be called from any thread.
    */
    virtual void addSource (Source* source) = 0;

    //--------------------------------------------------------------------------

    // Trusted Validators

    //virtual bool isPublicKeyTrusted (RipplePublicKey const& publicKey) = 0;

    //--------------------------------------------------------------------------

    /** Called when a validation with a proper signature is received. */
    virtual void receiveValidation (ReceivedValidation const& rv) = 0;
    
    /** Called when a ledger is closed. */
    virtual void ledgerClosed (RippleLedgerHash const& ledgerHash) = 0;
};

}
}

#endif
