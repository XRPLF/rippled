//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_JSONFIELDS_H_INCLUDED
#define RIPPLE_PROTOCOL_JSONFIELDS_H_INCLUDED

#include <ripple/json/json_value.h>

namespace ripple {
namespace jss {

// JSON static strings

#define JSS(x) constexpr ::Json::StaticString x ( #x )

/* These "StaticString" field names are used instead of string literals to
   optimize the performance of accessing properties of Json::Value objects.

   Most strings have a trailing comment. Here is the legend:

   in: Read by the given RPC handler from its `Json::Value` parameter.
   out: Assigned by the given RPC handler in the `Json::Value` it returns.
   field: A field of at least one type of transaction.
   RPC: Common properties of RPC requests and responses.
   error: Common properties of RPC error responses.
*/

JSS ( AL_hit_rate );                // out: GetCounts
JSS ( Account );                    // in: TransactionSign; field.
JSS ( AccountDelete );              // transaction type.
JSS ( AccountRoot );                // ledger type.
JSS ( AccountSet );                 // transaction type.
JSS ( Amendments );                 // ledger type.
JSS ( Amount );                     // in: TransactionSign; field.
JSS ( Check );                      // ledger type.
JSS ( CheckCancel );                // transaction type.
JSS ( CheckCash );                  // transaction type.
JSS ( CheckCreate );                // transaction type.
JSS ( ClearFlag );                  // field.
JSS ( DeliverMin );                 // in: TransactionSign
JSS ( DepositPreauth );             // transaction and ledger type.
JSS ( Destination );                // in: TransactionSign; field.
JSS ( DirectoryNode );              // ledger type.
JSS ( EnableAmendment );            // transaction type.
JSS ( Escrow );                     // ledger type.
JSS ( EscrowCancel );               // transaction type.
JSS ( EscrowCreate );               // transaction type.
JSS ( EscrowFinish );               // transaction type.
JSS ( Fee );                        // in/out: TransactionSign; field.
JSS ( FeeSettings );                // ledger type.
JSS ( Flags );                      // in/out: TransactionSign; field.
JSS ( Invalid );                    //
JSS ( LastLedgerSequence );         // in: TransactionSign; field
JSS ( LedgerHashes );               // ledger type.
JSS ( LimitAmount );                // field.
JSS ( Offer );                      // ledger type.
JSS ( OfferCancel );                // transaction type.
JSS ( OfferCreate );                // transaction type.
JSS ( OfferSequence );              // field.
JSS ( Paths );                      // in/out: TransactionSign
JSS ( PayChannel );                 // ledger type.
JSS ( Payment );                    // transaction type.
JSS ( PaymentChannelClaim );        // transaction type.
JSS ( PaymentChannelCreate );       // transaction type.
JSS ( PaymentChannelFund );         // transaction type.
JSS ( RippleState );                // ledger type.
JSS ( SLE_hit_rate );               // out: GetCounts.
JSS ( SetFee );                     // transaction type.
JSS ( SettleDelay );                // in: TransactionSign
JSS ( SendMax );                    // in: TransactionSign
JSS ( Sequence );                   // in/out: TransactionSign; field.
JSS ( SetFlag );                    // field.
JSS ( SetRegularKey );              // transaction type.
JSS ( SignerList );                 // ledger type.
JSS ( SignerListSet );              // transaction type.
JSS ( SigningPubKey );              // field.
JSS ( TakerGets );                  // field.
JSS ( TakerPays );                  // field.
JSS ( Ticket );                     // ledger type.
JSS ( TicketCancel );               // transaction type.
JSS ( TicketCreate );               // transaction type.
JSS ( TxnSignature );               // field.
JSS ( TransactionType );            // in: TransactionSign.
JSS ( TransferRate );               // in: TransferRate.
JSS ( TrustSet );                   // transaction type.
JSS ( aborted );                    // out: InboundLedger
JSS ( accepted );                   // out: LedgerToJson, OwnerInfo
JSS ( account );                    // in/out: many
JSS ( accountState );               // out: LedgerToJson
JSS ( accountTreeHash );            // out: ledger/Ledger.cpp
JSS ( account_data );               // out: AccountInfo
JSS ( account_hash );               // out: LedgerToJson
JSS ( account_id );                 // out: WalletPropose
JSS ( account_objects );            // out: AccountObjects
JSS ( account_root );               // in: LedgerEntry
JSS ( accounts );                   // in: LedgerEntry, Subscribe,
                                    //     handlers/Ledger, Unsubscribe
JSS ( accounts_proposed );          // in: Subscribe, Unsubscribe
JSS ( action );
JSS ( acquiring );                  // out: LedgerRequest
JSS ( address );                    // out: PeerImp
JSS ( affected );                   // out: AcceptedLedgerTx
JSS ( age );                        // out: NetworkOPs, Peers
JSS ( alternatives );               // out: PathRequest, RipplePathFind
JSS ( amendment_blocked );          // out: NetworkOPs
JSS ( amendments );                 // in: AccountObjects, out: NetworkOPs
JSS ( amount );                     // out: AccountChannels
JSS ( api_version);                 // in: many, out: Version
JSS ( api_version_low);             // out: Version
JSS ( asks );                       // out: Subscribe
JSS ( assets );                     // out: GatewayBalances
JSS ( authorized );                 // out: AccountLines
JSS ( auth_change );                // out: AccountInfo
JSS ( auth_change_queued );         // out: AccountInfo
JSS ( available );                  // out: ValidatorList
JSS ( avg_bps_recv );               // out: Peers
JSS ( avg_bps_sent );               // out: Peers
JSS ( balance );                    // out: AccountLines
JSS ( balances );                   // out: GatewayBalances
JSS ( base );                       // out: LogLevel
JSS ( base_fee );                   // out: NetworkOPs
JSS ( base_fee_xrp );               // out: NetworkOPs
JSS ( bids );                       // out: Subscribe
JSS ( binary );                     // in: AccountTX, LedgerEntry,
                                    //     AccountTxOld, Tx LedgerData
JSS ( books );                      // in: Subscribe, Unsubscribe
JSS ( both );                       // in: Subscribe, Unsubscribe
JSS ( both_sides );                 // in: Subscribe, Unsubscribe
JSS ( build_path );                 // in: TransactionSign
JSS ( build_version );              // out: NetworkOPs
JSS ( cancel_after );               // out: AccountChannels
JSS ( can_delete );                 // out: CanDelete
JSS ( channel_id );                 // out: AccountChannels
JSS ( channels );                   // out: AccountChannels
JSS ( check );                      // in: AccountObjects
JSS ( check_nodes );                // in: LedgerCleaner
JSS ( clear );                      // in/out: FetchInfo
JSS ( close_flags );                // out: LedgerToJson
JSS ( close_time );                 // in: Application, out: NetworkOPs,
                                    //      RCLCxPeerPos, LedgerToJson
JSS ( close_time_estimated );       // in: Application, out: LedgerToJson
JSS ( close_time_human );           // out: LedgerToJson
JSS ( close_time_offset );          // out: NetworkOPs
JSS ( close_time_resolution );      // in: Application; out: LedgerToJson
JSS ( closed );                     // out: NetworkOPs, LedgerToJson,
                                    //      handlers/Ledger
JSS ( closed_ledger );              // out: NetworkOPs
JSS ( cluster );                    // out: PeerImp
JSS ( code );                       // out: errors
JSS ( command );                    // in: RPCHandler
JSS ( complete );                   // out: NetworkOPs, InboundLedger
JSS ( complete_ledgers );           // out: NetworkOPs, PeerImp
JSS ( complete_shards );            // out: OverlayImpl, PeerImp
JSS ( consensus );                  // out: NetworkOPs, LedgerConsensus
JSS ( converge_time );              // out: NetworkOPs
JSS ( converge_time_s );            // out: NetworkOPs
JSS ( count );                      // in: AccountTx*, ValidatorList
JSS ( counters );                   // in/out: retrieve counters
JSS ( currency );                   // in: paths/PathRequest, STAmount
                                    // out: paths/Node, STPathSet, STAmount,
                                    //      AccountLines
JSS ( current );                    // out: OwnerInfo
JSS ( current_activities );
JSS ( current_ledger_size );        // out: TxQ
JSS ( current_queue_size );         // out: TxQ
JSS ( data );                       // out: LedgerData
JSS ( date );                       // out: tx/Transaction, NetworkOPs
JSS ( dbKBLedger );                 // out: getCounts
JSS ( dbKBTotal );                  // out: getCounts
JSS ( dbKBTransaction );            // out: getCounts
JSS ( debug_signing );              // in: TransactionSign
JSS ( deletion_blockers_only );     // in: AccountObjects
JSS ( delivered_amount );           // out: insertDeliveredAmount
JSS ( deposit_authorized );         // out: deposit_authorized
JSS ( deposit_preauth );            // in: AccountObjects, LedgerData
JSS ( deprecated );                 // out
JSS ( descending );                 // in: AccountTx*
JSS ( description );                // in/out: Reservations
JSS ( destination_account );        // in: PathRequest, RipplePathFind, account_lines
                                    // out: AccountChannels
JSS ( destination_amount );         // in: PathRequest, RipplePathFind
JSS ( destination_currencies );     // in: PathRequest, RipplePathFind
JSS ( destination_tag );            // in: PathRequest
                                    // out: AccountChannels
JSS ( dir_entry );                  // out: DirectoryEntryIterator
JSS ( dir_index );                  // out: DirectoryEntryIterator
JSS ( dir_root );                   // out: DirectoryEntryIterator
JSS ( directory );                  // in: LedgerEntry
JSS ( drops );                      // out: TxQ
JSS ( duration_us );                // out: NetworkOPs
JSS ( enabled );                    // out: AmendmentTable
JSS ( engine_result );              // out: NetworkOPs, TransactionSign, Submit
JSS ( engine_result_code );         // out: NetworkOPs, TransactionSign, Submit
JSS ( engine_result_message );      // out: NetworkOPs, TransactionSign, Submit
JSS ( error );                      // out: error
JSS ( errored );
JSS ( error_code );                 // out: error
JSS ( error_exception );            // out: Submit
JSS ( error_message );              // out: error
JSS ( escrow );                     // in: LedgerEntry
JSS ( expand );                     // in: handler/Ledger
JSS ( expected_ledger_size );       // out: TxQ
JSS ( expiration );                 // out: AccountOffers, AccountChannels,
                                    //      ValidatorList
JSS ( fail_hard );                  // in: Sign, Submit
JSS ( failed );                     // out: InboundLedger
JSS ( feature );                    // in: Feature
JSS ( features );                   // out: Feature
JSS ( fee );                        // out: NetworkOPs, Peers
JSS ( fee_base );                   // out: NetworkOPs
JSS ( fee_div_max );                // in: TransactionSign
JSS ( fee_level );                  // out: AccountInfo
JSS ( fee_mult_max );               // in: TransactionSign
JSS ( fee_ref );                    // out: NetworkOPs
JSS ( fetch_pack );                 // out: NetworkOPs
JSS ( first );                      // out: rpc/Version
JSS ( finished );
JSS ( fix_txns );                   // in: LedgerCleaner
JSS ( flags );                      // out: paths/Node, AccountOffers,
                                    //      NetworkOPs
JSS ( forward );                    // in: AccountTx
JSS ( freeze );                     // out: AccountLines
JSS ( freeze_peer );                // out: AccountLines
JSS ( frozen_balances );            // out: GatewayBalances
JSS ( full );                       // in: LedgerClearer, handlers/Ledger
JSS ( full_reply );                 // out: PathFind
JSS ( fullbelow_size );             // out: GetCounts
JSS ( good );                       // out: RPCVersion
JSS ( hash );                       // out: NetworkOPs, InboundLedger,
                                    //      LedgerToJson, STTx; field
JSS ( hashes );                     // in: AccountObjects
JSS ( have_header );                // out: InboundLedger
JSS ( have_state );                 // out: InboundLedger
JSS ( have_transactions );          // out: InboundLedger
JSS ( highest_sequence );           // out: AccountInfo
JSS ( historical_perminute );       // historical_perminute.
JSS ( hostid );                     // out: NetworkOPs
JSS ( hotwallet );                  // in: GatewayBalances
JSS ( id );                         // websocket.
JSS ( ident );                      // in: AccountCurrencies, AccountInfo,
                                    //     OwnerInfo
JSS ( inLedger );                   // out: tx/Transaction
JSS ( inbound );                    // out: PeerImp
JSS ( index );                      // in: LedgerEntry, DownloadShard
                                    // out: PathState, STLedgerEntry,
                                    //      LedgerEntry, TxHistory, LedgerData
                                    // field
JSS ( info );                       // out: ServerInfo, ConsensusInfo, FetchInfo
JSS ( internal_command );           // in: Internal
JSS ( invalid_API_version );        // out: Many, when a request has an invalid
                                    //      version
JSS ( io_latency_ms );              // out: NetworkOPs
JSS ( ip );                         // in: Connect, out: OverlayImpl
JSS ( issuer );                     // in: RipplePathFind, Subscribe,
                                    //     Unsubscribe, BookOffers
                                    // out: paths/Node, STPathSet, STAmount
JSS ( job );
JSS ( job_queue );
JSS ( jobs );
JSS ( jsonrpc );                    // json version
JSS ( jq_trans_overflow );          // JobQueue transaction limit overflow.
JSS ( key );                        // out
JSS ( key_type );                   // in/out: WalletPropose, TransactionSign
JSS ( latency );                    // out: PeerImp
JSS ( last );                       // out: RPCVersion
JSS ( last_close );                 // out: NetworkOPs
JSS ( last_refresh_time );          // out: ValidatorSite
JSS ( last_refresh_status );        // out: ValidatorSite
JSS ( last_refresh_message );       // out: ValidatorSite
JSS ( ledger );                     // in: NetworkOPs, LedgerCleaner,
                                    //     RPCHelpers
                                    // out: NetworkOPs, PeerImp
JSS ( ledger_current_index );       // out: NetworkOPs, RPCHelpers,
                                    //      LedgerCurrent, LedgerAccept,
                                    //      AccountLines
JSS ( ledger_data );                // out: LedgerHeader
JSS ( ledger_hash );                // in: RPCHelpers, LedgerRequest,
                                    //     RipplePathFind, TransactionEntry,
                                    //     handlers/Ledger
                                    // out: NetworkOPs, RPCHelpers,
                                    //      LedgerClosed, LedgerData,
                                    //      AccountLines
JSS ( ledger_hit_rate );            // out: GetCounts
JSS ( ledger_index );               // in/out: many
JSS ( ledger_index_max );           // in, out: AccountTx*
JSS ( ledger_index_min );           // in, out: AccountTx*
JSS ( ledger_max );                 // in, out: AccountTx*
JSS ( ledger_min );                 // in, out: AccountTx*
JSS ( ledger_time );                // out: NetworkOPs
JSS ( levels );                     // LogLevels
JSS ( limit );                      // in/out: AccountTx*, AccountOffers,
                                    //         AccountLines, AccountObjects
                                    // in: LedgerData, BookOffers
JSS ( limit_peer );                 // out: AccountLines
JSS ( lines );                      // out: AccountLines
JSS ( list );                       // out: ValidatorList
JSS ( load );                       // out: NetworkOPs, PeerImp
JSS ( load_base );                  // out: NetworkOPs
JSS ( load_factor );                // out: NetworkOPs
JSS ( load_factor_cluster );        // out: NetworkOPs
JSS ( load_factor_fee_escalation ); // out: NetworkOPs
JSS ( load_factor_fee_queue );      // out: NetworkOPs
JSS ( load_factor_fee_reference );  // out: NetworkOPs
JSS ( load_factor_local );          // out: NetworkOPs
JSS ( load_factor_net );            // out: NetworkOPs
JSS ( load_factor_server );         // out: NetworkOPs
JSS ( load_fee );                   // out: LoadFeeTrackImp, NetworkOPs
JSS ( local );                      // out: resource/Logic.h
JSS ( local_txs );                  // out: GetCounts
JSS ( local_static_keys );          // out: ValidatorList
JSS ( lowest_sequence );            // out: AccountInfo
JSS ( majority );                   // out: RPC feature
JSS ( marker );                     // in/out: AccountTx, AccountOffers,
                                    //         AccountLines, AccountObjects,
                                    //         LedgerData
                                    // in: BookOffers
JSS ( master_key );                 // out: WalletPropose, NetworkOPs
JSS ( master_seed );                // out: WalletPropose
JSS ( master_seed_hex );            // out: WalletPropose
JSS ( master_signature );           // out: pubManifest
JSS ( max_ledger );                 // in/out: LedgerCleaner
JSS ( max_queue_size );             // out: TxQ
JSS ( max_spend_drops );            // out: AccountInfo
JSS ( max_spend_drops_total );      // out: AccountInfo
JSS ( median_fee );                 // out: TxQ
JSS ( median_level );               // out: TxQ
JSS ( message );                    // error.
JSS ( meta );                       // out: NetworkOPs, AccountTx*, Tx
JSS ( metaData );
JSS ( metadata );                   // out: TransactionEntry
JSS ( method );                     // RPC
JSS ( methods );
JSS ( metrics );                    // out: Peers
JSS ( min_count );                  // in: GetCounts
JSS ( min_ledger );                 // in: LedgerCleaner
JSS ( minimum_fee );                // out: TxQ
JSS ( minimum_level );              // out: TxQ
JSS ( missingCommand );             // error
JSS ( name );                       // out: AmendmentTableImpl, PeerImp
JSS ( needed_state_hashes );        // out: InboundLedger
JSS ( needed_transaction_hashes );  // out: InboundLedger
JSS ( network_ledger );             // out: NetworkOPs
JSS ( next_refresh_time );          // out: ValidatorSite
JSS ( no_ripple );                  // out: AccountLines
JSS ( no_ripple_peer );             // out: AccountLines
JSS ( node );                       // out: LedgerEntry
JSS ( node_binary );                // out: LedgerEntry
JSS ( node_hit_rate );              // out: GetCounts
JSS ( node_read_bytes );            // out: GetCounts
JSS ( node_reads_hit );             // out: GetCounts
JSS ( node_reads_total );           // out: GetCounts
JSS ( node_writes );                // out: GetCounts
JSS ( node_written_bytes );         // out: GetCounts
JSS ( nodes );                      // out: PathState
JSS ( obligations );                // out: GatewayBalances
JSS ( offer );                      // in: LedgerEntry
JSS ( offers );                     // out: NetworkOPs, AccountOffers, Subscribe
JSS ( offline );                    // in: TransactionSign
JSS ( offset );                     // in/out: AccountTxOld
JSS ( open );                       // out: handlers/Ledger
JSS ( open_ledger_fee );            // out: TxQ
JSS ( open_ledger_level );          // out: TxQ
JSS ( owner );                      // in: LedgerEntry, out: NetworkOPs
JSS ( owner_funds );                // in/out: Ledger, NetworkOPs, AcceptedLedgerTx
JSS ( params );                     // RPC
JSS ( parent_close_time );          // out: LedgerToJson
JSS ( parent_hash );                // out: LedgerToJson
JSS ( partition );                  // in: LogLevel
JSS ( passphrase );                 // in: WalletPropose
JSS ( password );                   // in: Subscribe
JSS ( paths );                      // in: RipplePathFind
JSS ( paths_canonical );            // out: RipplePathFind
JSS ( paths_computed );             // out: PathRequest, RipplePathFind
JSS ( payment_channel );            // in: LedgerEntry
JSS ( peer );                       // in: AccountLines
JSS ( peer_authorized );            // out: AccountLines
JSS ( peer_id );                    // out: RCLCxPeerPos
JSS ( peers );                      // out: InboundLedger, handlers/Peers, Overlay
JSS ( peer_disconnects );           // Severed peer connection counter.
JSS ( peer_disconnects_resources ); // Severed peer connections because of
                                    // excess resource consumption.
JSS ( port );                       // in: Connect
JSS ( previous );                   // out: Reservations
JSS ( previous_ledger );            // out: LedgerPropose
JSS ( proof );                      // in: BookOffers
JSS ( propose_seq );                // out: LedgerPropose
JSS ( proposers );                  // out: NetworkOPs, LedgerConsensus
JSS ( protocol );                   // out: PeerImp
JSS ( proxied );                    // out: RPC ping
JSS ( pubkey_node );                // out: NetworkOPs
JSS ( pubkey_publisher );           // out: ValidatorList
JSS ( pubkey_validator );           // out: NetworkOPs, ValidatorList
JSS ( public_key );                 // out: OverlayImpl, PeerImp, WalletPropose
JSS ( public_key_hex );             // out: WalletPropose
JSS ( published_ledger );           // out: NetworkOPs
JSS ( publisher_lists );            // out: ValidatorList
JSS ( quality );                    // out: NetworkOPs
JSS ( quality_in );                 // out: AccountLines
JSS ( quality_out );                // out: AccountLines
JSS ( queue );                      // in: AccountInfo
JSS ( queue_data );                 // out: AccountInfo
JSS ( queued );
JSS ( queued_duration_us );
JSS ( random );                     // out: Random
JSS ( raw_meta );                   // out: AcceptedLedgerTx
JSS ( receive_currencies );         // out: AccountCurrencies
JSS ( reference_level );            // out: TxQ
JSS ( refresh_interval_min );       // out: ValidatorSites
JSS ( regular_seed );               // in/out: LedgerEntry
JSS ( remote );                     // out: Logic.h
JSS ( request );                    // RPC
JSS ( reservations );               // out: Reservations
JSS ( reserve_base );               // out: NetworkOPs
JSS ( reserve_base_xrp );           // out: NetworkOPs
JSS ( reserve_inc );                // out: NetworkOPs
JSS ( reserve_inc_xrp );            // out: NetworkOPs
JSS ( response );                   // websocket
JSS ( result );                     // RPC
JSS ( ripple_lines );               // out: NetworkOPs
JSS ( ripple_state );               // in: LedgerEntr
JSS ( ripplerpc );                  // ripple RPC version
JSS ( role );                       // out: Ping.cpp
JSS ( rpc );
JSS ( rt_accounts );                // in: Subscribe, Unsubscribe
JSS ( running_duration_us );
JSS ( sanity );                     // out: PeerImp
JSS ( search_depth );               // in: RipplePathFind
JSS ( secret );                     // in: TransactionSign,
                                    //     ValidationCreate, ValidationSeed,
                                    //     channel_authorize
JSS ( seed );                       //
JSS ( seed_hex );                   // in: WalletPropose, TransactionSign
JSS ( send_currencies );            // out: AccountCurrencies
JSS ( send_max );                   // in: PathRequest, RipplePathFind
JSS ( seq );                        // in: LedgerEntry;
                                    // out: NetworkOPs, RPCSub, AccountOffers,
                                    //      ValidatorList
JSS ( seqNum );                     // out: LedgerToJson
JSS ( server_state );               // out: NetworkOPs
JSS ( server_state_duration_us );   // out: NetworkOPs
JSS ( server_status );              // out: NetworkOPs
JSS ( settle_delay );               // out: AccountChannels
JSS ( severity );                   // in: LogLevel
JSS ( shards );                     // in/out: GetCounts, DownloadShard
JSS ( signature );                  // out: NetworkOPs, ChannelAuthorize
JSS ( signature_verified );         // out: ChannelVerify
JSS ( signing_key );                // out: NetworkOPs
JSS ( signing_keys );               // out: ValidatorList
JSS ( signing_time );               // out: NetworkOPs
JSS ( signer_list );                // in: AccountObjects
JSS ( signer_lists );               // in/out: AccountInfo
JSS ( snapshot );                   // in: Subscribe
JSS ( source_account );             // in: PathRequest, RipplePathFind
JSS ( source_amount );              // in: PathRequest, RipplePathFind
JSS ( source_currencies );          // in: PathRequest, RipplePathFind
JSS ( source_tag );                 // out: AccountChannels
JSS ( stand_alone );                // out: NetworkOPs
JSS ( start );                      // in: TxHistory
JSS ( started );
JSS ( state );                      // out: Logic.h, ServerState, LedgerData
JSS ( state_accounting );           // out: NetworkOPs
JSS ( state_now );                  // in: Subscribe
JSS ( status );                     // error
JSS ( stop );                       // in: LedgerCleaner
JSS ( streams );                    // in: Subscribe, Unsubscribe
JSS ( strict );                     // in: AccountCurrencies, AccountInfo
JSS ( sub_index );                  // in: LedgerEntry
JSS ( subcommand );                 // in: PathFind
JSS ( success );                    // rpc
JSS ( supported );                  // out: AmendmentTableImpl
JSS ( system_time_offset );         // out: NetworkOPs
JSS ( tag );                        // out: Peers
JSS ( taker );                      // in: Subscribe, BookOffers
JSS ( taker_gets );                 // in: Subscribe, Unsubscribe, BookOffers
JSS ( taker_gets_funded );          // out: NetworkOPs
JSS ( taker_pays );                 // in: Subscribe, Unsubscribe, BookOffers
JSS ( taker_pays_funded );          // out: NetworkOPs
JSS ( threshold );                  // in: Blacklist
JSS ( ticket );                     // in: AccountObjects
JSS ( time );
JSS ( timeouts );                   // out: InboundLedger
JSS ( traffic );                    // out: Overlay
JSS ( total );                      // out: counters
JSS ( totalCoins );                 // out: LedgerToJson
JSS ( total_bytes_recv );           // out: Peers
JSS ( total_bytes_sent );           // out: Peers
JSS ( total_coins );                // out: LedgerToJson
JSS ( transTreeHash );              // out: ledger/Ledger.cpp
JSS ( transaction );                // in: Tx
                                    // out: NetworkOPs, AcceptedLedgerTx,
JSS ( transaction_hash );           // out: RCLCxPeerPos, LedgerToJson
JSS ( transactions );               // out: LedgerToJson,
                                    // in: AccountTx*, Unsubscribe
JSS ( transitions );                // out: NetworkOPs
JSS ( treenode_cache_size );        // out: GetCounts
JSS ( treenode_track_size );        // out: GetCounts
JSS ( trusted );                    // out: UnlList
JSS ( trusted_validator_keys );     // out: ValidatorList
JSS ( tx );                         // out: STTx, AccountTx*
JSS ( tx_blob );                    // in/out: Submit,
                                    // in: TransactionSign, AccountTx*
JSS ( tx_hash );                    // in: TransactionEntry
JSS ( tx_json );                    // in/out: TransactionSign
                                    // out: TransactionEntry
JSS ( tx_signing_hash );            // out: TransactionSign
JSS ( tx_unsigned );                // out: TransactionSign
JSS ( txn_count );                  // out: NetworkOPs
JSS ( txs );                        // out: TxHistory
JSS ( type );                       // in: AccountObjects
                                    // out: NetworkOPs
                                    //      paths/Node.cpp, OverlayImpl, Logic
JSS ( type_hex );                   // out: STPathSet
JSS ( unl );                        // out: UnlList
JSS ( unlimited);                   // out: Connection.h
JSS ( uptime );                     // out: GetCounts
JSS ( uri );                        // out: ValidatorSites
JSS ( url );                        // in/out: Subscribe, Unsubscribe
JSS ( url_password );               // in: Subscribe
JSS ( url_username );               // in: Subscribe
JSS ( urlgravatar );                //
JSS ( username );                   // in: Subscribe
JSS ( validate );                   // in: DownloadShard
JSS ( validated );                  // out: NetworkOPs, RPCHelpers, AccountTx*
                                    //      Tx
JSS ( validator_list_expires );     // out: NetworkOps, ValidatorList
JSS ( validator_list );             // out: NetworkOps, ValidatorList
JSS ( validators );
JSS ( validated_ledger );           // out: NetworkOPs
JSS ( validated_ledgers );          // out: NetworkOPs
JSS ( validation_key );             // out: ValidationCreate, ValidationSeed
JSS ( validation_private_key );     // out: ValidationCreate
JSS ( validation_public_key );      // out: ValidationCreate, ValidationSeed
JSS ( validation_quorum );          // out: NetworkOPs
JSS ( validation_seed );            // out: ValidationCreate, ValidationSeed
JSS ( validations );                // out: AmendmentTableImpl
JSS ( validator_sites );            // out: ValidatorSites
JSS ( value );                      // out: STAmount
JSS ( version );                    // out: RPCVersion
JSS ( vetoed );                     // out: AmendmentTableImpl
JSS ( vote );                       // in: Feature
JSS ( warning );                    // rpc:
JSS ( workers );
JSS ( write_load );                 // out: GetCounts

#undef JSS

} // jss
} // ripple

#endif
