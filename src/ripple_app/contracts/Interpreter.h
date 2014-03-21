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

#ifndef INTERPRETER_H
#define INTERPRETER_H

namespace ripple {

namespace Script {

class Operation;
// Contracts are non typed  have variable data types

class Interpreter
{
    std::vector<Operation*> mFunctionTable;

    std::vector<Data::pointer> mStack;

    Contract* mContract;
    Blob* mCode;
    unsigned int mInstructionPointer;
    int mTotalFee;

    bool mInBlock;
    int mBlockJump;
    bool mBlockSuccess;

public:


    enum {  INT_OP = 1, FLOAT_OP, UINT160_OP, BOOL_OP, PATH_OP,
            ADD_OP, SUB_OP, MUL_OP, DIV_OP, MOD_OP,
            GTR_OP, LESS_OP, EQUAL_OP, NOT_EQUAL_OP,
            AND_OP, OR_OP, NOT_OP,
            JUMP_OP, JUMPIF_OP,
            STOP_OP, CANCEL_OP,

            BLOCK_OP, BLOCK_END_OP,
            SEND_XRP_OP, SEND_OP, REMOVE_CONTRACT_OP, FEE_OP, CHANGE_CONTRACT_OWNER_OP,
            STOP_REMOVE_OP,
            SET_DATA_OP, GET_DATA_OP, GET_NUM_DATA_OP,
            SET_REGISTER_OP, GET_REGISTER_OP,
            GET_ISSUER_ID_OP, GET_OWNER_ID_OP, GET_LEDGER_TIME_OP,  GET_LEDGER_NUM_OP, GET_RAND_FLOAT_OP,
            GET_XRP_ESCROWED_OP, GET_RIPPLE_ESCROWED_OP, GET_RIPPLE_ESCROWED_CURRENCY_OP, GET_RIPPLE_ESCROWED_ISSUER,
            GET_ACCEPT_DATA_OP, GET_ACCEPTOR_ID_OP, GET_CONTRACT_ID_OP,
            NUM_OF_OPS
         };

    Interpreter ();

    // returns a TransactionEngineResult
    TER interpret (Contract* contract, const SerializedTransaction& txn, Blob& code);

    void stop ();

    bool canSign (const uint160& signer);

    int getInstructionPointer ()
    {
        return (mInstructionPointer);
    }
    void setInstructionPointer (int n)
    {
        mInstructionPointer = n;
    }

    Data::pointer popStack ();
    void pushStack (Data::pointer data);
    bool jumpTo (int offset);

    bool startBlock (int offset);
    bool endBlock ();

    Data::pointer getIntData ();
    Data::pointer getFloatData ();
    Data::pointer getUint160Data ();
    Data::pointer getAcceptData (int index);
    Data::pointer getContractData (int index);
};

}  // end namespace

} // ripple

#endif
