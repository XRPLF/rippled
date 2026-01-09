#ifndef XRPL_PROTOCOL_STTAKESASSET_H_INCLUDED
#define XRPL_PROTOCOL_STTAKESASSET_H_INCLUDED

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {

/** Intermediate class for any STBase-derived class to store an Asset.
 *
 * In the class definition, this class should be specified as a base class
 * _instead_ of STBase.
 *
 * Specifically, the Asset is only stored and used at runtime. It should not be
 * serialized to the ledger.
 *
 * The derived class decides what to do with the Asset, and when. It will not
 * necessarily be set at any given time. As of this writing, only STNumber uses
 * it to round the stored Number to the Asset's precision both when associated,
 * and when serializing the Number.
 */
class STTakesAsset : public STBase
{
protected:
    std::optional<Asset> asset_;

public:
    using STBase::STBase;
    using STBase::operator=;

    virtual void
    associateAsset(Asset const& a);
};

inline void
STTakesAsset::associateAsset(Asset const& a)
{
    asset_.emplace(a);
}

class STLedgerEntry;

/** Associate an Asset with all sMD_NeedsAsset fields in a ledger entry.
 *
 * This function iterates over all fields in the given ledger entry. For each
 * field that is set and has the SField::sMD_NeedsAsset metadata flag, it calls
 * `associateAsset` on that field with the given Asset. Such field must be
 * derived from STTakesAsset - if it is not, the conversion will throw.
 *
 * Typically, associateAsset should be called near the end of doApply() of any
 * Transactor classes on the SLEs of any new or modified ledger entries
 * containing STNumber fields, after doing all of the modifications t the SLEs.
 *
 * @param sle The ledger entry whose fields will be updated.
 * @param asset The Asset to associate with the relevant fields.
 *
 */
void
associateAsset(STLedgerEntry& sle, Asset const& asset);

}  // namespace ripple

#endif
