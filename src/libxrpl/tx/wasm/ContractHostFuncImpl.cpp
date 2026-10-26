#include <xrpl/tx/wasm/ContractHostFuncImpl.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Emitable.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl {

// Serializes a value the way a later serializer appends it, without its field header.
template <class T>
static Bytes
serializedBytes(T const& value)
{
    Serializer s;
    value.add(s);
    auto const& serialized = s.getData();
    return Bytes{serialized.begin(), serialized.end()};
}

std::expected<Bytes, HostFunctionError>
getFieldBytesFromSTData(xrpl::STData const& funcParam, std::uint32_t stTypeId)
{
    // The contract names the type it expects; a parameter of another type is refused.
    if (funcParam.getInnerSType() != stTypeId)
        return std::unexpected(HostFunctionError::InvalidParams);

    switch (stTypeId)
    {
        case STI_UINT8:
            return Bytes{funcParam.getFieldU8()};
        case STI_UINT16: {
            uint16_t const data = funcParam.getFieldU16();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
            };
        }
        case STI_UINT32: {
            uint32_t const data = funcParam.getFieldU32();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF),
            };
        }
        case STI_UINT64: {
            uint64_t const data = funcParam.getFieldU64();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF),
                static_cast<unsigned char>((data >> 32) & 0xFF),
                static_cast<unsigned char>((data >> 40) & 0xFF),
                static_cast<unsigned char>((data >> 48) & 0xFF),
                static_cast<unsigned char>((data >> 56) & 0xFF),
            };
        }
        case STI_UINT128: {
            uint128 const data = funcParam.getFieldH128();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT160: {
            uint160 const data = funcParam.getFieldH160();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT192: {
            uint192 const data = funcParam.getFieldH192();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT256: {
            uint256 const data = funcParam.getFieldH256();
            return Bytes{data.begin(), data.end()};
        }
        case STI_VL: {
            auto const data = funcParam.getFieldVL();
            return Bytes{data.begin(), data.end()};
        }
        case STI_ACCOUNT: {
            AccountID const data = funcParam.getAccountID();
            return Bytes{data.begin(), data.end()};
        }
        case STI_AMOUNT:
            return serializedBytes(funcParam.getFieldAmount());
        case STI_NUMBER:
            return serializedBytes(funcParam.getFieldNumber());
        case STI_ISSUE:
            return serializedBytes(funcParam.getFieldIssue());
        case STI_CURRENCY:
            return serializedBytes(funcParam.getFieldCurrency());
        default:
            return std::unexpected(HostFunctionError::InvalidParams);
    }
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::instanceParam(std::uint32_t index, std::uint32_t stTypeId)
{
    auto const j = getJournal();
    auto const& instanceParams = contractCtx_.instanceParameters;

    if (instanceParams.size() <= index)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_
                        << "]: " << "instanceParam: Index out of bounds";
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    }

    xrpl::STData const& instParam = instanceParams[index].value;
    return getFieldBytesFromSTData(instParam, stTypeId);
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::functionParam(std::uint32_t index, std::uint32_t stTypeId)
{
    auto const j = getJournal();
    auto const& funcParams = contractCtx_.functionParameters;

    if (funcParams.size() <= index)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_
                        << "]: " << "functionParam: Index out of bounds";
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    }

    xrpl::STData const& funcParam = funcParams[index].value;
    return getFieldBytesFromSTData(funcParam, stTypeId);
}

// The account's data object: this call's cached copy, else the ledger's, which is then cached
// unmodified.
static std::expected<STJson, HostFunctionError>
getDataOrCache(ContractContext& contractCtx, AccountID const& account)
{
    auto const& view = contractCtx.applyCtx.view();
    if (!view.exists(keylet::account(account)))
        return std::unexpected(HostFunctionError::InvalidAccount);

    auto& dataMap = contractCtx.result.dataMap;
    if (auto const cached = dataMap.find(account); cached != dataMap.end())
        return cached->second.second;

    auto const dataSle =
        view.read(keylet::contractData(account, contractCtx.result.contractAccount));
    if (!dataSle)
        return std::unexpected(HostFunctionError::LedgerObjNotFound);

    STJson data = dataSle->getFieldJson(sfContractJson);
    dataMap[account] = {false, data};
    return data;
}

// What a write starts from: the account's data object, or an empty object when it has none.
static std::expected<STJson, HostFunctionError>
dataForWrite(ContractContext& contractCtx, AccountID const& account)
{
    auto data = getDataOrCache(contractCtx, account);
    if (!data && data.error() == HostFunctionError::LedgerObjNotFound)
        return STJson{};
    return data;
}

// Whether an array element at `index` fits a data object of the size limit: each element
// before it takes at least one byte, so a larger index never does.
static bool
holdableIndex(std::size_t index)
{
    return index < static_cast<std::size_t>(contract::maxContractDataSize);
}

// Records a write of the account's data object, which counts toward the modification limit,
// must fit the size limit and, unless it only removes, needs the owner to afford one more
// reserve increment.
static std::expected<void, HostFunctionError>
setDataCache(
    ContractContext& contractCtx,
    AccountID const& account,
    STJson const& data,
    beast::Journal const& j,
    bool removesOnly = false)
{
    auto& dataMap = contractCtx.result.dataMap;
    auto const& view = contractCtx.applyCtx.view();
    auto const contractId = contractCtx.result.contractKeylet.key;

    // dataForWrite has found the account.
    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
        return std::unexpected(HostFunctionError::InvalidAccount);  // LCOV_EXCL_LINE

    if (dataMap.modifiedCount >= contract::maxDataModifications)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataCache: Exceeded max data modifications";
        return std::unexpected(HostFunctionError::InvalidState);
    }

    if (data.size() > contract::maxContractDataSize)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: " << "setDataCache: Data too large";
        return std::unexpected(HostFunctionError::DataFieldTooLarge);
    }

    auto const& fees = view.fees();
    XRPAmount const spare = sleAccount->getFieldAmount(sfBalance).xrp() -
        fees.accountReserve(sleAccount->getFieldU32(sfOwnerCount), 1);
    if (!removesOnly && spare < std::max(fees.increment, XRPAmount{1}))
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId
                        << "]: " << "setDataCache: Insufficient reserve";
        return std::unexpected(HostFunctionError::InvalidState);
    }

    ++dataMap.modifiedCount;
    dataMap[account] = {true, data};
    return {};
}

