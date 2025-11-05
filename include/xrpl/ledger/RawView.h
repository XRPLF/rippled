#ifndef XRPL_LEDGER_RAWVIEW_H_INCLUDED
#define XRPL_LEDGER_RAWVIEW_H_INCLUDED

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

/** Interface for ledger entry changes.

    Subclasses allow raw modification of ledger entries.
*/
class RawView
{
public:
    virtual ~RawView() = default;
    RawView() = default;
    RawView(RawView const&) = default;
    RawView&
    operator=(RawView const&) = delete;

    /** Delete an existing state item.

        The SLE is provided so the implementation
        can calculate metadata.
    */
    virtual void
    rawErase(std::shared_ptr<SLE> const& sle) = 0;

    /** Unconditionally insert a state item.

        Requirements:
            The key must not already exist.

        Effects:

            The key is associated with the SLE.

        @note The key is taken from the SLE
    */
    virtual void
    rawInsert(std::shared_ptr<SLE> const& sle) = 0;

    /** Unconditionally replace a state item.

        Requirements:

            The key must exist.

        Effects:

            The key is associated with the SLE.

        @note The key is taken from the SLE
    */
    virtual void
    rawReplace(std::shared_ptr<SLE> const& sle) = 0;

    /** Destroy XRP.

        This is used to pay for transaction fees.
    */
    virtual void
    rawDestroyXRP(XRPAmount const& fee) = 0;
};

//------------------------------------------------------------------------------

/** Interface for changing ledger entries with transactions.

    Allows raw modification of ledger entries and insertion
    of transactions into the transaction map.
*/
class TxsRawView : public RawView
{
public:
    /** Add a transaction to the tx map.

        Closed ledgers must have metadata,
        while open ledgers omit metadata.
    */
    virtual void
    rawTxInsert(
        ReadView::key_type const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) = 0;
};

}  // namespace ripple

#endif
