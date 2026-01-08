#ifndef XRPL_PROTOCOL_STTAKESASSET_H_INCLUDED
#define XRPL_PROTOCOL_STTAKESASSET_H_INCLUDED

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {

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

class STLedgerEntry;

void
associateAsset(STLedgerEntry& sle, Asset const& asset);

}  // namespace ripple

#endif
