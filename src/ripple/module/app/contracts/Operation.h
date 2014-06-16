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

#ifndef OPERATION_H
#define OPERATION_H

namespace ripple {

namespace Script {

// Contracts are non typed  have variable data types


class Operation
{
public:
    // returns false if there was an error
    virtual bool work (Interpreter* interpreter) = 0;

    virtual int getFee ();

    virtual ~Operation ()
    {
        ;
    }
};

// this is just an Int in the code
class IntOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data = interpreter->getIntData ();

        if (data->isInt32 ())
        {
            interpreter->pushStack ( data );
            return (true);
        }

        return (false);
    }
};

class FloatOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data = interpreter->getFloatData ();

        if (data->isFloat ())
        {
            interpreter->pushStack ( data );
            return (true);
        }

        return (false);
    }
};

class Uint160Op : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data = interpreter->getUint160Data ();

        if (data->isUint160 ())
        {
            interpreter->pushStack ( data );
            return (true);
        }

        return (false);
    }
};

class AddOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( (data1->isInt32 () || data1->isFloat ()) &&
                (data2->isInt32 () || data2->isFloat ()) )
        {
            if (data1->isFloat () || data2->isFloat ()) interpreter->pushStack (Data::pointer (new FloatData (data1->getFloat () + data2->getFloat ())));
            else interpreter->pushStack (Data::pointer (new IntData (data1->getInt () + data2->getInt ())));

            return (true);
        }
        else
        {
            return (false);
        }
    }
};

class SubOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( (data1->isInt32 () || data1->isFloat ()) &&
                (data2->isInt32 () || data2->isFloat ()) )
        {
            if (data1->isFloat () || data2->isFloat ()) interpreter->pushStack (Data::pointer (new FloatData (data1->getFloat () - data2->getFloat ())));
            else interpreter->pushStack (Data::pointer (new IntData (data1->getInt () - data2->getInt ())));

            return (true);
        }
        else
        {
            return (false);
        }
    }
};

class MulOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( (data1->isInt32 () || data1->isFloat ()) &&
                (data2->isInt32 () || data2->isFloat ()) )
        {
            if (data1->isFloat () || data2->isFloat ()) interpreter->pushStack (Data::pointer (new FloatData (data1->getFloat ()*data2->getFloat ())));
            else interpreter->pushStack (Data::pointer (new IntData (data1->getInt ()*data2->getInt ())));

            return (true);
        }
        else
        {
            return (false);
        }
    }
};

class DivOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( (data1->isInt32 () || data1->isFloat ()) &&
                (data2->isInt32 () || data2->isFloat ()) )
        {
            if (data1->isFloat () || data2->isFloat ()) interpreter->pushStack (Data::pointer (new FloatData (data1->getFloat () / data2->getFloat ())));
            else interpreter->pushStack (Data::pointer (new IntData (data1->getInt () / data2->getInt ())));

            return (true);
        }
        else
        {
            return (false);
        }
    }
};

class GtrOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( (data1->isInt32 () || data1->isFloat ()) &&
                (data2->isInt32 () || data2->isFloat ()) )
        {
            interpreter->pushStack (Data::pointer (new BoolData (data1->getFloat () > data2->getFloat ())));
            return (true);
        }
        else
        {
            return (false);
        }
    }
};

class LessOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( (data1->isInt32 () || data1->isFloat ()) &&
                (data2->isInt32 () || data2->isFloat ()) )
        {
            interpreter->pushStack (Data::pointer (new FloatData (data1->getFloat () < data2->getFloat ())));
            return (true);
        }
        else
        {
            return (false);
        }
    }
};

class ModOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data1 = interpreter->popStack ();
        Data::pointer data2 = interpreter->popStack ();

        if ( data1->isInt32 () && data2->isInt32 () )
        {
            interpreter->pushStack (Data::pointer (new IntData (data1->getInt () % data2->getInt ())));
            return (true);
        }
        else
        {
            return (false);
        }
    }
};


class StartBlockOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer offset = interpreter->getIntData ();
        return (interpreter->startBlock (offset->getInt ()));
    }
};

class EndBlockOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        return (interpreter->endBlock ());
    }
};

class StopOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        interpreter->stop ();
        return (true);
    }
};

class AcceptDataOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer data = interpreter->popStack ();

        if (data->isInt32 ())
        {
            interpreter->pushStack ( interpreter->getAcceptData (data->getInt ()) );
            return (true);
        }

        return (false);
    }
};

class JumpIfOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer offset = interpreter->getIntData ();
        Data::pointer cond = interpreter->popStack ();

        if (cond->isBool () && offset->isInt32 ())
        {
            if (cond->isTrue ())
            {
                return (interpreter->jumpTo (offset->getInt ()));
            }

            return (true);
        }

        return (false);
    }
};

class JumpOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer offset = interpreter->getIntData ();

        if (offset->isInt32 ())
        {
            return (interpreter->jumpTo (offset->getInt ()));
        }

        return (false);
    }
};

class SendXRPOp  : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer sourceID = interpreter->popStack ();
        Data::pointer destID = interpreter->popStack ();
        Data::pointer amount = interpreter->popStack ();

        if (sourceID->isUint160 () && destID->isUint160 () && amount->isInt32 () && interpreter->canSign (sourceID->getUint160 ()))
        {
            // make sure:
            // source is either, this contract, issuer, or acceptor

            // TODO do the send
            //interpreter->pushStack( send result);

            return (true);
        }

        return (false);
    }
};

class GetDataOp : public Operation
{
public:
    bool work (Interpreter* interpreter)
    {
        Data::pointer index = interpreter->popStack ();

        if (index->isInt32 ())
        {
            interpreter->pushStack ( interpreter->getContractData (index->getInt ()));
            return (true);
        }

        return (false);
    }
};

}

} // ripple

#endif
