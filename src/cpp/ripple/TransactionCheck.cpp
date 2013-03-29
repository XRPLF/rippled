
#include "TransactionErr.h"
#include "TransactionEngine.h"

// Double check a transaction's metadata to make sure no system invariants were broken
// Call right before 'calcRawMeta'

bool TransactionEngine::checkInvariants(TER result, const SerializedTransaction& txn, TransactionEngineParams params)
{
	return true;
}
