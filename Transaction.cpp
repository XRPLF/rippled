#include "Transaction.h"
#include "BitcoinUtil.h"

using namespace std;


// sort by from address and then seqnum
bool gTransactionSorter(const TransactionPtr& lhs, const TransactionPtr& rhs)
{
	if(lhs->from() == rhs->from())
	{
		return(lhs->seqnum() < rhs->seqnum() );
	}else return lhs->from() < rhs->from();
}

// I don't think we need to bother checking the sig or public key
bool Transaction::isEqual(TransactionPtr t1,TransactionPtr t2)
{
	if(t1->amount() != t2->amount()) return(false);
	if(t1->seqnum() != t2->seqnum()) return(false);
	if(t1->ledgerindex() != t2->ledgerindex()) return(false);
	if(t1->from() != t2->from()) return(false);
	if(t1->dest() != t2->dest()) return(false);

	return(true);
}


uint256 Transaction::calcHash(TransactionPtr trans)
{
	vector<unsigned char> buffer;
	buffer.resize(trans->ByteSize());
	trans->SerializeToArray(&(buffer[0]),buffer.size());
	return Hash(buffer.begin(), buffer.end());
}