//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STTX_WRAPPER_H_INCLUDED
#define RIPPLE_PROTOCOL_STTX_WRAPPER_H_INCLUDED

#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STTx.h>
#include <functional>

namespace ripple {

// This class is a wrapper to deal with delegating in AccountPermission
// amendment. It wraps STTx, and delegates to STTx methods. The key change is
// getAccountID and operator[]. We need to first check if the transaction is
// delegated by another account by checking if the sfOnBehalfOf field is
// present. If it is present, we need to return the sfOnBehalfOf field as the
// account when calling getAccountID(sfAccount) and tx[sfAccount].
class STTxDelegated
{
private:
    const STTx& tx_;    // Wrap an instance of STTx
    bool isDelegated_;  // if the transaction is delegated by another account

public:
    explicit STTxDelegated(STTx const& tx, bool isDelegated)
        : tx_(tx), isDelegated_(isDelegated)
    {
    }

    const STTx&
    getSTTx() const
    {
        return tx_;
    }

    bool
    isDelegated() const
    {
        return isDelegated_;
    }

    AccountID
    getSenderAccount() const
    {
        return tx_.getAccountID(sfAccount);
    }

    std::uint32_t
    getEffectiveSeq() const
    {
        return isDelegated_ ? tx_.getDelegateSeqProxy().value()
                            : tx_.getSeqProxy().value();
    }

    AccountID
    getAccountID(SField const& field) const
    {
        if (field == sfAccount)
            return tx_.isFieldPresent(sfOnBehalfOf) ? *tx_[~sfOnBehalfOf]
                                                    : tx_[sfAccount];
        return tx_.getAccountID(field);
    }

    template <class T>
        requires(!std::is_same<TypedField<T>, SF_ACCOUNT>::value)
    typename T::value_type
    operator[](TypedField<T> const& f) const
    {
        return tx_[f];
    }

    // When Type is SF_ACCOUNT and also field name is sfAccount, we need to
    // check if the transaction is delegated by another account. If it is,
    // return sfOnBehalfOf field instead.
    template <class T>
        requires std::is_same<TypedField<T>, SF_ACCOUNT>::value
    AccountID
    operator[](TypedField<T> const& f) const
    {
        if (f == sfAccount)
            return tx_.isFieldPresent(sfOnBehalfOf) ? *tx_[~sfOnBehalfOf]
                                                    : tx_[sfAccount];
        return tx_[f];
    }

    template <class T>
    std::optional<std::decay_t<typename T::value_type>>
    operator[](OptionaledField<T> const& of) const
    {
        return tx_[of];
    }

    template <class T>
        requires(!std::is_same<TypedField<T>, SF_ACCOUNT>::value)
    typename T::value_type
    at(TypedField<T> const& f) const
    {
        return tx_.at(f);
    }

    // When Type is SF_ACCOUNT and also field name is sfAccount, we need to
    // check if the transaction is delegated by another account. If it is,
    // return sfOnBehalfOf field instead.
    template <class T>
        requires std::is_same<TypedField<T>, SF_ACCOUNT>::value
    AccountID
    at(TypedField<T> const& f) const
    {
        if (f == sfAccount)
            return tx_.isFieldPresent(sfOnBehalfOf) ? *tx_[~sfOnBehalfOf]
                                                    : tx_[sfAccount];
        return tx_.at(f);
    }

    template <class T>
    std::optional<std::decay_t<typename T::value_type>>
    at(OptionaledField<T> const& of) const
    {
        return tx_.at(of);
    }

    uint256
    getTransactionID() const
    {
        return tx_.getTransactionID();
    }

    TxType
    getTxnType() const
    {
        return tx_.getTxnType();
    }

    std::uint32_t
    getFlags() const
    {
        return tx_.getFlags();
    }

    bool
    isFieldPresent(SField const& field) const
    {
        return tx_.isFieldPresent(field);
    }

    Json::Value
    getJson(JsonOptions options) const
    {
        return tx_.getJson(options);
    }

    void
    add(Serializer& s) const
    {
        tx_.add(s);
    }

    unsigned char
    getFieldU8(SField const& field) const
    {
        return tx_.getFieldU8(field);
    }

    std::uint32_t
    getFieldU32(SField const& field) const
    {
        return tx_.getFieldU32(field);
    }

    uint256
    getFieldH256(SField const& field) const
    {
        return tx_.getFieldH256(field);
    }

    Blob
    getFieldVL(SField const& field) const
    {
        return tx_.getFieldVL(field);
    }

    STAmount const&
    getFieldAmount(SField const& field) const
    {
        return tx_.getFieldAmount(field);
    }

    STPathSet const&
    getFieldPathSet(SField const& field) const
    {
        return tx_.getFieldPathSet(field);
    }

    const STVector256&
    getFieldV256(SField const& field) const
    {
        return tx_.getFieldV256(field);
    }

    const STArray&
    getFieldArray(SField const& field) const
    {
        return tx_.getFieldArray(field);
    }

    Blob
    getSigningPubKey() const
    {
        return tx_.getSigningPubKey();
    }

    Blob
    getSignature() const
    {
        return tx_.getSignature();
    }

    bool
    isFlag(std::uint32_t f) const
    {
        return tx_.isFlag(f);
    }

    SeqProxy
    getSeqProxy() const
    {
        return tx_.getSeqProxy();
    }

    SeqProxy
    getDelegateSeqProxy() const
    {
        return tx_.getDelegateSeqProxy();
    }
};

}  // namespace ripple

#endif
