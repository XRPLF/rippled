#ifndef __TRANSACTION__
#define __TRANSACTION__

#include "newcoin.pb.h"
#include "uint256.h"
#include <boost/shared_ptr.hpp>

/*
We could have made something that inherited from the protobuf transaction but this seemed simpler
*/

typedef boost::shared_ptr<newcoin::Transaction> TransactionPtr;

class Transaction
{
public:
	static bool isSigValid(TransactionPtr trans);
	static bool isEqual(TransactionPtr t1, TransactionPtr t2);
	static uint256 calcHash(TransactionPtr trans);

	static uint256 protobufToInternalHash(const std::string& hash);
};


extern bool gTransactionSorter(const TransactionPtr& lhs, const TransactionPtr& rhs);

#endif