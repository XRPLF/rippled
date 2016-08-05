//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <BeastConfig.h>

#include <ripple/protocol/impl/AccountID.cpp>
#include <ripple/protocol/impl/Book.cpp>
#include <ripple/protocol/impl/BuildInfo.cpp>
#include <ripple/protocol/impl/ByteOrder.cpp>
#include <ripple/protocol/impl/digest.cpp>
#include <ripple/protocol/impl/ErrorCodes.cpp>
#include <ripple/protocol/impl/Feature.cpp>
#include <ripple/protocol/impl/HashPrefix.cpp>
#include <ripple/protocol/impl/Indexes.cpp>
#include <ripple/protocol/impl/Issue.cpp>
#include <ripple/protocol/impl/Keylet.cpp>
#include <ripple/protocol/impl/LedgerFormats.cpp>
#include <ripple/protocol/impl/PublicKey.cpp>
#include <ripple/protocol/impl/Quality.cpp>
#include <ripple/protocol/impl/Rate2.cpp>
#include <ripple/protocol/impl/SecretKey.cpp>
#include <ripple/protocol/impl/Seed.cpp>
#include <ripple/protocol/impl/Serializer.cpp>
#include <ripple/protocol/impl/SField.cpp>
#include <ripple/protocol/impl/Sign.cpp>
#include <ripple/protocol/impl/SOTemplate.cpp>
#include <ripple/protocol/impl/TER.cpp>
#include <ripple/protocol/impl/tokens.cpp>
#include <ripple/protocol/impl/TxFormats.cpp>
#include <ripple/protocol/impl/UintTypes.cpp>

#include <ripple/protocol/impl/STAccount.cpp>
#include <ripple/protocol/impl/STArray.cpp>
#include <ripple/protocol/impl/STAmount.cpp>
#include <ripple/protocol/impl/STBase.cpp>
#include <ripple/protocol/impl/STBlob.cpp>
#include <ripple/protocol/impl/STInteger.cpp>
#include <ripple/protocol/impl/STLedgerEntry.cpp>
#include <ripple/protocol/impl/STObject.cpp>
#include <ripple/protocol/impl/STParsedJSON.cpp>
#include <ripple/protocol/impl/InnerObjectFormats.cpp>
#include <ripple/protocol/impl/STPathSet.cpp>
#include <ripple/protocol/impl/STTx.cpp>
#include <ripple/protocol/impl/STValidation.cpp>
#include <ripple/protocol/impl/STVar.cpp>
#include <ripple/protocol/impl/STVector256.cpp>
#include <ripple/protocol/impl/IOUAmount.cpp>

#include <ripple/basics/impl/contract.cpp>
#include <ripple/basics/impl/CountedObject.cpp>
#include <ripple/basics/impl/Log.cpp>
#include <ripple/basics/impl/strHex.cpp>
#include <ripple/basics/impl/StringUtilities.cpp>
#include <ripple/basics/impl/Time.cpp>
#include <ripple/beast/core/SemanticVersion.cpp>
#include <ripple/beast/hash/impl/spookyv2.cpp>
#include <ripple/beast/utility/src/beast_Journal.cpp>
#include <ripple/crypto/impl/csprng.cpp>
#include <ripple/crypto/impl/ec_key.cpp>
#include <ripple/crypto/impl/GenerateDeterministicKey.cpp>
#include <ripple/crypto/impl/openssl.cpp>
#include <ripple/crypto/impl/RFC1751.cpp>
#include <ripple/json/impl/json_value.cpp>
#include <ripple/json/impl/json_valueiterator.cpp>
#include <ripple/json/impl/json_writer.cpp>

#include <ripple/unity/secp256k1.cpp>
/* ed25519.c needs to be built separately because it's C. */
//#include <ripple/unity/ed25519.c>

#if DOXYGEN
#include <ripple/protocol/README.md>
#endif
