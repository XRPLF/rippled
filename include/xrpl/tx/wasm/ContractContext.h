#pragma once

#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/st.h>
#include <xrpl/tx/ApplyContext.h>

#include <queue>

namespace xrpl {

struct ParameterValueVec
{
    STData const value;
};

struct FunctionParameterValueVecWithName
{
    Blob const name;
    STData const value;
};

struct ParameterTypeVec
{
    STDataType const type;
};

std::vector<ParameterValueVec>
getParameterValueVec(STArray const& functionParameters);

std::vector<ParameterTypeVec>
getParameterTypeVec(STArray const& functionParameters);

enum ExitType : uint8_t {
    UNSET = 0,
    WASM_ERROR = 1,
    ROLLBACK = 2,
    ACCEPT = 3,
};

struct ContractResult
{
    uint256 const contractHash;          // Hash of the contract code
    Keylet const contractKeylet;         // Keylet for the contract instance
    Keylet const contractSourceKeylet;   // Keylet for the contract source
    Keylet const contractAccountKeylet;  // Keylet for the contract account
    AccountID const contractAccount;     // AccountID of the contract account
    std::uint32_t nextSequence;          // Next sequence number for the contract account
    AccountID const otxnAccount;         // AccountID for the originating transaction
    uint256 const otxnId;                // ID for the originating transaction
    std::string exitReason{""};
    int64_t exitCode{-1};
    ContractDataMap dataMap;
    ContractEventMap eventMap;
    std::queue<std::shared_ptr<STTx const>> emittedTxns{};
    std::size_t changedDataCount{0};
};

struct ContractContext
{
    ApplyContext& applyCtx;
    std::vector<ParameterValueVec> instanceParameters;
    std::vector<ParameterValueVec> functionParameters;
    std::vector<STObject> built_txns;
    int64_t expected_etxn_count{-1};       // expected emitted transaction count
    std::map<uint256, bool> nonce_used{};  // nonces used in this execution
    uint32_t generation = 0;               // generation of the contract being executed
    uint64_t burden = 0;                   // computational burden used
    ContractResult result;

    /// Persistent view used to track cumulative state from emitted
    /// transactions so that successive emits within the same WASM
    /// execution see the correct sequence numbers, balances, etc.
    std::optional<OpenView> emitView;

    /// Return the emit view, lazily creating it on first use.
    /// On first call the view is seeded with any pending changes
    /// already in the transactor's apply-view (e.g. the tfSendAmount
    /// balance transfer, consumed sequence number, paid fee) so that
    /// emitted transactions validate against up-to-date state.
    OpenView&
    getEmitView()
    {
        if (!emitView)
        {
            emitView.emplace(batch_view, applyCtx.openView());

            // Copy every SLE change that doApply has already made
            // (via ctx_.view()) into the emit view.
            applyCtx.visit([this](
                               uint256 const&,
                               bool isDelete,
                               std::shared_ptr<SLE const> const& before,
                               std::shared_ptr<SLE const> const& after) {
                if (isDelete && before)
                    emitView->rawErase(std::make_shared<SLE>(*before));
                else if (!before && after)
                    emitView->rawInsert(std::make_shared<SLE>(*after));
                else if (before && after)
                    emitView->rawReplace(std::make_shared<SLE>(*after));
            });
        }
        return *emitView;
    }
};

}  // namespace xrpl
