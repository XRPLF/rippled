#include "Interpreter.h"
#include "Config.h"

/*
We also need to charge for each op

*/

namespace Script {


//////////////////////////////////////////////////////////////////////////
/// Operations

int Operation::getFee()
{
	return(theConfig.FEE_CONTRACT_OPERATION);
}

// this is just an Int in the code
class IntOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer data=interpreter->getIntData();
		if(data->isInt32()) 
		{ 
			interpreter->pushStack( data );
			return(true);
		}
		return(false);
	}
};

class FloatOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer data=interpreter->getFloatData();
		if(data->isFloat()) 
		{ 
			interpreter->pushStack( data );
			return(true);
		}
		return(false);
	}
};

class Uint160Op : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer data=interpreter->getUint160Data();
		if(data->isUint160()) 
		{ 
			interpreter->pushStack( data );
			return(true);
		}
		return(false);
	}
};

class AddOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer data1=interpreter->popStack();
		Data::pointer data2=interpreter->popStack();
		if( (data1->isInt32() || data1->isFloat()) &&
			(data2->isInt32() || data2->isFloat()) )
		{
			if(data1->isFloat() || data2->isFloat()) interpreter->pushStack(Data::pointer(new FloatData(data1->getFloat()+data2->getFloat())));
			else interpreter->pushStack(Data::pointer(new IntData(data1->getInt()+data2->getInt())));
			return(true);
		}else
		{
			return(false);
		}	
	}
};

class SubOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer data1=interpreter->popStack();
		Data::pointer data2=interpreter->popStack();
		if( (data1->isInt32() || data1->isFloat()) &&
			(data2->isInt32() || data2->isFloat()) )
		{
			if(data1->isFloat() || data2->isFloat()) interpreter->pushStack(Data::pointer(new FloatData(data1->getFloat()-data2->getFloat())));
			else interpreter->pushStack(Data::pointer(new IntData(data1->getInt()-data2->getInt())));
			return(true);
		}else
		{
			return(false);
		}	
	}
};


class StartBlockOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer offset=interpreter->getIntData();
		return(interpreter->startBlock(offset->getInt()));
	}
};

class EndBlockOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		return(interpreter->endBlock());
	}
};

class StopOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		interpreter->stop();
		return(true);
	}
};

class AcceptDataOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer data=interpreter->popStack();
		if(data->isInt32())
		{
			interpreter->pushStack( interpreter->getAcceptData(data->getInt()) );
			return(true);
		}
		return(false);
	}
};

class JumpIfOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer offset=interpreter->getIntData();
		Data::pointer cond=interpreter->popStack();
		if(cond->isBool() && offset->isInt32())
		{
			if(cond->isTrue())
			{
				return(interpreter->jumpTo(offset->getInt()));
			}
			return(true);
		}
		return(false);
	}
};

class JumpOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer offset=interpreter->getIntData();
		if(offset->isInt32())
		{
			return(interpreter->jumpTo(offset->getInt()));
		}
		return(false);
	}
};

class SendXNSOp  : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer sourceID=interpreter->popStack();
		Data::pointer destID=interpreter->popStack();
		Data::pointer amount=interpreter->popStack();
		if(sourceID->isUint160() && destID->isUint160() && amount->isInt32() && interpreter->canSign(sourceID->getUint160()))
		{
			// make sure:
			// source is either, this contract, issuer, or acceptor

			// TODO do the send
			//interpreter->pushStack( send result);

			return(true);
		}
		
		return(false);
	}
};

class GetDataOp : public Operation
{
public:
	bool work(Interpreter* interpreter)
	{
		Data::pointer index=interpreter->popStack();
		if(index->isInt32()) 
		{
			interpreter->pushStack( interpreter->getContractData(index->getInt()));
			return(true);
		}
		
		return(false);
	}
};

//////////////////////////////////////////////////////////////////////////

Interpreter::Interpreter()
{
	mContract=NULL;
	mCode=NULL;
	mInstructionPointer=0;
	mTotalFee=0;

	mInBlock=false;
	mBlockSuccess=true;
	mBlockJump=0;

	mFunctionTable.resize(NUM_OF_OPS);

	mFunctionTable[INT_OP]=new IntOp();
	mFunctionTable[FLOAT_OP]=new FloatOp();
	mFunctionTable[UINT160_OP]=new Uint160Op();

	mFunctionTable[ADD_OP]=new AddOp();
	mFunctionTable[SUB_OP]=new SubOp();

}

Data::pointer Interpreter::popStack()
{
	if(mStack.size())
	{
		Data::pointer item=mStack[mStack.size()-1];
		mStack.pop_back();
		return(item);
	}else
	{
		return(Data::pointer(new ErrorData()));
	}
}

void Interpreter::pushStack(Data::pointer data)
{
	mStack.push_back(data);
}


// offset is where to jump to if the block fails
bool Interpreter::startBlock(int offset)
{
	if(mInBlock) return(false); // can't nest blocks
	mBlockSuccess=true;
	mInBlock=true;
	mBlockJump=offset+mInstructionPointer;
	return(true);
}

bool Interpreter::endBlock()
{
	if(!mInBlock) return(false);
	mInBlock=false;
	mBlockJump=0;
	pushStack(Data::pointer(new BoolData(mBlockSuccess)));
	return(true);
}

TER Interpreter::interpret(Contract* contract,const SerializedTransaction& txn,std::vector<unsigned char>& code)
{
	mContract=contract;
	mCode=&code;
	mTotalFee=0;
	mInstructionPointer=0;
	while(mInstructionPointer<code.size())
	{
		unsigned int fun=(*mCode)[mInstructionPointer];
		mInstructionPointer++;

		if(fun>=mFunctionTable.size()) 
		{
			// TODO: log
			return(temMALFORMED);  // TODO: is this actually what we want to do?
		}

		mTotalFee += mFunctionTable[ fun ]->getFee();
		if(mTotalFee>txn.getTransactionFee().getNValue())
		{
			// TODO: log
			return(telINSUF_FEE_P);
		}else 
		{
			if(!mFunctionTable[ fun ]->work(this))
			{
				// TODO: log
				return(temMALFORMED);  // TODO: is this actually what we want to do?
			}
		}
	}
	return(tesSUCCESS);
}


Data::pointer Interpreter::getIntData()
{
	int value=0; // TODO
	mInstructionPointer += 4;
	return(Data::pointer(new IntData(value)));
}

Data::pointer Interpreter::getFloatData()
{
	float value=0; // TODO
	mInstructionPointer += 4;
	return(Data::pointer(new FloatData(value)));
}

Data::pointer Interpreter::getUint160Data()
{
	uint160 value; // TODO
	mInstructionPointer += 20;
	return(Data::pointer(new Uint160Data(value)));
}



bool Interpreter::jumpTo(int offset)
{
	mInstructionPointer += offset;
	if( (mInstructionPointer<0) || (mInstructionPointer>mCode->size()) )
	{
		mInstructionPointer -= offset;
		return(false);
	}
	return(true);
}

void Interpreter::stop()
{
	mInstructionPointer=mCode->size();
}

Data::pointer Interpreter::getContractData(int index)
{
	return(Data::pointer(new ErrorData()));
}




} // end namespace
