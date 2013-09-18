//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_MANAGER_H_INCLUDED
#define RIPPLE_VALIDATORS_MANAGER_H_INCLUDED

namespace Validators
{

/** Maintains the list of chosen validators.

    The algorithm for acquiring, building, and calculating metadata on
    the list of chosen validators is critical to the health of the network.

    All operations are performed asynchronously on an internal thread.
*/
class Manager : public Uncopyable
{
public:
    /** Create a new Manager object.
    */
    static Manager* New (Service& parent, Journal journal);

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
    virtual void addURL (UniformResourceLocator const& url) = 0;

    /** Add a live source of validators.
        The caller loses ownership of the object.
        Thread safety:
            Can be called from any thread.
    */
    virtual void addSource (Source* source) = 0;

    //--------------------------------------------------------------------------

    // Trusted Validators

    //virtual bool isPublicKeyTrusted (PublicKey const& publicKey) = 0;

    //--------------------------------------------------------------------------

    /** Called when a validation with a proper signature is received.
    */
    virtual void receiveValidation (ReceivedValidation const& rv) = 0;
};

}

#endif
