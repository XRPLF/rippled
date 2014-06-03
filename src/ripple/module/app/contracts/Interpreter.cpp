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

namespace ripple {

namespace Script
{

Interpreter::Interpreter ()
{
    mContract = nullptr;
    mCode = nullptr;
    mInstructionPointer = 0;
    mTotalFee = 0;

    mInBlock = false;
    mBlockSuccess = true;
    mBlockJump = 0;

    mFunctionTable.resize (NUM_OF_OPS);

    mFunctionTable[INT_OP] = new IntOp ();
    mFunctionTable[FLOAT_OP] = new FloatOp ();
    mFunctionTable[UINT160_OP] = new Uint160Op ();
    mFunctionTable[BOOL_OP] = new Uint160Op ();
    mFunctionTable[PATH_OP] = new Uint160Op ();

    mFunctionTable[ADD_OP] = new AddOp ();
    mFunctionTable[SUB_OP] = new SubOp ();
    mFunctionTable[MUL_OP] = new MulOp ();
    mFunctionTable[DIV_OP] = new DivOp ();
    mFunctionTable[MOD_OP] = new ModOp ();
    mFunctionTable[GTR_OP] = new GtrOp ();
    mFunctionTable[LESS_OP] = new LessOp ();
    mFunctionTable[EQUAL_OP] = new SubOp ();
    mFunctionTable[NOT_EQUAL_OP] = new SubOp ();
    mFunctionTable[AND_OP] = new SubOp ();
    mFunctionTable[OR_OP] = new SubOp ();
    mFunctionTable[NOT_OP] = new SubOp ();
    mFunctionTable[JUMP_OP] = new SubOp ();
    mFunctionTable[JUMPIF_OP] = new SubOp ();
    mFunctionTable[STOP_OP] = new SubOp ();
    mFunctionTable[CANCEL_OP] = new SubOp ();
    mFunctionTable[BLOCK_OP] = new SubOp ();
    mFunctionTable[BLOCK_END_OP] = new SubOp ();
    mFunctionTable[SEND_XRP_OP] = new SendXRPOp ();
    /*
    mFunctionTable[SEND_OP]=new SendOp();
    mFunctionTable[REMOVE_CONTRACT_OP]=new SubOp();
    mFunctionTable[FEE_OP]=new SubOp();
    mFunctionTable[CHANGE_CONTRACT_OWNER_OP]=new SubOp();
    mFunctionTable[STOP_REMOVE_OP]=new SubOp();
    mFunctionTable[SET_DATA_OP]=new SubOp();
    mFunctionTable[GET_DATA_OP]=new SubOp();
    mFunctionTable[GET_NUM_DATA_OP]=new SubOp();
    mFunctionTable[SET_REGISTER_OP]=new SubOp();
    mFunctionTable[GET_REGISTER_OP]=new SubOp();
    mFunctionTable[GET_ISSUER_ID_OP]=new SubOp();
    mFunctionTable[GET_OWNER_ID_OP]=new SubOp();
    mFunctionTable[GET_LEDGER_TIME_OP]=new SubOp();
    mFunctionTable[GET_LEDGER_NUM_OP]=new SubOp();
    mFunctionTable[GET_RAND_FLOAT_OP]=new SubOp();
    mFunctionTable[GET_XRP_ESCROWED_OP]=new SubOp();
    mFunctionTable[GET_RIPPLE_ESCROWED_OP]=new SubOp();
    mFunctionTable[GET_RIPPLE_ESCROWED_CURRENCY_OP]=new SubOp();
    mFunctionTable[GET_RIPPLE_ESCROWED_ISSUER]=new GetRippleEscrowedIssuerOp();
    mFunctionTable[GET_ACCEPT_DATA_OP]=new AcceptDataOp();
    mFunctionTable[GET_ACCEPTOR_ID_OP]=new GetAcceptorIDOp();
    mFunctionTable[GET_CONTRACT_ID_OP]=new GetContractIDOp();
    */

}

Data::pointer Interpreter::popStack ()
{
    if (mStack.size ())
    {
        Data::pointer item = mStack[mStack.size () - 1];
        mStack.pop_back ();
        return (item);
    }
    else
    {
        return (Data::pointer (new ErrorData ()));
    }
}

void Interpreter::pushStack (Data::pointer data)
{
    mStack.push_back (data);
}


// offset is where to jump to if the block fails
bool Interpreter::startBlock (int offset)
{
    if (mInBlock) return (false); // can't nest blocks

    mBlockSuccess = true;
    mInBlock = true;
    mBlockJump = offset + mInstructionPointer;
    return (true);
}

bool Interpreter::endBlock ()
{
    if (!mInBlock) return (false);

    mInBlock = false;
    mBlockJump = 0;
    pushStack (Data::pointer (new BoolData (mBlockSuccess)));
    return (true);
}

TER Interpreter::interpret (Contract* contract, const SerializedTransaction& txn, Blob& code)
{
    mContract = contract;
    mCode = &code;
    mTotalFee = 0;
    mInstructionPointer = 0;

    while (mInstructionPointer < code.size ())
    {
        unsigned int fun = (*mCode)[mInstructionPointer];
        mInstructionPointer++;

        if (fun >= mFunctionTable.size ())
        {
            // TODO: log
            return (temMALFORMED); // TODO: is this actually what we want to do?
        }

        mTotalFee += mFunctionTable[ fun ]->getFee (); // FIXME: You can't use fees this way, there's no consensus

        if (mTotalFee > txn.getTransactionFee ().getNValue ())
        {
            // TODO: log
            return (telINSUF_FEE_P);
        }
        else
        {
            if (!mFunctionTable[ fun ]->work (this))
            {
                // TODO: log
                return (temMALFORMED); // TODO: is this actually what we want to do?
            }
        }
    }

    return (tesSUCCESS);
}


Data::pointer Interpreter::getIntData ()
{
    int value = 0; // TODO
    mInstructionPointer += 4;
    return (Data::pointer (new IntData (value)));
}

Data::pointer Interpreter::getFloatData ()
{
    float value = 0; // TODO
    mInstructionPointer += 4;
    return (Data::pointer (new FloatData (value)));
}

Data::pointer Interpreter::getUint160Data ()
{
    uint160 value; // TODO
    mInstructionPointer += 20;
    return (Data::pointer (new Uint160Data (value)));
}



bool Interpreter::jumpTo (int offset)
{
    if (offset < 0)
    {
        if (-offset > mInstructionPointer)
            return false;
    }
    else
    {
        if (offset > mCode->size () ||
                mInstructionPointer > mCode->size () - offset)
            return false;
    }

    mInstructionPointer += offset;
    return (true);
}

void Interpreter::stop ()
{
    mInstructionPointer = mCode->size ();
}

Data::pointer Interpreter::getContractData (int index)
{
    return (Data::pointer (new ErrorData ()));
}

bool Interpreter::canSign (const uint160& signer)
{
    return (true);
}

}

} // ripple
