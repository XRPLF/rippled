#include <tx/wasm/fixtures/ContractLedger.h>

#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <utility>

namespace xrpl::test {

ContractHost::ContractHost(
    std::shared_ptr<STTx const> tx,
    std::unique_ptr<ApplyContext> context,
    std::unique_ptr<ContractContext> contractContext,
    std::unique_ptr<ContractHostFunctionsImpl> host)
    : tx_{std::move(tx)}
    , context_{std::move(context)}
    , contractContext_{std::move(contractContext)}
    , host_{std::move(host)}
{
}

ContractHostFunctionsImpl*
ContractHost::operator->() const
{
    return host_.get();
}

ContractHostFunctionsImpl&
ContractHost::operator*() const
{
    return *host_;
}

ContractContext&
ContractHost::context() const
{
    return *contractContext_;
}

ApplyContext&
ContractHost::applyContext() const
{
    return *context_;
}

TER
ContractHost::finalize() const
{
    auto const result = contract::finalizeContractData(
        context_->registry,
        context_->view(),
        contractContext_->result.contractAccount,
        contractContext_->result.dataMap,
        contractContext_->result.eventMap,
        tx_->getTransactionID());
    if (!isTesSuccess(result))
        return result;

    context_->apply(tesSUCCESS);

    // `apply` reaches the sandbox `ApplyContext` holds, not the ledger under it; the
    // transactor pushes one into the other, and so does this.
    context_->finalize();
    return tesSUCCESS;
}

ContractHost
ContractLedger::makeContractHost(beast::Journal journal, ContractHostOptions options)
{
    auto const contractKeylet =
        options.contractKeylet.value_or(keylet::contract(options.contractHash));

    auto const contractAccount = options.contractAccount;
    auto const caller = options.caller;
    auto tx = std::make_shared<STTx>(ttCONTRACT_CALL, [&](STObject& obj) {
        obj.setAccountID(sfAccount, caller);
        obj.setAccountID(sfContractAccount, contractAccount);
        obj.setFieldVL(sfFunctionName, Blob{'c', 'a', 'l', 'l'});
        obj.setFieldU32(sfGas, 1'000'000);
        obj.setFieldU32(sfSequence, callSequence_++);
    });

    auto context = std::make_unique<ApplyContext>(
        ledger.getServiceRegistry(),
        ledger.getOpenLedger(),
        *tx,
        tesSUCCESS,
        ledger.getOpenLedger().fees().base,
        TapNone,
        journal);

    // The contract account's own sequence, which `build_txn` stamps on each transaction it
    // builds. A contract account that does not exist yet starts at the first sequence.
    auto const accountSle = context->view().read(keylet::account(contractAccount));
    std::uint32_t const nextSequence = accountSle ? accountSle->getFieldU32(sfSequence) : 1;

    auto contractContext = std::make_unique<ContractContext>(ContractContext{
        .applyCtx = *context,
        .instanceParameters = std::move(options.instanceParameters),
        .functionParameters = std::move(options.functionParameters),
        .built_txns = {},
        .expected_etxn_count = 1,
        .result =
            {
                .contractHash = options.contractHash,
                .contractKeylet = contractKeylet,
                .contractSourceKeylet = keylet::contractSource(options.contractHash),
                .contractAccountKeylet = keylet::account(contractAccount),
                .contractAccount = contractAccount,
                .nextSequence = nextSequence,
                .otxnAccount = caller,
                .otxnId = tx->getTransactionID(),
            },
    });

    auto host = std::make_unique<ContractHostFunctionsImpl>(*contractContext);
    return ContractHost{
        std::move(tx), std::move(context), std::move(contractContext), std::move(host)};
}

ContractHost
ContractLedger::makeContractHost(ContractHostOptions options)
{
    return makeContractHost(beast::Journal{beast::Journal::getNullSink()}, std::move(options));
}

ContractHost
ContractLedger::makeTracingContractHost(ContractHostOptions options)
{
    return makeContractHost(tracingJournal(), std::move(options));
}

std::shared_ptr<SLE const>
ContractLedger::contractData(AccountID const& owner, AccountID const& contractAccount)
{
    return ledger.getOpenLedger().read(keylet::contractData(owner, contractAccount));
}

}  // namespace xrpl::test
