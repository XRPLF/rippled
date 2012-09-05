#ifndef __INTERPRETER__
#define __INTERPRETER__

#include "uint256.h"
#include "Contract.h"
#include <boost/shared_ptr.hpp>
#include <vector>
#include "ScriptData.h"
#include "TransactionEngine.h"

namespace Script {

class Interpreter;

// Contracts are non typed  have variable data types


class Operation
{
public:
	// returns false if there was an error
	virtual bool work(Interpreter* interpreter)=0;

	virtual int getFee();
};

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
	enum {  INT_OP,FLOAT_OP,UINT160_OP,BOOL_OP,PATH_OP,
			ADD_OP,SUB_OP,MUL_OP,DIV_OP,MOD_OP,
			GTR_OP,LESS_OP,EQUAL_OP,NOT_EQUAL_OP,
			AND_OP,OR_OP,NOT_OP,
			BLOCK_OP, BLOCK_END_OP,
			JUMP_OP, JUMPIF_OP,
			STOP_OP,
			SET_DATA_OP,GET_DATA_OP, GET_ISSUER_ID_OP, GET_OWNER_ID_OP, GET_LEDGER_TIME_OP, 
			ACCEPT_DATA_OP,
			SEND_XNS_OP, NUM_OF_OPS };

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