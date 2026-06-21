#pragma once

#include <test/formal_verification/ffi/protocol/AcceptedCredentialFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/AssetFFI.h>
#include <test/formal_verification/ffi/protocol/MptIdFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/STNumberFFI.h>
#include <test/formal_verification/ffi/protocol/UInt128FFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol_autogen/ledger_entries/AccountRoot.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_account_root_empty(lean_object* unit);

lean_object*
lean_account_root_key_get(lean_object* o);
lean_object*
lean_account_root_key_set(lean_object* o, lean_object* key);
uint32_t
lean_account_root_flags_get(lean_object* o);
lean_object*
lean_account_root_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_account_root_account_get(lean_object* o);
lean_object*
lean_account_root_account_set(lean_object* o, lean_object* account);
uint32_t
lean_account_root_sequence_get(lean_object* o);
lean_object*
lean_account_root_sequence_set(lean_object* o, uint32_t sequence);
lean_object*
lean_account_root_balance_get(lean_object* o);
lean_object*
lean_account_root_balance_set(lean_object* o, lean_object* balance);
uint32_t
lean_account_root_owner_count_get(lean_object* o);
lean_object*
lean_account_root_owner_count_set(lean_object* o, uint32_t ownerCount);
lean_object*
lean_account_root_previous_txn_id_get(lean_object* o);
lean_object*
lean_account_root_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_account_root_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_account_root_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
lean_object*
lean_account_root_account_txn_id_get(lean_object* o);
lean_object*
lean_account_root_account_txn_id_set(lean_object* o, lean_object* accountTxnID);
lean_object*
lean_account_root_regular_key_get(lean_object* o);
lean_object*
lean_account_root_regular_key_set(lean_object* o, lean_object* regularKey);
lean_object*
lean_account_root_email_hash_get(lean_object* o);
lean_object*
lean_account_root_email_hash_set(lean_object* o, lean_object* emailHash);
lean_object*
lean_account_root_wallet_locator_get(lean_object* o);
lean_object*
lean_account_root_wallet_locator_set(lean_object* o, lean_object* walletLocator);
lean_object*
lean_account_root_wallet_size_get(lean_object* o);
lean_object*
lean_account_root_wallet_size_set(lean_object* o, lean_object* walletSize);
lean_object*
lean_account_root_message_key_get(lean_object* o);
lean_object*
lean_account_root_message_key_set(lean_object* o, lean_object* messageKey);
lean_object*
lean_account_root_transfer_rate_get(lean_object* o);
lean_object*
lean_account_root_transfer_rate_set(lean_object* o, lean_object* transferRate);
lean_object*
lean_account_root_domain_get(lean_object* o);
lean_object*
lean_account_root_domain_set(lean_object* o, lean_object* domain);
lean_object*
lean_account_root_tick_size_get(lean_object* o);
lean_object*
lean_account_root_tick_size_set(lean_object* o, lean_object* tickSize);
lean_object*
lean_account_root_ticket_count_get(lean_object* o);
lean_object*
lean_account_root_ticket_count_set(lean_object* o, lean_object* ticketCount);
lean_object*
lean_account_root_nftoken_minter_get(lean_object* o);
lean_object*
lean_account_root_nftoken_minter_set(lean_object* o, lean_object* nFTokenMinter);
uint32_t
lean_account_root_minted_nftokens_get(lean_object* o);
lean_object*
lean_account_root_minted_nftokens_set(lean_object* o, uint32_t mintedNFTokens);
uint32_t
lean_account_root_burned_nftokens_get(lean_object* o);
lean_object*
lean_account_root_burned_nftokens_set(lean_object* o, uint32_t burnedNFTokens);
lean_object*
lean_account_root_first_nftoken_sequence_get(lean_object* o);
lean_object*
lean_account_root_first_nftoken_sequence_set(lean_object* o, lean_object* firstNFTokenSequence);
lean_object*
lean_account_root_amm_id_get(lean_object* o);
lean_object*
lean_account_root_amm_id_set(lean_object* o, lean_object* aMMID);
lean_object*
lean_account_root_vault_id_get(lean_object* o);
lean_object*
lean_account_root_vault_id_set(lean_object* o, lean_object* vaultID);
lean_object*
lean_account_root_loan_broker_id_get(lean_object* o);
lean_object*
lean_account_root_loan_broker_id_set(lean_object* o, lean_object* loanBrokerID);
}

