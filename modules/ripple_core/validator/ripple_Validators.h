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
    class Listener
    {
    public:
        //virtual void onValidatorsChosen (ValidatorList validators) { }
    };

public:
    static Validators* New (Listener* listener);

    virtual ~Validators () { }

    virtual void addTrustedUri (String uri) = 0;

    virtual void start () = 0;
};

#endif
