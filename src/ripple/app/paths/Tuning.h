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

#ifndef RIPPLE_APP_PATHS_TUNING_H_INCLUDED
#define RIPPLE_APP_PATHS_TUNING_H_INCLUDED

namespace ripple {

int const CALC_NODE_DELIVER_MAX_LOOPS = 100;
int const CALC_NODE_DELIVER_MAX_LOOPS_MQ = 2000;
int const NODE_ADVANCE_MAX_LOOPS = 100;
int const PAYMENT_MAX_LOOPS = 1000;
int const PATHFINDER_HIGH_PRIORITY = 100000;
int const PATHFINDER_MAX_PATHS = 50;
int const PATHFINDER_MAX_COMPLETE_PATHS = 1000;
int const PATHFINDER_MAX_PATHS_FROM_SOURCE = 10;

}  // namespace ripple

#endif
