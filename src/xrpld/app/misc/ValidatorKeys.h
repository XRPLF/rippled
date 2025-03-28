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

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/UintTypes.h>

#include <string>

namespace ripple {

class Config;

/** Validator keys and manifest as set in configuration file.  Values will be
    empty if not configured as a validator or not configured with a manifest.
*/
class ValidatorKeys
{
public:
    // Group all keys in a struct. Either all keys are valid or none are.
    struct Keys
    {
        PublicKey masterPublicKey;
        PublicKey publicKey;
        SecretKey secretKey;

        Keys() = delete;
        Keys(
            PublicKey const& masterPublic_,
            PublicKey const& public_,
            SecretKey const& secret_)
            : masterPublicKey(masterPublic_)
            , publicKey(public_)
            , secretKey(secret_)
        {
        }
    };

    // Note: The existence of keys cannot be used as a proxy for checking the
    // validity of a configuration. It is possible to have a valid
    // configuration while not setting the keys, as per the constructor of
    // the ValidatorKeys class.
    std::optional<Keys> keys;
    NodeID nodeID;
    std::string manifest;
    std::uint32_t sequence = 0;

    ValidatorKeys() = delete;
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
