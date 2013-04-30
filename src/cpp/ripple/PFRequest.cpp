#include "PFRequest.h"

#include "NetworkOPs.h"
#include "RPCErr.h"
#include "Ledger.h"
#include "Application.h"

boost::recursive_mutex		PFRequest::sLock;
std::set<PFRequest::wptr>	PFRequest::sRequests;

PFRequest::PFRequest(const boost::shared_ptr<InfoSub>& subscriber) :
	wpSubscriber(subscriber), jvStatus(Json::objectValue), bValid(false)
{
	;
}

bool PFRequest::isValid()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return bValid;
}

bool PFRequest::isValid(Ledger::ref lrLedger)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	bValid = raSrcAccount.isSet() && raDstAccount.isSet() && saDstAmount.isPositive();
	if (bValid)
	{
		AccountState::pointer asSrc = theApp->getOPs().getAccountState(lrLedger, raSrcAccount);
		if (!asSrc)
		{ // no source account
			bValid = false;
			jvStatus = rpcError(rpcSRC_ACT_NOT_FOUND);
		}
		else
		{
			AccountState::pointer asDst = theApp->getOPs().getAccountState(lrLedger, raDstAccount);
			if (!asDst)
			{ // no destination account
				if(!saDstAmount.isNative())
				{ // only XRP can be send to a non-existent account
					bValid = false;
					jvStatus = rpcError(rpcACT_NOT_FOUND);
				}
				else if (saDstAmount < STAmount(lrLedger->getReserve(0)))
				{ // payment must meet reserve
					bValid = false;
					jvStatus = rpcError(rpcDST_AMT_MALFORMED);
				}
			}
		}
	}
	return bValid;
}

Json::Value PFRequest::doCreate(Ledger::ref lrLedger, const Json::Value& value)
{
	Json::Value status;
	bool mValid;

	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		parseJson(value, true);
		status = jvStatus;
		mValid = isValid(lrLedger);
	}

	if (mValid)
	{
		boost::recursive_mutex::scoped_lock sl(sLock);
		sRequests.insert(shared_from_this());
	}

	return jvStatus;
}

int PFRequest::parseJson(const Json::Value& jvParams, bool complete)
{
	int ret = PFR_PJ_NOCHANGE;

	if (jvParams.isMember("source_account"))
	{
		if (!raSrcAccount.setAccountID(jvParams["source_account"].asString()))
		{
			jvStatus = rpcError(rpcSRC_ACT_MALFORMED);
			return PFR_PJ_INVALID;
		}
	}
	else if (complete)
	{
		jvStatus = rpcError(rpcSRC_ACT_MISSING);
		return PFR_PJ_INVALID;
	}

	if (jvParams.isMember("destination_account"))
	{
		if (!raDstAccount.setAccountID(jvParams["source_account"].asString()))
		{
			jvStatus = rpcError(rpcDST_ACT_MALFORMED);
			return PFR_PJ_INVALID;
		}
	}
	else if (complete)
	{
		jvStatus = rpcError(rpcDST_ACT_MISSING);
		return PFR_PJ_INVALID;
	}

	if (jvParams.isMember("destination_amount"))
	{
		if (!saDstAmount.bSetJson(jvParams["destination_amount"]) ||
			(saDstAmount.getCurrency().isZero() && saDstAmount.getIssuer().isNonZero()) ||
			(saDstAmount.getCurrency() == CURRENCY_BAD))
		{
			jvStatus = rpcError(rpcDST_AMT_MALFORMED);
			return PFR_PJ_INVALID;
		}
	}
	else if (complete)
	{
		jvStatus = rpcError(rpcDST_ACT_MISSING);
		return PFR_PJ_INVALID;
	}

	if (jvParams.isMember("source_currencies"))
	{
		const Json::Value& jvSrcCur = jvParams["source_currencies"];
		if (!jvSrcCur.isArray())
		{
			jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
			return PFR_PJ_INVALID;
		}
		sciSourceCurrencies.clear();
		for (unsigned i = 0; i < jvSrcCur.size(); ++i)
		{
			const Json::Value& jvCur = jvSrcCur[i];
			uint160 uCur, uIss;
			if (!jvCur.isMember("currency") || !STAmount::currencyFromString(uCur, jvCur["currency"].asString()))
			{
				jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
				return PFR_PJ_INVALID;
			}
			if (jvCur.isMember("issuer") && !STAmount::issuerFromString(uIss, jvCur["issuer"].asString()))
			{
				jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
			}
			if (uCur.isZero() && uIss.isNonZero())
			{
				jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
				return PFR_PJ_INVALID;
			}
			sciSourceCurrencies.insert(currIssuer_t(uCur, uIss));
		}
	}

	return ret;
}
Json::Value PFRequest::doClose(const Json::Value&)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return jvStatus;
}

Json::Value PFRequest::doStatus(const Json::Value&)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return jvStatus;
}

// vim:ts=4
