//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_VALIDATOR_KEYS_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATOR_KEYS_H_INCLUDED

#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/UintTypes.h>
#include <string>

namespace ripple {

class Config;

/** Validator keys and manifest as set in configuration file.  Values will be
    empty if not configured as a validator or not configured with a manifest.
*/
class ValidatorKeys
{
public:
    PublicKey publicKey;
    SecretKey secretKey;
    NodeID nodeID;
    std::string manifest;

    ValidatorKeys(Config const& config, beast::Journal j);

    bool
    configInvalid() const
    {
        return configInvalid_;
    }

private:
    bool configInvalid_ = false;  //< Set to true if config was invalid
};

}  // namespace ripple

#endif
