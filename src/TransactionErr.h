#ifndef _TRANSACTION_ERR_
#define _TRANSACTION_ERR_

#include <string>

enum TER	// aka TransactionEngineResult
{
	// Note: Range is stable.  Exact numbers are currently unstable.  Use tokens.

	// -399 .. -300: L Local error (transaction fee inadequate, exceeds local limit)
	// Only valid during non-consensus processing.
	// Implications:
	// - Not forwarded
	// - No fee check
	telLOCAL_ERROR	= -399,
	telBAD_PATH_COUNT,
	telINSUF_FEE_P,

	// -299 .. -200: M Malformed (bad signature)
	// Causes:
	// - Transaction corrupt.
	// Implications:
	// - Not applied
	// - Not forwarded
	// - Reject
	// - Can not succeed in any imagined ledger.
	temMALFORMED	= -299,
	temBAD_AMOUNT,
	temBAD_AUTH_MASTER,
	temBAD_EXPIRATION,
	temBAD_ISSUER,
	temBAD_OFFER,
	temBAD_PATH,
	temBAD_PATH_LOOP,
	temBAD_PUBLISH,
	temBAD_TRANSFER_RATE,
	temBAD_SET_ID,
	temCREATEXNS,
	temDST_IS_SRC,
	temDST_NEEDED,
	temINSUF_FEE_P,
	temINVALID,
	temREDUNDANT,
	temRIPPLE_EMPTY,
	temUNCERTAIN,
	temUNKNOWN,

	// -199 .. -100: F Failure (sequence number previously used)
	// Causes:
	// - Transaction cannot succeed because of ledger state.
	// - Unexpected ledger state.
	// - C++ exception.
	// Implications:
	// - Not applied
	// - Not forwarded
	// - Could succeed in an imagined ledger.
	tefFAILURE		= -199,
	tefALREADY,
	tefBAD_ADD_AUTH,
	tefBAD_AUTH,
	tefBAD_CLAIM_ID,
	tefBAD_GEN_AUTH,
	tefBAD_LEDGER,
	tefCLAIMED,
	tefCREATED,
	tefEXCEPTION,
	tefGEN_IN_USE,
	tefPAST_SEQ,

	// -99 .. -1: R Retry (sequence too high, no funds for txn fee, originating account non-existent)
	// Causes:
	// - Priror application of another, possibly non-existant, transaction could allow this transaction to succeed.
	// Implications:
	// - Not applied
	// - Not forwarded
	// - Might succeed later
	// - Hold
	terRETRY		= -99,
	terDIR_FULL,
	terFUNDS_SPENT,
	terINSUF_FEE_B,
	terNO_ACCOUNT,
	terNO_DST,
	terNO_LINE,
	terNO_LINE_NO_ZERO,
	terOFFER_NOT_FOUND, // XXX If we check sequence first this could be hard failure.
	terPRE_SEQ,
	terSET_MISSING_DST,
	terUNFUNDED,

	// 0: S Success (success)
	// Causes:
	// - Success.
	// Implications:
	// - Applied
	// - Forwarded
	tesSUCCESS		= 0,

	// 100 .. P Partial success (SR) (ripple transaction with no good paths, pay to non-existent account)
	// Causes:
	// - Success, but does not achieve optimal result.
	// Implications:
	// - Applied
	// - Forwarded
	// Only allowed as a return code of appliedTransaction when !tapRetry. Otherwise, treated as terRETRY.
	tepPARTIAL		= 100,
	tepPATH_DRY,
	tepPATH_PARTIAL,
};

#define isTemMalformed(x)	((x) >= temMALFORMED && (x) < tefFAILURE)
#define isTefFailure(x)		((x) >= tefFAILURE && (x) < terRETRY)
#define isTepPartial(x)		((x) >= tepPATH_PARTIAL)
#define isTepSuccess(x)		((x) >= tesSUCCESS)
#define isTerRetry(x)		((x) >= terRETRY && (x) < tesSUCCESS)

bool transResultInfo(TER terCode, std::string& strToken, std::string& strHuman);
std::string transToken(TER terCode);
std::string transHuman(TER terCode);

#endif
