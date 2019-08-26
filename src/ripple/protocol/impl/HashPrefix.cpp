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

#include <ripple/protocol/HashPrefix.h>

namespace ripple {

// The prefix codes are part of the Ripple protocol
// and existing codes cannot be arbitrarily changed.

HashPrefix const HashPrefix::transactionID               ('T', 'X', 'N');
HashPrefix const HashPrefix::txNode                      ('S', 'N', 'D');
HashPrefix const HashPrefix::leafNode                    ('M', 'L', 'N');
HashPrefix const HashPrefix::innerNode                   ('M', 'I', 'N');
HashPrefix const HashPrefix::ledgerMaster                ('L', 'W', 'R');
HashPrefix const HashPrefix::txSign                      ('S', 'T', 'X');
HashPrefix const HashPrefix::txMultiSign                 ('S', 'M', 'T');
HashPrefix const HashPrefix::validation                  ('V', 'A', 'L');
HashPrefix const HashPrefix::proposal                    ('P', 'R', 'P');
HashPrefix const HashPrefix::manifest                    ('M', 'A', 'N');
HashPrefix const HashPrefix::paymentChannelClaim         ('C', 'L', 'M');

} // ripple
