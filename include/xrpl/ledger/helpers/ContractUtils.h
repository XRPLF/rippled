#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <map>

namespace xrpl {

class ContractDataMap : public std::map<xrpl::AccountID, std::pair<bool, STJson>>
{
public:
    uint32_t modifiedCount = 0;
};

class ContractEventMap : public std::map<std::string, STJson>
{
};

namespace contract {

/** The maximum number of data modifications in a single function. */
int64_t constexpr maxDataModifications = 1000;

/** The maximum number of bytes the data can occupy. */
int64_t constexpr maxContractDataSize = 1024;

/** The multiplier for contract data size calculations. */
int64_t constexpr dataByteMultiplier = 512;

/** The cost multiplier of creating a contract in bytes. */
int64_t constexpr createByteMultiplier = 500ULL;

/** The value to return when the fee calculation failed. */
int64_t constexpr feeCalculationFailed = 0x7FFFFFFFFFFFFFFFLL;

/** The maximum number of contract parameters that can be in a transaction. */
std::size_t constexpr maxContractParams = 8;

/** The maximum number of contract functions that can be in a transaction. */
std::size_t constexpr maxContractFunctions = 8;

int64_t
contractCreateFee(uint64_t byteCount);

NotTEC
preflightFunctions(STTx const& tx, beast::Journal j);

NotTEC
preflightInstanceParameters(STTx const& tx, beast::Journal j);

bool
validateParameterMapping(STArray const& params, STArray const& values, beast::Journal j);

NotTEC
preflightInstanceParameterValues(STTx const& tx, beast::Journal j);

NotTEC
preflightFlagParameters(STArray const& parameters, beast::Journal j);

bool
isValidParameterFlag(std::uint32_t flags);

TER
preclaimFlagParameters(
    ReadView const& view,
    AccountID const& sourceAccount,
    AccountID const& contractAccount,
    STArray const& parameters,
    beast::Journal j);

TER
doApplyFlagParameters(
    ApplyView& view,
    STTx const& tx,
    AccountID const& sourceAccount,
    AccountID const& contractAccount,
    STArray const& parameters,
    XRPAmount const& priorBalance,
    beast::Journal j);

TER
finalizeContractData(
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& contractAccount,
    ContractDataMap const& dataMap,
    ContractEventMap const& eventMap,
    uint256 const& txnID);

}  // namespace contract
}  // namespace xrpl
