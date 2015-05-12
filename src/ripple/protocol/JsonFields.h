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

#define JSS(x) const Json::StaticString x ( #x )

/* The "StaticString" field names are used instead of string literals to
   optimize the performance of accessing members of Json::Value objects.
*/
JSS ( AL_hit_rate );                // out: GetCounts
JSS ( Account );                    // in: TransactionSign; field.
JSS ( Amount );                     // in: TransactionSign; field.
JSS ( ClearFlag );                  // field.
JSS ( Destination );                // in: TransactionSign; field.
JSS ( Fee );                        // in/out: TransactionSign; field.
JSS ( Flags );                      // in/out: TransactionSign; field.
JSS ( Invalid );                    // out: app/misc/AccountState
JSS ( LimitAmount );                // field.
JSS ( OfferSequence );              // field.
JSS ( Paths );                      // in/out: TransactionSign
JSS ( historical_perminute );       // historical_perminute
JSS ( SLE_hit_rate );               // out: GetCounts
JSS ( SendMax );                    // in: TransactionSign
JSS ( Sequence );                   // in/out: TransactionSign; field.
JSS ( SetFlag );                    // field.
JSS ( TakerGets );                  // field.
JSS ( TakerPays );                  // field.
JSS ( TransactionType );            // in: TransactionSign
JSS ( aborted );                    // out: InboundLedger
JSS ( accepted );                   // out: LedgerToJson, OwnerInfo
JSS ( account );                    // in/out: many
JSS ( accountState );               // out: LedgerToJson
JSS ( accountTreeHash );            // out: ledger/Ledger.cpp
JSS ( account_data );               // out: AccountInfo
JSS ( account_hash );               // out: LedgerToJson
JSS ( account_id );                 // out: WalletPropose
JSS ( account_index );              // in: AccountCurrencies, AccountOffers,
                                    //     AccountInfo, AccountLines,
                                    //     AccountObjects, OwnerInfo
                                    // out: AccountOffers
JSS ( account_objects );            // out: AccountObjects
JSS ( account_root );               // in: LedgerEntry
JSS ( accounts );                   // in: LedgerEntry, Subscribe,
                                    //     handlers/Ledger, Unsubscribe
                                    // out: WalletAccounts
JSS ( accounts_proposed );          // in: Subscribe, Unsubscribe
JSS ( action );                     // out: LedgerEntrySet
JSS ( address );                    // out: PeerImp
JSS ( affected );                   // out: AcceptedLedgerTx
JSS ( age );                        // out: UniqueNodeList, NetworkOPs
JSS ( alternatives );               // out: PathRequest, RipplePathFind
JSS ( amendment_blocked );          // out: NetworkOPs
JSS ( asks );                       // out: Subscribe
JSS ( authorized );                 // out: AccountLines
JSS ( balance );                    // out: AccountLines
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
JSS ( can_delete );                 // out: CanDelete
JSS ( check_nodes );                // in: LedgerCleaner
JSS ( clear );                      // in/out: FetchInfo
JSS ( close_time );                 // in: Application, out: NetworkOPs,
                                    //      LedgerProposal, LedgerToJson
JSS ( close_time_estimated );       // in: Application, out: LedgerToJson
JSS ( close_time_human );           // out: LedgerToJson
JSS ( close_time_offset );          // out: NetworkOPs
JSS ( close_time_resolution );      // in: Application; out: LedgerToJson
JSS ( closed );                     // out: NetworkOPs, LedgerToJson,
                                    //      handlers/Ledger
JSS ( closed_ledger );              // out: NetworkOPs
JSS ( cluster );                    // out: UniqueNodeList, PeerImp
JSS ( code );                       // out: errors
JSS ( command );                    // in: RPCHandler
JSS ( comment );                    // in: UnlAdd
JSS ( complete );                   // out: NetworkOPs, InboundLedger
JSS ( complete_ledgers );           // out: NetworkOPs, PeerImp
JSS ( consensus );                  // out: NetworkOPs, LedgerConsensus
JSS ( converge_time );              // out: NetworkOPs
JSS ( converge_time_s );            // out: NetworkOPs
JSS ( count );                      // in: AccountTx*
JSS ( currency );                   // in: paths/PathRequest, STAmount
                                    // out: paths/Node, STPathSet, STAmount
