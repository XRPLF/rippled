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

#ifndef RIPPLE_OVERLAY_ABSTRACT_PROTOCOL_HANDLER_H_INCLUDED
#define RIPPLE_OVERLAY_ABSTRACT_PROTOCOL_HANDLER_H_INCLUDED

#include "ripple.pb.h"
#include <boost/system/error_code.hpp>
#include <cstdint>

namespace ripple {

/** Handles protocol messages. */
class abstract_protocol_handler
{
protected:
    typedef boost::system::error_code error_code;

public:
    // Called for messages of unknown type
    virtual error_code on_message_unknown (std::uint16_t type) = 0;

    // Called before a specific message handler is invoked
    virtual error_code on_message_begin (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m) = 0;

    // Called after a specific message handler is invoked,
    // if on_message_begin did not return an error.
    virtual void on_message_end (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m) = 0;

    virtual error_code on_message (std::shared_ptr <protocol::TMHello> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMPing> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMProofWork> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMCluster> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMGetPeers> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMPeers> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMEndpoints> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMTransaction> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMGetLedger> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMLedgerData> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMProposeSet> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMStatusChange> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMHaveTransactionSet> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMValidation> const& m) { return error_code(); }
    virtual error_code on_message (std::shared_ptr <protocol::TMGetObjectByHash> const& m) { return error_code(); }
};

} // ripple

#endif
