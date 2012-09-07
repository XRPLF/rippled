#ifndef __INTERPRETER__
#define __INTERPRETER__

#include "uint256.h"
#include "Contract.h"
#include <boost/shared_ptr.hpp>
#include <vector>
#include "ScriptData.h"
#include "TransactionEngine.h"

namespace Script {

class Operation;
// Contracts are non typed  have variable data types

class Interpreter
{
	std::vector<Operation*> mFunctionTable;

	std::vector<Data::pointer> mStack;

	Contract* mContract;
	std::vector<unsigned char>* mCode;
	unsigned int mInstructionPointer;
	int mTotalFee;

	bool mInBlock;
	int mBlockJump;
	bool mBlockSuccess;

public:


	enum {  INT_OP=1,FLOAT_OP,UINT160_OP,BOOL_OP,PATH_OP,
			ADD_OP,SUB_OP,MUL_OP,DIV_OP,MOD_OP,
			GTR_OP,LESS_OP,EQUAL_OP,NOT_EQUAL_OP,
			AND_OP,OR_OP,NOT_OP,
			JUMP_OP, JUMPIF_OP,
			STOP_OP, CANCEL_OP,

			BLOCK_OP, BLOCK_END_OP,
			SEND_XNS_OP,SEND_OP,REMOVE_CONTRACT_OP,FEE_OP,CHANGE_CONTRACT_OWNER_OP, 
			STOP_REMOVE_OP,
			SET_DATA_OP,GET_DATA_OP, GET_NUM_DATA_OP,
			SET_REGISTER_OP,GET_REGISTER_OP,
			GET_ISSUER_ID_OP, GET_OWNER_ID_OP, GET_LEDGER_TIME_OP,  GET_LEDGER_NUM_OP, GET_RAND_FLOAT_OP,
			GET_XNS_ESCROWED_OP, GET_RIPPLE_ESCROWED_OP, GET_RIPPLE_ESCROWED_CURRENCY_OP, GET_RIPPLE_ESCROWED_ISSUER,
			GET_ACCEPT_DATA_OP, GET_ACCEPTOR_ID_OP, GET_CONTRACT_ID_OP,
			NUM_OF_OPS };

	Interpreter();

	// returns a TransactionEngineResult  
	TER interpret(Contract* contract,const SerializedTransaction& txn,std::vector<unsigned char>& code);

	void stop();

	bool canSign(uint160& signer);

	int getInstructionPointer(){ return(mInstructionPointer); }
	void setInstructionPointer(int n){ mInstructionPointer=n;}

	Data::pointer popStack();
	void pushStack(Data::pointer data);
	bool jumpTo(int offset);

	bool startBlock(int offset);
	bool endBlock();

	
	Data::pointer getIntData();
	Data::pointer getFloatData();
	Data::pointer getUint160Data();
	Data::pointer getAcceptData(int index);
	Data::pointer getContractData(int index);


};

}  // end namespace

#endif