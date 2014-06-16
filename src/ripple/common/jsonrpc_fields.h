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

#ifndef RIPPLE_COMMON_JSONRPC_FIELDS_H_INCLUDED
#define RIPPLE_COMMON_JSONRPC_FIELDS_H_INCLUDED

#include <ripple/unity/json.h>

namespace ripple {
namespace jss {

// JSON static strings

#define JSS(x) const Json::StaticString x ( #x )

/* The "StaticString" field names are used instead of string literals to
   optimize the performance of accessing members of Json::Value objects.
*/
// VFALCO NOTE Some of these are part of the JSON-RPC API and some aren't
//        TODO Move the string not part of the JSON-RPC API into another file
JSS ( accepted );
JSS ( account );
JSS ( account_hash );
JSS ( account_index );
JSS ( accountState );
JSS ( accountTreeHash );
JSS ( affected );
JSS ( age );
JSS ( amendment_blocked );
JSS ( asks );
JSS ( authorized );
JSS ( balance );
JSS ( base_fee );
JSS ( base_fee_xrp );
JSS ( bids );
JSS ( binary );
JSS ( build_version );
JSS ( closed );
JSS ( closed_ledger );
JSS ( close_time );
JSS ( close_time_estimated );
JSS ( close_time_human );
JSS ( close_time_resolution );
JSS ( code );
JSS ( command );
JSS ( complete_ledgers );
JSS ( consensus );
JSS ( converge_time );
JSS ( converge_time_s );
JSS ( currency );
JSS ( date );
JSS ( engine_result );
JSS ( engine_result_code );
JSS ( engine_result_message );
JSS ( error );
JSS ( error_exception );
JSS ( fee_base );
JSS ( fee_ref );
JSS ( fetch_pack );
JSS ( flags );
JSS ( hash );
JSS ( hostid );
JSS ( id );
JSS ( issuer );
JSS ( last_close );
JSS ( ledger );
JSS ( ledgerClosed );
JSS ( ledger_current_index );
JSS ( ledger_hash );
JSS ( ledger_index );
JSS ( ledger_index_max );
JSS ( ledger_index_min );
JSS ( ledger_time );
JSS ( limit );
JSS ( limit_peer );
JSS ( lines );
JSS ( load );
JSS ( load_base );
JSS ( load_factor );
JSS ( load_factor_cluster );
JSS ( load_factor_local );
JSS ( load_factor_net );
JSS ( load_fee );
JSS ( marker );
JSS ( message );
JSS ( meta );
JSS ( metaData );
JSS ( method );
JSS ( missingCommand );
JSS ( name );
JSS ( network_ledger );
JSS ( none );
JSS ( no_ripple );
JSS ( no_ripple_peer );
JSS ( offers );
JSS ( params );
JSS ( parent_hash );
JSS ( peer );
JSS ( peer_authorized );
JSS ( peer_index );
JSS ( peers );
JSS ( proposed );
JSS ( proposers );
JSS ( pubkey_node );
JSS ( pubkey_validator );
JSS ( published_ledger );
JSS ( quality );
JSS ( quality_in );
JSS ( quality_out );
JSS ( random );
JSS ( raw_meta );
JSS ( request );
JSS ( reserve_base );
JSS ( reserve_base_xrp );
JSS ( reserve_inc );
JSS ( reserve_inc_xrp );
JSS ( response );
JSS ( result );
JSS ( ripple_lines );
JSS ( seq );
JSS ( seqNum );
JSS ( server_state );
JSS ( server_status );
JSS ( stand_alone );
JSS ( status );
JSS ( success );
JSS ( taker_gets );
JSS ( taker_gets_funded );
JSS ( taker_pays );
JSS ( taker_pays_funded );
JSS ( total_coins );
JSS ( totalCoins );
JSS ( transaction );
JSS ( transaction_hash );
JSS ( transactions );
JSS ( transTreeHash );
JSS ( tx );
JSS ( tx_blob );
JSS ( tx_json );
JSS ( txn_count );
JSS ( type );
JSS ( type_hex );
JSS ( validated );
JSS ( validated_ledger );
JSS ( validated_ledgers );
JSS ( validation_quorum );
JSS ( value );
JSS ( waiting );
JSS ( warning );

#undef JSS

} // jss
} // ripple

#endif