// A stored field's serialization, or InvalidField when there is no such field.
static std::expected<Bytes, HostFunctionError>
fieldBytes(std::optional<STJson::Value> const& value)
{
    if (!value)
        return std::unexpected(HostFunctionError::InvalidField);
    Serializer s;
    (*value)->add(s);
    return Bytes{s.peekData().begin(), s.peekData().end()};
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataObjectField(AccountID const& account, std::string_view const& key)
{
    auto const data = getDataOrCache(contractCtx_, account);
    if (!data)
        return std::unexpected(data.error());
    return fieldBytes(data->getObjectField(std::string(key)));
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataNestedObjectField(
    AccountID const& account,
    std::string_view const& key,
    std::string_view const& nestedKey)
{
    auto const data = getDataOrCache(contractCtx_, account);
    if (!data)
        return std::unexpected(data.error());
    return fieldBytes(data->getNestedObjectField(std::string(key), std::string(nestedKey)));
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataObjectField(
    AccountID const& account,
    std::string_view const& key,
    STJson::Value const& value)
{
    auto const j = getJournal();
    try
    {
        auto loaded = dataForWrite(contractCtx_, account);
        if (!loaded)
            return std::unexpected(loaded.error());
        STJson& data = *loaded;
        if (!data.isObject())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataObjectField: Invalid state: not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        // Storing no value removes the key.
        if (value)
        {
            data.setObjectField(std::string(key), value);
        }
        else
        {
            data.eraseObjectField(std::string(key));
        }
        if (auto const ret = setDataCache(contractCtx_, account, data, j, !value); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataObjectField: Failed to set object field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        // STJson refuses a value its structure cannot hold.
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataObjectField: Invalid value: " << e.what();
        return std::unexpected(HostFunctionError::InvalidParams);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataNestedObjectField(
    AccountID const& account,
    std::string_view const& key,
    std::string_view const& nestedKey,
    STJson::Value const& value)
{
    auto const j = getJournal();
    try
    {
        auto loaded = dataForWrite(contractCtx_, account);
        if (!loaded)
            return std::unexpected(loaded.error());
        STJson& data = *loaded;
        if (!data.isObject())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedObjectField: Invalid state: not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        data.setNestedObjectField(std::string(key), std::string(nestedKey), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedObjectField: Failed to set nested "
                               "object field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        // STJson refuses a value its structure cannot hold.
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataNestedObjectField: Invalid value: " << e.what();
        return std::unexpected(HostFunctionError::InvalidParams);
    }
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataArrayElementField(
    AccountID const& account,
    size_t index,
    std::string_view const& key)
{
    auto const data = getDataOrCache(contractCtx_, account);
    if (!data)
        return std::unexpected(data.error());
    if (!data->isArray())
        return std::unexpected(HostFunctionError::InvalidState);
    return fieldBytes(data->getArrayElementField(index, std::string(key)));
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataNestedArrayElementField(
    AccountID const& account,
    std::string_view const& key,
    size_t index,
    std::string_view const& nestedKey)
{
    auto const data = getDataOrCache(contractCtx_, account);
    if (!data)
        return std::unexpected(data.error());
    if (!data->isObject())
        return std::unexpected(HostFunctionError::InvalidState);
    return fieldBytes(
        data->getNestedArrayElementField(std::string(key), index, std::string(nestedKey)));
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataArrayElementField(
    AccountID const& account,
    size_t index,
    std::string_view const& key,
    STJson::Value const& value)
{
    auto const j = getJournal();
    if (!holdableIndex(index))
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    auto loaded = dataForWrite(contractCtx_, account);
    if (!loaded)
        return std::unexpected(loaded.error());
    STJson& data = *loaded;

    try
    {
        // A new data object starts as an empty object, which becomes an array here; an
        // object that holds keys is not an array.
        if (data.isObject())
        {
            if (!data.getMap().empty())
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                                << "setDataArrayElementField: Invalid state: not an array";
                return std::unexpected(HostFunctionError::InvalidState);
            }
            data = STJson(STJson::Array{});
        }

        data.setArrayElementField(index, std::string(key), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataArrayElementField: Failed to set array "
                               "element field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        // STJson refuses a value its structure cannot hold.
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataArrayElementField: Invalid value: " << e.what();
        return std::unexpected(HostFunctionError::InvalidParams);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataNestedArrayElementField(
    AccountID const& account,
    std::string_view const& key,
    size_t index,
    std::string_view const& nestedKey,
    STJson::Value const& value)
{
    auto const j = getJournal();
    if (!holdableIndex(index))
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    try
    {
        auto loaded = dataForWrite(contractCtx_, account);
        if (!loaded)
            return std::unexpected(loaded.error());
        STJson& data = *loaded;
        if (!data.isObject())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedArrayElementField: Invalid state: "
                               "not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        data.setNestedArrayElementField(std::string(key), index, std::string(nestedKey), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedArrayElementField: Failed to set "
                               "nested array element field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        // STJson refuses a value its structure cannot hold.
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataNestedArrayElementField: Invalid value: " << e.what();
        return std::unexpected(HostFunctionError::InvalidParams);
    }
}

// Whether `tx` names another account's signature (a sponsor's or a counterparty's), which an
// emitted transaction, applied as an inner transaction, never has checked.
static bool
namesAnotherSigner(STTx const& tx)
{
    return tx.isFieldPresent(sfSponsor) || tx.isFieldPresent(sfSponsorFlags) ||
        tx.isFieldPresent(sfSponsorSignature) || tx.isFieldPresent(sfCounterpartySignature);
}

// Whether a contract may emit a transaction of this type.
static bool
isEmitable(TxType txType)
{
    return Emitable::getInstance().isEmitable(txType);
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::buildTxn(std::uint16_t const& txType)
{
    auto const j = getJournal();
    auto const& app = contractCtx_.applyCtx.registry;

    // A contract may build only a transaction type that exists and that it may emit.
    auto const* item = TxFormats::getInstance().findByType(safeCast<TxType>(txType));
    if (item == nullptr || !isEmitable(safeCast<TxType>(txType)))
    {
        JLOG(j.trace()) << "Transaction type: " << txType << " is not emitable.";
        return std::unexpected(HostFunctionError::SubmitTxnFailure);
    }

    auto jv = json::Value(json::ValueType::Object);
    jv[sfTransactionType] = item->getName();
    jv[sfFee] = "0";
    jv[sfFlags] = 1073741824;
    jv[sfSequence] = contractCtx_.result.nextSequence;
    jv[sfAccount] = to_string(contractCtx_.result.contractAccount);
    jv[sfSigningPubKey] = "";
    if (auto const networkID = app.get().getNetworkIDService().getNetworkID(); networkID != 0)
        jv[sfNetworkID] = networkID;

    STParsedJSONObject parsed("txn", jv);
    if (!parsed.object)
        return std::unexpected(HostFunctionError::InternalFatal);  // LCOV_EXCL_LINE
    contractCtx_.builtTxns.push_back(*parsed.object);
    contractCtx_.result.nextSequence += 1;
    return contractCtx_.builtTxns.size() - 1;
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::addTxnField(
    std::uint32_t const& index,
    SField const& field,
    Slice const& data)
{
    auto const j = getJournal();
    try
    {
        if (index >= contractCtx_.builtTxns.size())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "addTxnField: index out of bounds: " << index;
            return std::unexpected(HostFunctionError::IndexOutOfBounds);
        }

        // Get the transaction STObject
        auto& obj = contractCtx_.builtTxns[index];

        // build_txn sets TransactionType, and addTxnField can only replace it.
        if (!obj.isFieldPresent(sfTransactionType))
            return std::unexpected(HostFunctionError::FieldNotFound);  // LCOV_EXCL_LINE

        // Extract the numeric tx type from the STObject and convert to TxType
        auto txTypeVal = obj.getFieldU16(sfTransactionType);
        auto txFormat = TxFormats::getInstance().findByType(safeCast<TxType>(txTypeVal));
        if (txFormat == nullptr)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "addTxnField: Invalid TransactionType: " << txTypeVal;
            return std::unexpected(HostFunctionError::FieldNotFound);
        }

        // Check if the provided field is allowed for this transaction type
        bool found = false;
        for (auto const& e : txFormat->getSOTemplate())
        {
            if (e.sField().getName() == field.getName())
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: " << "addTxnField: Field "
                            << field.getName() << " not allowed in transaction type "
                            << txFormat->getName();
            return std::unexpected(HostFunctionError::FieldNotFound);
        }

        obj.addFieldFromSlice(field, data);
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: " << "addTxnField: TXN: "
                        << obj.getJson(JsonOptions::Values::None).toStyledString();
        return 0;
    }
    catch (std::exception const& e)
    {
        // The bytes do not hold the field the contract named: its own mistake.
        JLOG(j.trace()) << "WasmTrace[" << contractId_
                        << "]: addTxnField: Invalid value: " << e.what();
        return std::unexpected(HostFunctionError::InvalidParams);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitBuiltTxn(std::uint32_t const& index)
{
    auto const j = getJournal();
    auto const& app = contractCtx_.applyCtx.registry;
    auto& parentTx = contractCtx_.applyCtx.tx;
    auto const parentBatchId = parentTx.getTransactionID();
    if (index >= contractCtx_.builtTxns.size())
    {
        JLOG(j.trace()) << "WasmTrace[" << parentBatchId
                        << "]: " << "emitBuiltTxn: index out of bounds: " << index;
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    }

    // Ensure tfInnerBatchTxn is always set, even if the contract
    // overwrote sfFlags via addTxnField.
    contractCtx_.builtTxns[index].setFlag(tfInnerBatchTxn);

    // `STTx`'s constructor checks the transaction against its format, and reports a
    // field the type requires but the contract never set by throwing. That is the
    // contract's own mistake, so it is answered with SubmitTxnFailure.
    //
    // The built transaction is copied rather than moved: a run that is told its
    // transaction is malformed can correct it and emit the same index again. `STTx`
    // takes the object by rvalue, so the copy is made here.
    std::shared_ptr<STTx const> stx;
    try
    {
        STObject built = contractCtx_.builtTxns[index];
        stx = std::make_shared<STTx const>(std::move(built));
        (void)stx->getTransactionID();
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: "
                        << "emitBuiltTxn: Failed to build transaction: " << e.what();
        return std::unexpected(HostFunctionError::SubmitTxnFailure);
    }

    // Inner transactions skip the signature check, so a contract may emit only from its own
    // account, only a type it may build (TransactionType can be rewritten after build_txn),
    // and nothing that names another account's signature.
    if (stx->getAccountID(sfAccount) != contractCtx_.result.contractAccount ||
        !isEmitable(stx->getTxnType()) || namesAnotherSigner(*stx))
        return std::unexpected(HostFunctionError::SubmitTxnFailure);

    // Use a persistent emit view that is seeded with the
    // transactor's pending state changes (balances, consumed
    // sequence, etc.) so that each emitted transaction validates
    // against the full current state.  A full apply() is used
    // (matching the Batch inner-transaction pattern) so that
    // sequence numbers, balances, owner counts, and all other
    // ledger state are properly updated between successive emits.
    auto& emitView = contractCtx_.getEmitView();

    OpenView perTxView(kBatchView, emitView);
    auto const ret = apply(app, perTxView, parentBatchId, *stx, TapBatch, j);

    JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: " << stx->getTransactionID() << " "
                    << transToken(ret.ter);

    if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
    {
        perTxView.apply(emitView);
        contractCtx_.result.emittedTxns.push(stx);
    }
    return TERtoInt(ret.ter);
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitTxn(std::shared_ptr<STTx const> const& stxPtr)
{
    auto const& app = contractCtx_.applyCtx.registry;
    auto& parentTx = contractCtx_.applyCtx.tx;
    auto const j = getJournal();

    // Set tfInnerBatchTxn on a copy made with STTx's copy constructor.
    std::shared_ptr<STTx const> txPtr = stxPtr;
    if (!stxPtr->isFlag(tfInnerBatchTxn))
    {
        auto flagged = std::make_shared<STTx>(*stxPtr);

        // `setFlag` reaches an existing `sfFlags` and cannot add one: a transaction
        // built against a template holds the field as a placeholder until something
        // gives it a value, and that is what `setFieldU32` does.
        flagged->setFieldU32(sfFlags, flagged->getFlags() | tfInnerBatchTxn);
        txPtr = std::move(flagged);
    }

    // See emitBuiltTxn: a contract may emit only from its own account, only a type it may
    // build, and nothing that names another account's signature.
    if (txPtr->getAccountID(sfAccount) != contractCtx_.result.contractAccount ||
        !isEmitable(txPtr->getTxnType()) || namesAnotherSigner(*txPtr))
        return std::unexpected(HostFunctionError::SubmitTxnFailure);

    // Use a persistent emit view seeded with the transactor's
    // pending state, and do a full apply() for each emission
    // (see emitBuiltTxn for detailed rationale).
    auto& emitView = contractCtx_.getEmitView();
    auto const parentBatchId = parentTx.getTransactionID();
    auto const& stx = txPtr;

    OpenView perTxView(kBatchView, emitView);
    auto const ret = apply(app, perTxView, parentBatchId, *stx, TapBatch, j);

    JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: " << stx->getTransactionID() << " "
                    << transToken(ret.ter);

    if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
    {
        perTxView.apply(emitView);
        contractCtx_.result.emittedTxns.push(stx);
    }
    return TERtoInt(ret.ter);
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitEvent(std::string_view const& eventName, STJson const& eventData)
{
    auto& events = contractCtx_.result.events;
    if (events.size() >= static_cast<std::size_t>(contract::maxContractEvents))
    {
        JLOG(getJournal().trace()) << "WasmTrace[" << contractId_ << "]: "
                                   << "emitEvent: Exceeded max events";
        return std::unexpected(HostFunctionError::InvalidState);
    }
    events.push_back({.name = std::string(eventName), .data = eventData});
    return 0;
}

}  // namespace xrpl
