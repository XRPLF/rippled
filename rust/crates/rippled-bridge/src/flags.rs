use cxx::{ExternType, type_id};
use cxx::kind::Trivial;

#[repr(i32)]
#[derive(Copy, Clone)]
pub enum ApplyFlags {
    tapNONE = 0x00,

    // This is a local transaction with the
    // fail_hard flag set.
    tapFAIL_HARD = 0x10,

    // This is not the transaction's last pass
    // Transaction can be retried, soft failures allowed
    tapRETRY = 0x20,

    // Transaction came from a privileged source
    tapUNLIMITED = 0x400,
}

unsafe impl cxx::ExternType for ApplyFlags {
    type Id = type_id!("ripple::ApplyFlags");
    type Kind = Trivial;
}


pub enum LedgerSpecificFlags {
    // ltACCOUNT_ROOT
    lsfPasswordSpent,  // True, if password set fee is spent.
    lsfRequireDestTag,  // True, to require a DestinationTag for payments.
    lsfRequireAuth,  // True, to require a authorization to hold IOUs.
    lsfDisallowXRP,    // True, to disallow sending XRP.
    lsfDisableMaster,  // True, force regular key
    lsfNoFreeze,       // True, cannot freeze ripple states
    lsfGlobalFreeze,   // True, all assets frozen
    lsfDefaultRipple,               // True, trust lines allow rippling by default
    lsfDepositAuth,  // True, all deposits require authorization
    /*  // reserved for Hooks amendment
        lsfTshCollect = 0x02000000,     // True, allow TSH collect-calls to acc hooks
    */
    lsfDisallowIncomingNFTokenOffer,// True, reject new incoming NFT offers
    lsfDisallowIncomingCheck,               // True, reject new checks
    lsfDisallowIncomingPayChan,               // True, reject new paychans
    lsfDisallowIncomingTrustline,               // True, reject new trustlines (only if no issued assets)

    // ltOFFER
    lsfPassive,
    lsfSell,  // True, offer was placed as a sell.

    // ltRIPPLE_STATE
    lsfLowReserve,  // True, if entry counts toward reserve.
    lsfHighReserve,
    lsfLowAuth,
    lsfHighAuth,
    lsfLowNoRipple,
    lsfHighNoRipple,
    lsfLowFreeze,   // True, low side has set freeze flag
    lsfHighFreeze,  // True, high side has set freeze flag

    // ltSIGNER_LIST
    lsfOneOwnerCount,  // True, uses only one OwnerCount

    // ltDIR_NODE
    lsfNFTokenBuyOffers,
    lsfNFTokenSellOffers,

    // ltNFTOKEN_OFFER
    lsfSellNFToken,
}

impl From<LedgerSpecificFlags> for u32 {
    fn from(value: LedgerSpecificFlags) -> Self {
        match value {
            LedgerSpecificFlags::lsfPasswordSpent => 0x00010000,
            LedgerSpecificFlags::lsfRequireDestTag => 0x00020000,
            LedgerSpecificFlags::lsfRequireAuth => 0x00040000,
            LedgerSpecificFlags::lsfDisallowXRP => 0x00080000,
            LedgerSpecificFlags::lsfDisableMaster => 0x00100000,
            LedgerSpecificFlags::lsfNoFreeze => 0x00200000,
            LedgerSpecificFlags::lsfGlobalFreeze => 0x00400000,
            LedgerSpecificFlags::lsfDefaultRipple => 0x00800000,
            LedgerSpecificFlags::lsfDepositAuth => 0x01000000,
            LedgerSpecificFlags::lsfDisallowIncomingNFTokenOffer => 0x04000000,
            LedgerSpecificFlags::lsfDisallowIncomingCheck => 0x08000000,
            LedgerSpecificFlags::lsfDisallowIncomingPayChan => 0x10000000,
            LedgerSpecificFlags::lsfDisallowIncomingTrustline => 0x20000000,
            LedgerSpecificFlags::lsfPassive => 0x00010000,
            LedgerSpecificFlags::lsfSell => 0x00020000,
            LedgerSpecificFlags::lsfLowReserve => 0x00010000,
            LedgerSpecificFlags::lsfHighReserve => 0x00020000,
            LedgerSpecificFlags::lsfLowAuth => 0x00040000,
            LedgerSpecificFlags::lsfHighAuth => 0x00080000,
            LedgerSpecificFlags::lsfLowNoRipple => 0x00100000,
            LedgerSpecificFlags::lsfHighNoRipple => 0x00200000,
            LedgerSpecificFlags::lsfLowFreeze => 0x00400000,
            LedgerSpecificFlags::lsfHighFreeze => 0x00800000,
            LedgerSpecificFlags::lsfOneOwnerCount => 0x00010000,
            LedgerSpecificFlags::lsfNFTokenBuyOffers => 0x00000001,
            LedgerSpecificFlags::lsfNFTokenSellOffers => 0x00000002,
            LedgerSpecificFlags::lsfSellNFToken => 0x00000001,
        }
    }
}