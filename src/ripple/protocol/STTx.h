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

#ifndef RIPPLE_PROTOCOL_STTX_H_INCLUDED
#define RIPPLE_PROTOCOL_STTX_H_INCLUDED

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Expected.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/SeqProxy.h>
#include <ripple/protocol/TxFormats.h>
#include <boost/container/flat_set.hpp>
#include <functional>

namespace ripple {

enum TxnSql : char {
    txnSqlNew = 'N',
    txnSqlConflict = 'C',
    txnSqlHeld = 'H',
    txnSqlValidated = 'V',
    txnSqlIncluded = 'I',
    txnSqlUnknown = 'U'
};

class STTx final : public STObject, public CountedObject<STTx>
{
public:
    static std::size_t const minMultiSigners = 1;
    static std::size_t const maxMultiSigners = 8;

public:
    STTx() = delete;
    STTx&
    operator=(STTx const& other) = delete;

    STTx(STTx const& other) = default;

    explicit STTx(SerialIter& sit) noexcept(false);
    explicit STTx(SerialIter&& sit) noexcept(false) : STTx(sit)
    {
    }

    explicit STTx(STObject&& object) noexcept(false);

    /** Constructs a transaction.

        The returned transaction will have the specified type and
        any fields that the callback function adds to the object
        that's passed in.
    */
    STTx(TxType type, std::function<void(STObject&)> assembler);

    STBase*
    copy(std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move(std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    // STObject functions.
    SerializedTypeID
    getSType() const override
    {
        return STI_TRANSACTION;
    }
    std::string
    getFullText() const override;

    // Outer transaction functions / signature functions.
    Blob
    getSignature() const;

    Buffer
    getSignature(PublicKey const& publicKey, SecretKey const& secretKey) const;

    // Get one of the multi-signatures
    Buffer
    getMultiSignature(
        AccountID const& signingID,
        PublicKey const& publicKey,
        SecretKey const& secretKey) const;

    // unconditionally set signature. No error checking.
    void
    setSignature(Buffer const& sig);

    uint256
    getSigningHash() const;

    TxType
    getTxnType() const
    {
        return tx_type_;
    }

    Blob
    getSigningPubKey() const
    {
        return getFieldVL(sfSigningPubKey);
    }

    SeqProxy
    getSeqProxy() const;

    boost::container::flat_set<AccountID>
    getMentionedAccounts() const;

    uint256
    getTransactionID() const
    {
        return tid_;
    }

    Json::Value
    getJson(JsonOptions options) const override;
    Json::Value
    getJson(JsonOptions options, bool binary) const;

    void
    sign(PublicKey const& publicKey, SecretKey const& secretKey);

    /** Check the signature.
        @return `true` if valid signature. If invalid, the error message string.
    */
    enum class RequireFullyCanonicalSig : bool { no, yes };
    Expected<void, std::string>
    checkSign(RequireFullyCanonicalSig requireCanonicalSig) const;

    // SQL Functions with metadata.
    static std::string const&
    getMetaSQLInsertReplaceHeader();

    std::string
    getMetaSQL(std::uint32_t inLedger, std::string const& escapedMetaData)
        const;

    std::string
    getMetaSQL(
        Serializer rawTxn,
        std::uint32_t inLedger,
        char status,
        std::string const& escapedMetaData) const;

private:
    Expected<void, std::string>
    checkSingleSign(RequireFullyCanonicalSig requireCanonicalSig) const;

    Expected<void, std::string>
    checkMultiSign(RequireFullyCanonicalSig requireCanonicalSig) const;

    uint256 tid_;
    TxType tx_type_;
};

bool
passesLocalChecks(STObject const& st, std::string&);

/** Sterilize a transaction.

    The transaction is serialized and then deserialized,
    ensuring that all equivalent transactions are in canonical
    form. This also ensures that program metadata such as
    the transaction's digest, are all computed.
*/
std::shared_ptr<STTx const>
sterilize(STTx const& stx);

/** Check whether a transaction is a pseudo-transaction */
bool
isPseudoTx(STObject const& tx);

}  // namespace ripple

#endif
