//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_KEYVADB_H_INCLUDED
#define RIPPLE_KEYVADB_H_INCLUDED

/** Key/value database optimized for Ripple usage.
*/
class KeyvaDB : LeakChecked <KeyvaDB>
{
public:
    class GetCallback
    {
    public:
        virtual void* getStorageForValue (int valueBytes) = 0;
    };

    static KeyvaDB* New (int keyBytes,
                         File keyPath,
                         File valPath,
                         bool filesAreTemporary);

    virtual ~KeyvaDB () { }

    // VFALCO TODO Make the return value a Result so we can
    //             detect corruption and errors!
    //
    virtual bool get (void const* key, GetCallback* callback) = 0;

    // VFALCO TODO Use Result for return value
    //
    virtual void put (void const* key, void const* value, int valueBytes) = 0;

    virtual void flush () = 0;
};

#endif