namespace xrpl::test::formal_verification {

class AccountRootFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_account_root_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_account_root_flags_get);
    }
    AccountID
    account() const
    {
        return leanGetObj<AccountIDFFI>(lean_account_root_account_get);
    }
    uint32_t
    sequence() const
    {
        return leanGet<uint32_t>(lean_account_root_sequence_get);
    }
    STAmount
    balance() const
    {
        return leanGetObj<STAmountFFI>(lean_account_root_balance_get);
    }
    uint32_t
    ownerCount() const
    {
        return leanGet<uint32_t>(lean_account_root_owner_count_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_account_root_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_account_root_previous_txn_lgr_seq_get);
    }
    std::optional<uint256>
    accountTxnID() const
    {
        return leanGetOpt<UInt256FFI>(lean_account_root_account_txn_id_get);
    }
    std::optional<AccountID>
    regularKey() const
    {
        return leanGetOpt<AccountIDFFI>(lean_account_root_regular_key_get);
    }
    std::optional<uint128>
    emailHash() const
    {
        return leanGetOpt<UInt128FFI>(lean_account_root_email_hash_get);
    }
    std::optional<uint256>
    walletLocator() const
    {
        return leanGetOpt<UInt256FFI>(lean_account_root_wallet_locator_get);
    }
    std::optional<uint32_t>
    walletSize() const
    {
        return leanGetOptU32(lean_account_root_wallet_size_get);
    }
    std::optional<Blob>
    messageKey() const
    {
        return leanGetOptBytes(lean_account_root_message_key_get);
    }
    std::optional<uint32_t>
    transferRate() const
    {
        return leanGetOptU32(lean_account_root_transfer_rate_get);
    }
    std::optional<Blob>
    domain() const
    {
        return leanGetOptBytes(lean_account_root_domain_get);
    }
    std::optional<uint8_t>
    tickSize() const
    {
        return leanGetOptU8(lean_account_root_tick_size_get);
    }
    std::optional<uint32_t>
    ticketCount() const
    {
        return leanGetOptU32(lean_account_root_ticket_count_get);
    }
    std::optional<AccountID>
    nFTokenMinter() const
    {
        return leanGetOpt<AccountIDFFI>(lean_account_root_nftoken_minter_get);
    }
    uint32_t
    mintedNFTokens() const
    {
        return leanGet<uint32_t>(lean_account_root_minted_nftokens_get);
    }
    uint32_t
    burnedNFTokens() const
    {
        return leanGet<uint32_t>(lean_account_root_burned_nftokens_get);
    }
    std::optional<uint32_t>
    firstNFTokenSequence() const
    {
        return leanGetOptU32(lean_account_root_first_nftoken_sequence_get);
    }
    std::optional<uint256>
    aMMID() const
    {
        return leanGetOpt<UInt256FFI>(lean_account_root_amm_id_get);
    }
    std::optional<uint256>
    vaultID() const
    {
        return leanGetOpt<UInt256FFI>(lean_account_root_vault_id_get);
    }
    std::optional<uint256>
    loanBrokerID() const
    {
        return leanGetOpt<UInt256FFI>(lean_account_root_loan_broker_id_get);
    }

    ledger_entries::AccountRoot
    toCpp() const
    {
        ledger_entries::AccountRootBuilder b(
            account(), sequence(), balance(), ownerCount(), previousTxnID(), previousTxnLgrSeq());
        b.setFlags(flags());
        if (auto v = accountTxnID())
            b.setAccountTxnID(*v);
        if (auto v = regularKey())
            b.setRegularKey(*v);
        if (auto v = emailHash())
            b.setEmailHash(*v);
        if (auto v = walletLocator())
            b.setWalletLocator(*v);
        if (auto v = walletSize())
            b.setWalletSize(*v);
        if (auto v = messageKey())
            b.setMessageKey(makeSlice(*v));
        if (auto v = transferRate())
            b.setTransferRate(*v);
        if (auto v = domain())
            b.setDomain(makeSlice(*v));
        if (auto v = tickSize())
            b.setTickSize(*v);
        if (auto v = ticketCount())
            b.setTicketCount(*v);
        if (auto v = nFTokenMinter())
            b.setNFTokenMinter(*v);
        if (uint32_t v = mintedNFTokens(); v != 0)
            b.setMintedNFTokens(v);
        if (uint32_t v = burnedNFTokens(); v != 0)
            b.setBurnedNFTokens(v);
        if (auto v = firstNFTokenSequence())
            b.setFirstNFTokenSequence(*v);
        if (auto v = aMMID())
            b.setAMMID(*v);
        if (auto v = vaultID())
            b.setVaultID(*v);
        if (auto v = loanBrokerID())
            b.setLoanBrokerID(*v);
        return b.build(key());
    }
};

