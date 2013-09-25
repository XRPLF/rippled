//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATORS_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATORS_H_INCLUDED

/** Maintains the list of chosen validators.

    The algorithm for acquiring, building, and calculating metadata on
    the list of chosen validators is critical to the health of the network.

    All operations are performed asynchronously on an internal thread.
*/
class Validators : public Uncopyable
{
public:
    typedef RipplePublicKeyHash KeyType;

    //--------------------------------------------------------------------------

    /** A source of validator descriptors. */
    class Source
    {
    public:
        /** A Source's descriptor for a Validator. */
        struct Info
        {
            /** The unique key for this validator. */
            KeyType key;
        };

        /** Destroy the Source.
            This can be called from any thread. If the Source is busy
            fetching, the destructor must block until the operation is either
            canceled or complete.
        */
        virtual ~Source () { }

        struct CancelCallback
        {
            virtual bool shouldCancel () = 0;
        };

        /** Fetch the most recent list from the Source.
            If possible, the Source should periodically poll the
            CancelCallback, and abort the operation if shouldCancel
            returns `true`.
            This call will block.
        */
        struct Result
        {
            Result ();
            void swapWith (Result& other);

            bool success;
            String message;
            Time expirationTime;
            Array <Info> list;
        };
        virtual Result fetch (CancelCallback& callback) = 0;
    };

    //--------------------------------------------------------------------------

    /** Create a new Validators object.
    */
    static Validators* New ();

    /** Destroy the object.

        Any pending source fetch operations are aborted.

        There may be some listener calls made before the
        destructor returns.
    */
    virtual ~Validators () { }

    /** Add a static source of validators from a string array. */
    /** @{ */
    virtual void addStrings (std::vector <std::string> const& strings) = 0;
    virtual void addStrings (StringArray const& stringArray) = 0;
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

    //virtual bool isPublicKeyTrusted (Validator::PublicKey const&) = 0;

    //--------------------------------------------------------------------------

    struct ReceivedValidation
    {
        uint256 ledgerHash;
        RipplePublicKeyHash signerPublicKeyHash;
    };

    /** Called when a validation with a proper signature is received.
    */
    virtual void receiveValidation (ReceivedValidation const& rv) = 0;
};

#endif
