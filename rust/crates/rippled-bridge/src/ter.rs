use cxx::{ExternType, type_id};
use cxx::kind::Trivial;

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum TELcodes {
    telLOCAL_ERROR = -399,
    telBAD_DOMAIN,
    telBAD_PATH_COUNT,
    telBAD_PUBLIC_KEY,
    telFAILED_PROCESSING,
    telINSUF_FEE_P,
    telNO_DST_PARTIAL,
    telCAN_NOT_QUEUE,
    telCAN_NOT_QUEUE_BALANCE,
    telCAN_NOT_QUEUE_BLOCKS,
    telCAN_NOT_QUEUE_BLOCKED,
    telCAN_NOT_QUEUE_FEE,
    telCAN_NOT_QUEUE_FULL,
}

unsafe impl cxx::ExternType for TELcodes {
    type Id = type_id!("ripple::TELcodes");
    type Kind = Trivial;
}

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum TEMcodes {
    temMALFORMED = -299,

    temBAD_AMOUNT,
    temBAD_CURRENCY,
    temBAD_EXPIRATION,
    temBAD_FEE,
    temBAD_ISSUER,
    temBAD_LIMIT,
    temBAD_OFFER,
    temBAD_PATH,
    temBAD_PATH_LOOP,
    temBAD_REGKEY,
    temBAD_SEND_XRP_LIMIT,
    temBAD_SEND_XRP_MAX,
    temBAD_SEND_XRP_NO_DIRECT,
    temBAD_SEND_XRP_PARTIAL,
    temBAD_SEND_XRP_PATHS,
    temBAD_SEQUENCE,
    temBAD_SIGNATURE,
    temBAD_SRC_ACCOUNT,
    temBAD_TRANSFER_RATE,
    temDST_IS_SRC,
    temDST_NEEDED,
    temINVALID,
    temINVALID_FLAG,
    temREDUNDANT,
    temRIPPLE_EMPTY,
    temDISABLED,
    temBAD_SIGNER,
    temBAD_QUORUM,
    temBAD_WEIGHT,
    temBAD_TICK_SIZE,
    temINVALID_ACCOUNT_ID,
    temCANNOT_PREAUTH_SELF,
    temINVALID_COUNT,

    temUNCERTAIN,  // An internal intermediate result; should never be returned.
    temUNKNOWN,    // An internal intermediate result; should never be returned.

    temSEQ_AND_TICKET,
    temBAD_NFTOKEN_TRANSFER_FEE,
}

unsafe impl cxx::ExternType for TEMcodes {
    type Id = type_id!("ripple::TEMcodes");
    type Kind = Trivial;
}

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum TEFcodes {
    tefFAILURE = -199,
    tefALREADY,
    tefBAD_ADD_AUTH,
    tefBAD_AUTH,
    tefBAD_LEDGER,
    tefCREATED,
    tefEXCEPTION,
    tefINTERNAL,
    tefNO_AUTH_REQUIRED,
    // Can't set auth if auth is not required.
    tefPAST_SEQ,
    tefWRONG_PRIOR,
    tefMASTER_DISABLED,
    tefMAX_LEDGER,
    tefBAD_SIGNATURE,
    tefBAD_QUORUM,
    tefNOT_MULTI_SIGNING,
    tefBAD_AUTH_MASTER,
    tefINVARIANT_FAILED,
    tefTOO_BIG,
    tefNO_TICKET,
    tefNFTOKEN_IS_NOT_TRANSFERABLE,
}

unsafe impl cxx::ExternType for TEFcodes {
    type Id = type_id!("ripple::TEFcodes");
    type Kind = Trivial;
}

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum TERcodes {
    terRETRY = -99,
    terFUNDS_SPENT,  // DEPRECATED.
    terINSUF_FEE_B,  // Can't pay fee, therefore don't burden network.
    terNO_ACCOUNT,   // Can't pay fee, therefore don't burden network.
    terNO_AUTH,      // Not authorized to hold IOUs.
    terNO_LINE,      // Internal flag.
    terOWNERS,       // Can't succeed with non-zero owner count.
    terPRE_SEQ,      // Can't pay fee, no point in forwarding, so don't
    // burden network.
    terLAST,         // DEPRECATED.
    terNO_RIPPLE,    // Rippling not allowed
    terQUEUED,       // Transaction is being held in TxQ until fee drops
    terPRE_TICKET,   // Ticket is not yet in ledger but might be on its way
}

unsafe impl cxx::ExternType for TERcodes {
    type Id = type_id!("ripple::TERcodes");
    type Kind = Trivial;
}

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum TEScodes {
    tesSUCCESS = 0
}

unsafe impl cxx::ExternType for TEScodes {
    type Id = type_id!("ripple::TEScodes");
    type Kind = Trivial;
}

