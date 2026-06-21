import XRPL.Model.Protocol.LedgerEntries.AccountRoot


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_account_root_empty]
def lean_account_root_empty (_ : Unit) : AccountRoot := AccountRoot.empty

@[export lean_account_root_key_get]
def lean_account_root_key_get (a : AccountRoot) : UInt256 := a.key
@[export lean_account_root_key_set]
def lean_account_root_key_set (a : AccountRoot) (key : UInt256) : AccountRoot := { a with key }

@[export lean_account_root_flags_get]
def lean_account_root_flags_get (a : AccountRoot) : UInt32 := a.flags
@[export lean_account_root_flags_set]
def lean_account_root_flags_set (a : AccountRoot) (flags : UInt32) : AccountRoot := { a with flags }

@[export lean_account_root_account_get]
def lean_account_root_account_get (a : AccountRoot) : AccountID := a.account
@[export lean_account_root_account_set]
def lean_account_root_account_set (a : AccountRoot) (account : AccountID) : AccountRoot := { a with account }

@[export lean_account_root_sequence_get]
def lean_account_root_sequence_get (a : AccountRoot) : UInt32 := a.sequence
@[export lean_account_root_sequence_set]
def lean_account_root_sequence_set (a : AccountRoot) (sequence : UInt32) : AccountRoot := { a with sequence }

@[export lean_account_root_balance_get]
def lean_account_root_balance_get (a : AccountRoot) : STAmount := a.balance
@[export lean_account_root_balance_set]
def lean_account_root_balance_set (a : AccountRoot) (balance : STAmount) : AccountRoot := { a with balance }

@[export lean_account_root_owner_count_get]
def lean_account_root_owner_count_get (a : AccountRoot) : UInt32 := a.ownerCount
@[export lean_account_root_owner_count_set]
def lean_account_root_owner_count_set (a : AccountRoot) (ownerCount : UInt32) : AccountRoot := { a with ownerCount }

@[export lean_account_root_previous_txn_id_get]
def lean_account_root_previous_txn_id_get (a : AccountRoot) : UInt256 := a.previousTxnID
@[export lean_account_root_previous_txn_id_set]
def lean_account_root_previous_txn_id_set (a : AccountRoot) (previousTxnID : UInt256) : AccountRoot :=
  { a with previousTxnID }

@[export lean_account_root_previous_txn_lgr_seq_get]
def lean_account_root_previous_txn_lgr_seq_get (a : AccountRoot) : UInt32 := a.previousTxnLgrSeq
@[export lean_account_root_previous_txn_lgr_seq_set]
def lean_account_root_previous_txn_lgr_seq_set (a : AccountRoot) (previousTxnLgrSeq : UInt32) : AccountRoot :=
  { a with previousTxnLgrSeq }

@[export lean_account_root_account_txn_id_get]
def lean_account_root_account_txn_id_get (a : AccountRoot) : Option UInt256 := a.accountTxnID
@[export lean_account_root_account_txn_id_set]
def lean_account_root_account_txn_id_set (a : AccountRoot) (accountTxnID : Option UInt256) : AccountRoot :=
  { a with accountTxnID }

@[export lean_account_root_regular_key_get]
def lean_account_root_regular_key_get (a : AccountRoot) : Option AccountID := a.regularKey
@[export lean_account_root_regular_key_set]
def lean_account_root_regular_key_set (a : AccountRoot) (regularKey : Option AccountID) : AccountRoot :=
  { a with regularKey }

@[export lean_account_root_email_hash_get]
def lean_account_root_email_hash_get (a : AccountRoot) : Option UInt128 := a.emailHash
@[export lean_account_root_email_hash_set]
def lean_account_root_email_hash_set (a : AccountRoot) (emailHash : Option UInt128) : AccountRoot :=
  { a with emailHash }

@[export lean_account_root_wallet_locator_get]
def lean_account_root_wallet_locator_get (a : AccountRoot) : Option UInt256 := a.walletLocator
@[export lean_account_root_wallet_locator_set]
def lean_account_root_wallet_locator_set (a : AccountRoot) (walletLocator : Option UInt256) : AccountRoot :=
  { a with walletLocator }

@[export lean_account_root_wallet_size_get]
def lean_account_root_wallet_size_get (a : AccountRoot) : Option UInt32 := a.walletSize
@[export lean_account_root_wallet_size_set]
def lean_account_root_wallet_size_set (a : AccountRoot) (walletSize : Option UInt32) : AccountRoot :=
  { a with walletSize }

