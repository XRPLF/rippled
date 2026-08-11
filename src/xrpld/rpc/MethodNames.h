#pragma once

#include <string_view>

namespace xrpl::rpc::method {

/**
 * Names of the RPC methods the server accepts.
 *
 * Defined here so the dispatch table in Handler.cpp and the command-line parser
 * table in RPCCall.cpp name each method through the same constant, and cannot
 * drift apart.
 */

inline constexpr std::string_view kAccountChannels{"account_channels"};
inline constexpr std::string_view kAccountCurrencies{"account_currencies"};
inline constexpr std::string_view kAccountInfo{"account_info"};
inline constexpr std::string_view kAccountLines{"account_lines"};
inline constexpr std::string_view kAccountNfts{"account_nfts"};
inline constexpr std::string_view kAccountObjects{"account_objects"};
inline constexpr std::string_view kAccountOffers{"account_offers"};
inline constexpr std::string_view kAccountTx{"account_tx"};
inline constexpr std::string_view kAmmInfo{"amm_info"};
inline constexpr std::string_view kBlacklist{"blacklist"};  // no command-line form
inline constexpr std::string_view kBookChanges{"book_changes"};
inline constexpr std::string_view kBookOffers{"book_offers"};
inline constexpr std::string_view kCanDelete{"can_delete"};
inline constexpr std::string_view kChannelAuthorize{"channel_authorize"};
inline constexpr std::string_view kChannelVerify{"channel_verify"};
inline constexpr std::string_view kConnect{"connect"};
inline constexpr std::string_view kConsensusInfo{"consensus_info"};
inline constexpr std::string_view kDepositAuthorized{"deposit_authorized"};
inline constexpr std::string_view kFeature{"feature"};
inline constexpr std::string_view kFee{"fee"};  // no command-line form
inline constexpr std::string_view kFetchInfo{"fetch_info"};
inline constexpr std::string_view kGatewayBalances{"gateway_balances"};
inline constexpr std::string_view kGetAggregatePrice{
    "get_aggregate_price"};  // no command-line form
inline constexpr std::string_view kGetCounts{"get_counts"};
inline constexpr std::string_view kInternal{"internal"};  // command-line wrapper
inline constexpr std::string_view kJson{"json"};          // command-line wrapper
inline constexpr std::string_view kJson2{"json2"};        // command-line wrapper
inline constexpr std::string_view kLedger{"ledger"};
inline constexpr std::string_view kLedgerAccept{"ledger_accept"};
inline constexpr std::string_view kLedgerCleaner{"ledger_cleaner"};  // no command-line form
inline constexpr std::string_view kLedgerClosed{"ledger_closed"};
inline constexpr std::string_view kLedgerCurrent{"ledger_current"};
inline constexpr std::string_view kLedgerData{"ledger_data"};  // no command-line form
inline constexpr std::string_view kLedgerEntry{"ledger_entry"};
inline constexpr std::string_view kLedgerHeader{"ledger_header"};
inline constexpr std::string_view kLedgerRequest{"ledger_request"};
inline constexpr std::string_view kLogLevel{"log_level"};
inline constexpr std::string_view kLogrotate{"logrotate"};
inline constexpr std::string_view kManifest{"manifest"};
inline constexpr std::string_view kNftBuyOffers{"nft_buy_offers"};    // no command-line form
inline constexpr std::string_view kNftSellOffers{"nft_sell_offers"};  // no command-line form
inline constexpr std::string_view kNorippleCheck{"noripple_check"};   // no command-line form
inline constexpr std::string_view kOwnerInfo{"owner_info"};
inline constexpr std::string_view kPathFind{"path_find"};
inline constexpr std::string_view kPeerReservationsAdd{"peer_reservations_add"};
inline constexpr std::string_view kPeerReservationsDel{"peer_reservations_del"};
inline constexpr std::string_view kPeerReservationsList{"peer_reservations_list"};
inline constexpr std::string_view kPeers{"peers"};
inline constexpr std::string_view kPing{"ping"};
inline constexpr std::string_view kPrint{"print"};
inline constexpr std::string_view kRandom{"random"};
inline constexpr std::string_view kRipplePathFind{"ripple_path_find"};
inline constexpr std::string_view kServerDefinitions{"server_definitions"};
inline constexpr std::string_view kServerInfo{"server_info"};
inline constexpr std::string_view kServerState{"server_state"};
inline constexpr std::string_view kSign{"sign"};
inline constexpr std::string_view kSignFor{"sign_for"};
inline constexpr std::string_view kSimulate{"simulate"};
inline constexpr std::string_view kStop{"stop"};
inline constexpr std::string_view kSubmit{"submit"};
inline constexpr std::string_view kSubmitMultisigned{"submit_multisigned"};
inline constexpr std::string_view kSubscribe{"subscribe"};
inline constexpr std::string_view kTransactionEntry{"transaction_entry"};
inline constexpr std::string_view kTx{"tx"};
inline constexpr std::string_view kTxHistory{"tx_history"};
inline constexpr std::string_view kTxReduceRelay{"tx_reduce_relay"};  // no command-line form
inline constexpr std::string_view kUnlList{"unl_list"};
inline constexpr std::string_view kUnsubscribe{"unsubscribe"};
inline constexpr std::string_view kValidationCreate{"validation_create"};
inline constexpr std::string_view kValidatorInfo{"validator_info"};
inline constexpr std::string_view kValidatorListSites{
    "validator_list_sites"};                                  // no command-line form
inline constexpr std::string_view kValidators{"validators"};  // no command-line form
inline constexpr std::string_view kVaultInfo{"vault_info"};
inline constexpr std::string_view kVersion{"version"};
inline constexpr std::string_view kWalletPropose{"wallet_propose"};

}  // namespace xrpl::rpc::method