#[repr(i32)]
#[derive(Clone, Copy)]
pub enum TECcodes {
    tecCLAIM = 100,
    tecPATH_PARTIAL = 101,
    tecUNFUNDED_ADD = 102,  // Unused legacy code
    tecUNFUNDED_OFFER = 103,
    tecUNFUNDED_PAYMENT = 104,
    tecFAILED_PROCESSING = 105,
    tecDIR_FULL = 121,
    tecINSUF_RESERVE_LINE = 122,
    tecINSUF_RESERVE_OFFER = 123,
    tecNO_DST = 124,
    tecNO_DST_INSUF_XRP = 125,
    tecNO_LINE_INSUF_RESERVE = 126,
    tecNO_LINE_REDUNDANT = 127,
    tecPATH_DRY = 128,
    tecUNFUNDED = 129,
    tecNO_ALTERNATIVE_KEY = 130,
    tecNO_REGULAR_KEY = 131,
    tecOWNERS = 132,
    tecNO_ISSUER = 133,
    tecNO_AUTH = 134,
    tecNO_LINE = 135,
    tecINSUFF_FEE = 136,
    tecFROZEN = 137,
    tecNO_TARGET = 138,
    tecNO_PERMISSION = 139,
    tecNO_ENTRY = 140,
    tecINSUFFICIENT_RESERVE = 141,
    tecNEED_MASTER_KEY = 142,
    tecDST_TAG_NEEDED = 143,
    tecINTERNAL = 144,
    tecOVERSIZE = 145,
    tecCRYPTOCONDITION_ERROR = 146,
    tecINVARIANT_FAILED = 147,
    tecEXPIRED = 148,
    tecDUPLICATE = 149,
    tecKILLED = 150,
    tecHAS_OBLIGATIONS = 151,
    tecTOO_SOON = 152,
    tecHOOK_ERROR = 153,
    tecMAX_SEQUENCE_REACHED = 154,
    tecNO_SUITABLE_NFTOKEN_PAGE = 155,
    tecNFTOKEN_BUY_SELL_MISMATCH = 156,
    tecNFTOKEN_OFFER_TYPE_MISMATCH = 157,
    tecCANT_ACCEPT_OWN_NFTOKEN_OFFER = 158,
    tecINSUFFICIENT_FUNDS = 159,
    tecOBJECT_NOT_FOUND = 160,
    tecINSUFFICIENT_PAYMENT = 161,
}

unsafe impl cxx::ExternType for TECcodes {
    type Id = type_id!("ripple::TECcodes");
    type Kind = Trivial;
}


/////////////////////////////
// NotTEC
/////////////////////////////

#[repr(C)]
#[derive(PartialEq)]
pub struct NotTEC {
    code: i32
}

unsafe impl cxx::ExternType for NotTEC {
    type Id = type_id!("ripple::NotTEC");
    type Kind = Trivial;
}

impl NotTEC {
    pub fn new(code: i32) -> Self {
        NotTEC { code }
    }
}

impl From<TEScodes> for NotTEC {
    fn from(value: TEScodes) -> Self {
        NotTEC::new(value as i32)
    }
}

impl From<TELcodes> for NotTEC {
    fn from(value: TELcodes) -> Self {
        NotTEC::new(value as i32)
    }
}


impl From<TEMcodes> for NotTEC {
    fn from(value: TEMcodes) -> Self {
        NotTEC::new(value as i32)
    }
}

impl From<TEFcodes> for NotTEC {
    fn from(value: TEFcodes) -> Self {
        NotTEC::new(value as i32)
    }
}

impl From<TERcodes> for NotTEC {
    fn from(value: TERcodes) -> Self {
        NotTEC::new(value as i32)
    }
}

impl PartialEq<TEScodes> for NotTEC {
    fn eq(&self, other: &TEScodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TELcodes> for NotTEC {
    fn eq(&self, other: &TELcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TEMcodes> for NotTEC {
    fn eq(&self, other: &TEMcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TEFcodes> for NotTEC {
    fn eq(&self, other: &TEFcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TERcodes> for NotTEC {
    fn eq(&self, other: &TERcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TECcodes> for NotTEC {
    fn eq(&self, other: &TECcodes) -> bool {
        self.code == (*other as i32)
    }
}


/////////////////////////////
// NotTEC
/////////////////////////////

#[repr(C)]
#[derive(PartialEq)]
pub struct TER {
    code: i32
}

unsafe impl cxx::ExternType for TER {
    type Id = type_id!("ripple::TER");
    type Kind = Trivial;
}

impl TER {
    pub fn new(code: i32) -> Self {
        TER { code }
    }
}

impl From<TEScodes> for TER {
    fn from(value: TEScodes) -> Self {
        TER::new(value as i32)
    }
}

impl From<TELcodes> for TER {
    fn from(value: TELcodes) -> Self {
        TER::new(value as i32)
    }
}


impl From<TEMcodes> for TER {
    fn from(value: TEMcodes) -> Self {
        TER::new(value as i32)
    }
}

impl From<TEFcodes> for TER {
    fn from(value: TEFcodes) -> Self {
        TER::new(value as i32)
    }
}

impl From<TERcodes> for TER {
    fn from(value: TERcodes) -> Self {
        TER::new(value as i32)
    }
}

impl From<TECcodes> for TER {
    fn from(value: TECcodes) -> Self {
        TER::new(value as i32)
    }
}

///////////////////
impl PartialEq<TEScodes> for TER {
    fn eq(&self, other: &TEScodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TELcodes> for TER {
    fn eq(&self, other: &TELcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TEMcodes> for TER {
    fn eq(&self, other: &TEMcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TEFcodes> for TER {
    fn eq(&self, other: &TEFcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TERcodes> for TER {
    fn eq(&self, other: &TERcodes) -> bool {
        self.code == (*other as i32)
    }
}

impl PartialEq<TECcodes> for TER {
    fn eq(&self, other: &TECcodes) -> bool {
        self.code == (*other as i32)
    }
}