class AccountRootFFIBuilder : public LeanObjectFFI
{
public:
    AccountRootFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_account_root_empty))
    {
    }

    AccountRootFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_account_root_flags_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    account(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_account_root_account_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    sequence(uint32_t v)
    {
        leanSet(lean_account_root_sequence_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    balance(STAmount const& v)
    {
        leanSetObj<STAmountFFI>(lean_account_root_balance_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    ownerCount(uint32_t v)
    {
        leanSet(lean_account_root_owner_count_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_account_root_previous_txn_id_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_account_root_previous_txn_lgr_seq_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    accountTxnID(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_account_root_account_txn_id_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    regularKey(AccountID const& v)
    {
        leanSetOptObj<AccountIDFFI>(lean_account_root_regular_key_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    emailHash(uint128 const& v)
    {
        leanSetOptObj<UInt128FFI>(lean_account_root_email_hash_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    walletLocator(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_account_root_wallet_locator_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    walletSize(uint32_t v)
    {
        leanSetOptU32(lean_account_root_wallet_size_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    messageKey(Blob const& v)
    {
        leanSetOptBytes(lean_account_root_message_key_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    transferRate(uint32_t v)
    {
        leanSetOptU32(lean_account_root_transfer_rate_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    domain(Blob const& v)
    {
        leanSetOptBytes(lean_account_root_domain_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    tickSize(uint8_t v)
    {
        leanSetOptU8(lean_account_root_tick_size_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    ticketCount(uint32_t v)
    {
        leanSetOptU32(lean_account_root_ticket_count_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    nFTokenMinter(AccountID const& v)
    {
        leanSetOptObj<AccountIDFFI>(lean_account_root_nftoken_minter_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    mintedNFTokens(uint32_t v)
    {
        leanSet(lean_account_root_minted_nftokens_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    burnedNFTokens(uint32_t v)
    {
        leanSet(lean_account_root_burned_nftokens_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    firstNFTokenSequence(uint32_t v)
    {
        leanSetOptU32(lean_account_root_first_nftoken_sequence_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    aMMID(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_account_root_amm_id_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    vaultID(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_account_root_vault_id_set, v);
        return *this;
    }
    AccountRootFFIBuilder&
    loanBrokerID(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_account_root_loan_broker_id_set, v);
        return *this;
    }

    AccountRootFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_account_root_key_set, key);
        return leanBuildAs<AccountRootFFI>();
    }

    AccountRootFFIBuilder&
    fromCpp(ledger_entries::AccountRoot const& c)
    {
        flags(c.getFlags());
        account(c.getAccount());
        sequence(c.getSequence());
        balance(c.getBalance());
        ownerCount(c.getOwnerCount());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        if (auto v = c.getAccountTxnID())
            accountTxnID(*v);
        if (auto v = c.getRegularKey())
            regularKey(*v);
        if (auto v = c.getEmailHash())
            emailHash(*v);
        if (auto v = c.getWalletLocator())
            walletLocator(*v);
        if (auto v = c.getWalletSize())
            walletSize(*v);
        if (auto v = c.getMessageKey())
            messageKey(Blob(v->begin(), v->end()));
        if (auto v = c.getTransferRate())
            transferRate(*v);
        if (auto v = c.getDomain())
            domain(Blob(v->begin(), v->end()));
        if (auto v = c.getTickSize())
            tickSize(*v);
        if (auto v = c.getTicketCount())
            ticketCount(*v);
        if (auto v = c.getNFTokenMinter())
            nFTokenMinter(*v);
        if (auto v = c.getMintedNFTokens())
            mintedNFTokens(*v);
        if (auto v = c.getBurnedNFTokens())
            burnedNFTokens(*v);
        if (auto v = c.getFirstNFTokenSequence())
            firstNFTokenSequence(*v);
        if (auto v = c.getAMMID())
            aMMID(*v);
        if (auto v = c.getVaultID())
            vaultID(*v);
        if (auto v = c.getLoanBrokerID())
            loanBrokerID(*v);
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