JSS ( current );                    // out: OwnerInfo
JSS ( data );                       // out: LedgerData
JSS ( date );                       // out: tx/Transaction, NetworkOPs
JSS ( dbKBLedger );                 // out: getCounts
JSS ( dbKBTotal );                  // out: getCounts
JSS ( dbKBTransaction );            // out: getCounts
JSS ( debug_signing );              // in: TransactionSign
JSS ( delivered_amount );           // out: addPaymentDeliveredAmount
JSS ( deprecated );                 // out: WalletSeed
JSS ( descending );                 // in: AccountTx*
JSS ( destination_account );        // in: PathRequest, RipplePathFind
JSS ( destination_amount );         // in: PathRequest, RipplePathFind
JSS ( destination_currencies );     // in: PathRequest, RipplePathFind
JSS ( dir_entry );                  // out: DirectoryEntryIterator
JSS ( dir_index );                  // out: DirectoryEntryIterator
JSS ( dir_root );                   // out: DirectoryEntryIterator
JSS ( directory );                  // in: LedgerEntry
JSS ( enabled );                    // out: AmendmentTable
JSS ( engine_result );              // out: NetworkOPs, TransactionSign, Submit
JSS ( engine_result_code );         // out: NetworkOPs, TransactionSign, Submit
JSS ( engine_result_message );      // out: NetworkOPs, TransactionSign, Submit
JSS ( error );                      // out: error
JSS ( error_code );                 // out: error
JSS ( error_exception );            // out: Submit
JSS ( error_message );              // out: error
JSS ( expand );                     // in: handler/Ledger
JSS ( fail_hard );                  // in: Sign, Submit
JSS ( failed );                     // out: InboundLedger
JSS ( feature );                    // in: Feature
JSS ( features );                   // out: Feature
JSS ( fee_base );                   // out: NetworkOPs
JSS ( fee_mult_max );               // in: TransactionSign
JSS ( fee_ref );                    // out: NetworkOPs
JSS ( fetch_pack );                 // out: NetworkOPs
JSS ( first );                      // out: rpc/Version
JSS ( fix_txns );                   // in: LedgerCleaner
JSS ( flags );                      // out: paths/Node, AccountOffers
JSS ( forward );                    // in: AccountTx
JSS ( freeze );                     // out: AccountLines
JSS ( freeze_peer );                // out: AccountLines
JSS ( full );                       // in: LedgerClearer, handlers/Ledger
JSS ( fullbelow_size );             // in: GetCounts
JSS ( generator );                  // in: LedgerEntry
JSS ( good );                       // out: RPCVersion
JSS ( hash );                       // out: NetworkOPs, InboundLedger,
                                    //      LedgerToJson, STTx; field
JSS ( have_header );                // out: InboundLedger
JSS ( have_state );                 // out: InboundLedger
JSS ( have_transactions );          // out: InboundLedger
JSS ( hostid );                     // out: NetworkOPs
JSS ( id );                         // websocket.
JSS ( ident );                      // in: AccountCurrencies, AccountInfo,
                                    //     OwnerInfo
JSS ( inLedger );                   // out: tx/Transaction
JSS ( inbound );                    // out: PeerImp
JSS ( index );                      // in: LedgerEntry; out: PathState,
                                    //     STLedgerEntry, LedgerEntry,
                                    //     TxHistory, LedgerData;
                                    // field
JSS ( info );                       // out: ServerInfo, ConsensusInfo, FetchInfo
JSS ( internal_command );           // in: Internal
JSS ( io_latency_ms );              // out: NetworkOPs
JSS ( ip );                         // in: Connect, out: OverlayImpl
JSS ( issuer );                     // in: RipplePathFind, Subscribe,
                                    //     Unsubscribe, BookOffers
                                    // out: paths/Node, STPathSet, STAmount
JSS ( key );                        // out: WalletSeed
JSS ( key_type );                   // in/out: WalletPropose, TransactionSign
JSS ( latency );                    // out: PeerImp
JSS ( last );                       // out: RPCVersion
JSS ( last_close );                 // out: NetworkOPs
JSS ( ledger );                     // in: NetworkOPs, LedgerCleaner,
                                    //     LookupLedger
                                    // out: NetworkOPs, PeerImp
JSS ( ledger_current_index );       // out: NetworkOPs, LookupLedger,
                                    //      LedgerCurrent, LedgerAccept
