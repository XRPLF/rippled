//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_H_INCLUDED
#define RIPPLE_VALIDATORS_H_INCLUDED

/** Maintains the list of chosen validators.

    The algorithm for acquiring, building, and calculating metadata on
    the list of chosen validators is critical to the health of the network.

    All operations are performed asynchronously on an internal thread.
*/
class Validators : Uncopyable
{
public:
    /** Provides a ValidatorList.
    */
    class Source
    {
    public:
        /** Destroy the source.

            If a fetch is active, it will be aborted before the
            destructor returns.
        */
        virtual ~Source () { }

        /** Fetch the validator list from this source.

            This call blocks.
        */
        virtual Array <Validator::Info> fetch () =0;
    };

    //--------------------------------------------------------------------------

    /** Receive event notifications on Validators operations.
    */
    class Listener
    {
    public:
        virtual void onValidatorsChosen (Validator::List::Ptr list) { }
    };

    //--------------------------------------------------------------------------

    /** Create a new Validators object.
    */
    static Validators* New (Listener* listener);

    /** Destroy the object.

        Any pending source fetch operations are aborted.

        There may be some listener calls made before the
        destructor returns.
    */
    virtual ~Validators () { }

    /** Add a source of validators.
    */
    virtual void addSource (Source* source) = 0;
};

#endif