@[export lean_account_root_message_key_get]
def lean_account_root_message_key_get (a : AccountRoot) : Option ByteArray := a.messageKey.map (⟨·.toArray⟩)
@[export lean_account_root_message_key_set]
def lean_account_root_message_key_set (a : AccountRoot) (messageKey : Option ByteArray) : AccountRoot :=
  { a with messageKey := messageKey.map (·.toList) }

@[export lean_account_root_transfer_rate_get]
def lean_account_root_transfer_rate_get (a : AccountRoot) : Option UInt32 := a.transferRate
@[export lean_account_root_transfer_rate_set]
def lean_account_root_transfer_rate_set (a : AccountRoot) (transferRate : Option UInt32) : AccountRoot :=
  { a with transferRate }

@[export lean_account_root_domain_get]
def lean_account_root_domain_get (a : AccountRoot) : Option ByteArray := a.domain.map (⟨·.toArray⟩)
@[export lean_account_root_domain_set]
def lean_account_root_domain_set (a : AccountRoot) (domain : Option ByteArray) : AccountRoot :=
  { a with domain := domain.map (·.toList) }

@[export lean_account_root_tick_size_get]
def lean_account_root_tick_size_get (a : AccountRoot) : Option UInt8 := a.tickSize
@[export lean_account_root_tick_size_set]
def lean_account_root_tick_size_set (a : AccountRoot) (tickSize : Option UInt8) : AccountRoot :=
  { a with tickSize }

@[export lean_account_root_ticket_count_get]
def lean_account_root_ticket_count_get (a : AccountRoot) : Option UInt32 := a.ticketCount
@[export lean_account_root_ticket_count_set]
def lean_account_root_ticket_count_set (a : AccountRoot) (ticketCount : Option UInt32) : AccountRoot :=
  { a with ticketCount }

@[export lean_account_root_nftoken_minter_get]
def lean_account_root_nftoken_minter_get (a : AccountRoot) : Option AccountID := a.nftokenMinter
@[export lean_account_root_nftoken_minter_set]
def lean_account_root_nftoken_minter_set (a : AccountRoot) (nftokenMinter : Option AccountID) : AccountRoot :=
  { a with nftokenMinter }

@[export lean_account_root_minted_nftokens_get]
def lean_account_root_minted_nftokens_get (a : AccountRoot) : UInt32 := a.mintedNFTokens
@[export lean_account_root_minted_nftokens_set]
def lean_account_root_minted_nftokens_set (a : AccountRoot) (mintedNFTokens : UInt32) : AccountRoot :=
  { a with mintedNFTokens }

@[export lean_account_root_burned_nftokens_get]
def lean_account_root_burned_nftokens_get (a : AccountRoot) : UInt32 := a.burnedNFTokens
@[export lean_account_root_burned_nftokens_set]
def lean_account_root_burned_nftokens_set (a : AccountRoot) (burnedNFTokens : UInt32) : AccountRoot :=
  { a with burnedNFTokens }

@[export lean_account_root_first_nftoken_sequence_get]
def lean_account_root_first_nftoken_sequence_get (a : AccountRoot) : Option UInt32 := a.firstNFTokenSequence
@[export lean_account_root_first_nftoken_sequence_set]
def lean_account_root_first_nftoken_sequence_set (a : AccountRoot) (firstNFTokenSequence : Option UInt32) : AccountRoot :=
  { a with firstNFTokenSequence }

@[export lean_account_root_amm_id_get]
def lean_account_root_amm_id_get (a : AccountRoot) : Option UInt256 := a.ammID
@[export lean_account_root_amm_id_set]
def lean_account_root_amm_id_set (a : AccountRoot) (ammID : Option UInt256) : AccountRoot := { a with ammID }

@[export lean_account_root_vault_id_get]
def lean_account_root_vault_id_get (a : AccountRoot) : Option UInt256 := a.vaultID
@[export lean_account_root_vault_id_set]
def lean_account_root_vault_id_set (a : AccountRoot) (vaultID : Option UInt256) : AccountRoot := { a with vaultID }

@[export lean_account_root_loan_broker_id_get]
def lean_account_root_loan_broker_id_get (a : AccountRoot) : Option UInt256 := a.loanBrokerID
@[export lean_account_root_loan_broker_id_set]
def lean_account_root_loan_broker_id_set (a : AccountRoot) (loanBrokerID : Option UInt256) : AccountRoot :=
  { a with loanBrokerID }

end XRPL.FFI