JSS ( ledger_data );                // out: LedgerHeader
JSS ( ledger_hash );                // in: LookupLedger, LedgerRequest,
                                    //     RipplePathFind, TransactionEntry,
                                    //     handlers/Ledger
                                    // out: NetworkOPs, LookupLedger,
                                    //      LedgerClosed, LedgerData
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
JSS ( load );                       // out: NetworkOPs, PeerImp
JSS ( load_base );                  // out: NetworkOPs
JSS ( load_factor );                // out: NetworkOPs
JSS ( load_factor_cluster );        // out: NetworkOPs
JSS ( load_factor_local );          // out: NetworkOPs
JSS ( load_factor_net );            // out: NetworkOPs
JSS ( load_fee );                   // out: LoadFeeTrackImp
JSS ( local );                      // out: resource/Logic.h
JSS ( local_txs );                  // out: GetCounts
JSS ( marker );                     // in/out: AccountTx, AccountOffers,
                                    //         AccountLines, AccountObjects,
                                    //         LedgerData
                                    // in: BookOffers
JSS ( master_key );                 // out: WalletPropose
JSS ( master_seed );                // out: WalletPropose
JSS ( master_seed_hex );            // out: WalletPropose
JSS ( max_ledger );                 // in/out: LedgerCleaner
JSS ( message );                    // error.
JSS ( meta );                       // out: NetworkOPs, AccountTx*, Tx
JSS ( metaData );                   // out: LedgerEntrySet, LedgerToJson
JSS ( metadata );                   // out: TransactionEntry
JSS ( method );                     // RPC
JSS ( min_count );                  // in: GetCounts
JSS ( min_ledger );                 // in: LedgerCleaner
JSS ( missingCommand );             // error
JSS ( name );                       // out: AmendmentTableImpl, PeerImp
JSS ( needed_state_hashes );        // out: InboundLedger
JSS ( needed_transaction_hashes );  // out: InboundLedger
JSS ( network_ledger );             // out: NetworkOPs
JSS ( no_ripple );                  // out: AccountLines
JSS ( no_ripple_peer );             // out: AccountLines
JSS ( node );                       // in: UnlAdd, UnlDelete
                                    // out: LedgerEntrySet, LedgerEntry
JSS ( node_binary );                // out: LedgerEntry
JSS ( node_hit_rate );              // out: GetCounts
JSS ( node_read_bytes );            // out: GetCounts
JSS ( node_reads_hit );             // out: GetCounts
JSS ( node_reads_total );           // out: GetCounts
JSS ( node_writes );                // out: GetCounts
JSS ( node_written_bytes );         // out: GetCounts
JSS ( nodes );                      // out: LedgerEntrySet, PathState
JSS ( offer );                      // in: LedgerEntry
JSS ( offers );                     // out: NetworkOPs, AccountOffers, Subscribe
JSS ( offline );                    // in: TransactionSign
JSS ( offset );                     // in/out: AccountTxOld
JSS ( open );                       // out: handlers/Ledger
JSS ( owner );                      // in: LedgerEntry, out: NetworkOPs
JSS ( owner_funds );                // out: NetworkOPs, AcceptedLedgerTx
JSS ( params );                     // RPC
JSS ( parent_hash );                // out: LedgerToJson
JSS ( partition );                  // in: LogLevel
JSS ( passphrase );                 // in: WalletPropose
JSS ( password );                   // in: Subscribe
JSS ( paths );                      // in: RipplePathFind
JSS ( paths_canonical );            // out: RipplePathFind
JSS ( paths_computed );             // out: PathRequest, RipplePathFind
JSS ( peer );                       // in: AccountLines
JSS ( peer_authorized );            // out: AccountLines
JSS ( peer_id );                    // out: LedgerProposal
JSS ( peer_index );                 // in/out: AccountLines
JSS ( peers );                      // out: InboundLedger, handlers/Peers
JSS ( port );                       // in: Connect
JSS ( previous_ledger );            // out: LedgerPropose
JSS ( proof );                      // in: BookOffers
JSS ( propose_seq );                // out: LedgerPropose
JSS ( proposers );                  // out: NetworkOPs, LedgerConsensus
JSS ( protocol );                   // out: PeerImp
JSS ( pubkey_node );                // out: NetworkOPs
JSS ( pubkey_validator );           // out: NetworkOPs
JSS ( public_key );                 // out: OverlayImpl, PeerImp, WalletPropose
JSS ( public_key_hex );             // out: WalletPropose
JSS ( published_ledger );           // out: NetworkOPs
JSS ( quality );                    // out: NetworkOPs
JSS ( quality_in );                 // out: AccountLines
JSS ( quality_out );                // out: AccountLines
JSS ( random );                     // out: Random
JSS ( raw_meta );                   // out: AcceptedLedgerTx
JSS ( receive_currencies );         // out: AccountCurrencies
JSS ( regular_seed );               // in/out: LedgerEntry
JSS ( remote );                     // out: Logic.h
JSS ( request );                    // RPC
JSS ( reserve_base );               // out: NetworkOPs
JSS ( reserve_base_xrp );           // out: NetworkOPs
JSS ( reserve_inc );                // out: NetworkOPs
JSS ( reserve_inc_xrp );            // out: NetworkOPs
JSS ( response );                   // websocket
JSS ( result );                     // RPC
JSS ( ripple_lines );               // out: NetworkOPs
JSS ( ripple_state );               // in: LedgerEntr
JSS ( rt_accounts );                // in: Subscribe, Unsubscribe
JSS ( sanity );                     // out: PeerImp
JSS ( search_depth );               // in: RipplePathFind
JSS ( secret );                     // in: TransactionSign, WalletSeed,
                                    //     ValidationCreate, ValidationSeed
