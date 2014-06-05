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

#ifndef SCRIPT_DATA_H
#define SCRIPT_DATA_H

namespace ripple {

namespace Script
{
class Data
{
public:
    typedef std::shared_ptr<Data> pointer;

    virtual ~Data ()
    {
        ;
    }

    virtual bool isInt32 ()
    {
        return (false);
    }
    virtual bool isFloat ()
    {
        return (false);
    }
    virtual bool isUint160 ()
    {
        return (false);
    }
    virtual bool isError ()
    {
        return (false);
    }
    virtual bool isTrue ()
    {
        return (false);
    }
    virtual bool isBool ()
    {
        return (false);
    }
    //virtual bool isBlockEnd(){ return(false); }

    virtual int getInt ()
    {
        return (0);
    }
    virtual float getFloat ()
    {
        return (0);
    }
    virtual uint160 getUint160 ()
    {
        return uint160(0);
    }

    //virtual bool isCurrency(){ return(false); }
};

class IntData : public Data
{
    int mValue;
public:
    IntData (int value)
    {
        mValue = value;
    }
    bool isInt32 ()
    {
        return (true);
    }
    int getInt ()
    {
        return (mValue);
    }
    float getFloat ()
    {
        return ((float)mValue);
    }
    bool isTrue ()
    {
        return (mValue != 0);
    }
};

class FloatData : public Data
{
    float mValue;
public:
    FloatData (float value)
    {
        mValue = value;
    }
    bool isFloat ()
    {
        return (true);
    }
    float getFloat ()
    {
        return (mValue);
    }
    bool isTrue ()
    {
        return (mValue != 0);
    }
};

class Uint160Data : public Data
{
    uint160 mValue;
public:
    Uint160Data (uint160 value) : mValue (value)
    {
        ;
    }
    bool isUint160 ()
    {
        return (true);
    }
    uint160 getUint160 ()
    {
        return (mValue);
    }
};

class BoolData : public Data
{
    bool mValue;
public:
    BoolData (bool value)
    {
        mValue = value;
    }
    bool isBool ()
    {
        return (true);
    }
    bool isTrue ()
    {
        return (mValue);
    }
};

class ErrorData : public Data
{
public:
    bool isError ()
    {
        return (true);
    }
};

class BlockEndData : public Data
{
public:
    bool isBlockEnd ()
    {
        return (true);
    }
};


}

} // ripple

#endif
