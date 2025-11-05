#ifndef XRPL_PROTOCOL_STTX_H_INCLUDED
#define XRPL_PROTOCOL_STTX_H_INCLUDED

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TxFormats.h>

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
    uint256 tid_;
    TxType tx_type_;

public:
    static std::size_t const minMultiSigners = 1;

    // if rules are not supplied then the largest possible value is returned
    static std::size_t
    maxMultiSigners(Rules const* rules = 0)
    {
        if (rules && !rules->enabled(featureExpandedSignerList))
            return 8;

        return 32;
    }

    STTx() = delete;
    STTx(STTx const& other) = default;
    STTx&
    operator=(STTx const& other) = delete;

    explicit STTx(SerialIter& sit);
    explicit STTx(SerialIter&& sit);
    explicit STTx(STObject&& object);

    /** Constructs a transaction.

        The returned transaction will have the specified type and
        any fields that the callback function adds to the object
        that's passed in.
    */
    STTx(TxType type, std::function<void(STObject&)> assembler);

    // STObject functions.
    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    // Outer transaction functions / signature functions.
    static Blob
    getSignature(STObject const& sigObject);

    Blob
    getSignature() const
    {
        return getSignature(*this);
    }

    uint256
    getSigningHash() const;

    TxType
    getTxnType() const;

    Blob
    getSigningPubKey() const;

    SeqProxy
    getSeqProxy() const;

    /** Returns the first non-zero value of (Sequence, TicketSequence). */
    std::uint32_t
    getSeqValue() const;

    boost::container::flat_set<AccountID>
    getMentionedAccounts() const;

    uint256
    getTransactionID() const;

    Json::Value
    getJson(JsonOptions options) const override;

    Json::Value
    getJson(JsonOptions options, bool binary) const;

    void
    sign(
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        std::optional<std::reference_wrapper<SField const>> signatureTarget =
            {});

    enum class RequireFullyCanonicalSig : bool { no, yes };

    /** Check the signature.
        @param requireCanonicalSig If `true`, check that the signature is fully
            canonical. If `false`, only check that the signature is valid.
        @param rules The current ledger rules.
        @return `true` if valid signature. If invalid, the error message string.
    */
    Expected<void, std::string>
    checkSign(RequireFullyCanonicalSig requireCanonicalSig, Rules const& rules)
        const;

    Expected<void, std::string>
    checkBatchSign(
        RequireFullyCanonicalSig requireCanonicalSig,
        Rules const& rules) const;

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

    std::vector<uint256> const&
    getBatchTransactionIDs() const;

private:
    /** Check the signature.
        @param requireCanonicalSig If `true`, check that the signature is fully
            canonical. If `false`, only check that the signature is valid.
        @param rules The current ledger rules.
        @param sigObject Reference to object that contains the signature fields.
            Will be *this more often than not.
        @return `true` if valid signature. If invalid, the error message string.
    */
    Expected<void, std::string>
    checkSign(
        RequireFullyCanonicalSig requireCanonicalSig,
        Rules const& rules,
        STObject const& sigObject) const;

    Expected<void, std::string>
    checkSingleSign(
        RequireFullyCanonicalSig requireCanonicalSig,
        STObject const& sigObject) const;

    Expected<void, std::string>
    checkMultiSign(
        RequireFullyCanonicalSig requireCanonicalSig,
        Rules const& rules,
        STObject const& sigObject) const;

    Expected<void, std::string>
    checkBatchSingleSign(
        STObject const& batchSigner,
        RequireFullyCanonicalSig requireCanonicalSig) const;

    Expected<void, std::string>
    checkBatchMultiSign(
        STObject const& batchSigner,
        RequireFullyCanonicalSig requireCanonicalSig,
        Rules const& rules) const;

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
    mutable std::vector<uint256> batchTxnIds_;
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

inline STTx::STTx(SerialIter&& sit) : STTx(sit)
{
}

inline TxType
STTx::getTxnType() const
{
    return tx_type_;
}

inline Blob
STTx::getSigningPubKey() const
{
    return getFieldVL(sfSigningPubKey);
}

inline uint256
STTx::getTransactionID() const
{
    return tid_;
}

}  // namespace ripple

#endif