JSS ( seed );                       // in: WalletAccounts, out: WalletSeed
JSS ( seed_hex );                   // in: WalletPropose, TransactionSign
JSS ( send_currencies );            // out: AccountCurrencies
JSS ( seq );                        // in: LedgerEntry;
                                    // out: NetworkOPs, RPCSub, AccountOffers
JSS ( seqNum );                     // out: LedgerToJson
JSS ( server_state );               // out: NetworkOPs
JSS ( server_status );              // out: NetworkOPs
JSS ( severity );                   // in: LogLevel
JSS ( snapshot );                   // in: Subscribe
JSS ( source_account );             // in: PathRequest, RipplePathFind
JSS ( source_amount );              // in: PathRequest, RipplePathFind
JSS ( source_currencies );          // in: PathRequest, RipplePathFind
JSS ( stand_alone );                // out: NetworkOPs
JSS ( start );                      // in: TxHistory
JSS ( state );                      // out: Logic.h, ServerState, LedgerData
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
JSS ( taker );                      // in: Subscribe, BookOffers
JSS ( taker_gets );                 // in: Subscribe, Unsubscribe, BookOffers
JSS ( taker_gets_funded );          // out: NetworkOPs
JSS ( taker_pays );                 // in: Subscribe, Unsubscribe, BookOffers
JSS ( taker_pays_funded );          // out: NetworkOPs
JSS ( threshold );                  // in: Blacklist
JSS ( timeouts );                   // out: InboundLedger
JSS ( totalCoins );                 // out: LedgerToJson
JSS ( total_coins );                // out: LedgerToJson
JSS ( transTreeHash );              // out: ledger/Ledger.cpp
JSS ( transaction );                // in: Tx
                                    // out: NetworkOPs, AcceptedLedgerTx,
JSS ( transaction_hash );           // out: LedgerProposal, LedgerToJson
JSS ( transactions );               // out: LedgerToJson,
                                    // in: AccountTx*, Unsubscribe
JSS ( treenode_cache_size );        // out: GetCounts
JSS ( treenode_track_size );        // out: GetCounts
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
                                    // out: NetworkOPs, LedgerEntrySet
                                    //      paths/Node.cpp, OverlayImpl, Logic
JSS ( type_hex );                   // out: STPathSet
JSS ( unl );                        // out: UnlList
JSS ( uptime );                     // out: GetCounts
JSS ( url );                        // in/out: Subscribe, Unsubscribe
JSS ( url_password );               // in: Subscribe
JSS ( url_username );               // in: Subscribe
JSS ( urlgravatar );                // out: AccountState
JSS ( username );                   // in: Subscribe
JSS ( validated );                  // out: NetworkOPs, LookupLedger, AccountTx*
                                    //      Tx
JSS ( validated_ledger );           // out: NetworkOPs
JSS ( validated_ledgers );          // out: NetworkOPs
JSS ( validation_key );             // out: ValidationCreate, ValidationSeed
JSS ( validation_public_key );      // out: ValidationCreate, ValidationSeed
JSS ( validation_quorum );          // out: NetworkOPs
JSS ( validation_seed );            // out: ValidationCreate, ValidationSeed
JSS ( value );                      // out: STAmount
JSS ( version );                    // out: RPCVersion
JSS ( vetoed );                     // out: AmendmentTableImpl
JSS ( vote );                       // in: Feature
JSS ( warning );                    // rpc:
JSS ( write_load );                 // out: GetCounts

#undef JSS

} // jss
} // ripple

#endif
