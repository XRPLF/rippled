use std::ffi::{c_char, c_void};
use std::fmt::Formatter;
use std::mem::MaybeUninit;
use std::ops::Deref;
use std::pin::Pin;
use cxx::{CxxString, CxxVector, ExternType, SharedPtr, type_id, UniquePtr};
use cxx::kind::Trivial;
use cxx::vector::VectorElement;
use xrpl_rust_sdk_core::core::types::{AccountId, XrpAmount};

pub mod dummy_tx_rs;
pub mod ter;
pub mod flags;

pub use dummy_tx_rs::pre_flight;
pub use dummy_tx_rs::pre_claim;
pub use dummy_tx_rs::do_apply;
pub use ter::{TER, NotTEC, TEFcodes, TEMcodes, TELcodes, TECcodes, TEScodes, TERcodes};
pub use flags::{LedgerSpecificFlags, ApplyFlags};
use crate::rippled::{OptionalSTVar, SerialIter, SField, STBase, STPluginType, Value};

// Also try this
/*#[no_mangle]
pub fn preflight(ctx: &rippled::PreflightContext) -> NotTEC {
    pre_flight(ctx)
}

#[no_mangle]
pub fn preclaim(ctx: &rippled::PreclaimContext) -> TER {
    pre_claim(ctx)
}

#[no_mangle]
pub fn calculateBaseFee(view: &rippled::ReadView, tx: &rippled::STTx) -> XRPAmount {
    rippled::defaultCalculateBaseFee(view, tx)
}

#[no_mangle]
pub fn doApply(mut ctx: Pin<&mut rippled::ApplyContext>, mPriorBalance: rippled::XRPAmount, mSourceBalance: rippled::XRPAmount) -> TER {
    do_apply(ctx, mPriorBalance, mSourceBalance)
}*/


#[cxx::bridge]
pub mod rippled {

    // TODO: Make these match whats in SetTrust.cpp
    /*pub struct STypeExport {
        pub type_id: i32,
        // pub create_ptr: fn(tid: i32, fv: i32, f_name: *const c_char)
    }*/

    /*pub struct SFieldInfo {
        pub type_id: i32,
        pub field_value: i32,
        pub txt_name: *const c_char
    }*/

    extern "Rust" {
        // This function is unused, but exists only to ensure that line 11's interface is bridge
        // compatible.
        /*pub fn preflight(ctx: &PreflightContext) -> NotTEC;
        pub fn preclaim(ctx: &PreclaimContext) -> TER;
        pub fn calculateBaseFee(view: &ReadView, tx: &STTx) -> XRPAmount;
        pub fn doApply(mut ctx: Pin<&mut ApplyContext>, mPriorBalance: XRPAmount, mSourceBalance: XRPAmount) -> TER;*/
    }

    // Safety: the extern "C++" block is responsible for deciding whether to expose each signature
    // inside as safe-to-call or unsafe-to-call. If an extern block contains at least one
    // safe-to-call signature, it must be written as an unsafe extern block, which serves as
    // an item level unsafe block to indicate that an unchecked safety claim is being made about
    // the contents of the block.

    // These are C++ functions that can be called by Rust.
    // Within the extern "C++" part, we list types and functions for which C++ is the source of
    // truth, as well as the header(s) that declare those APIs.
    #[namespace = "ripple"]
    unsafe extern "C++" {
        ////////////////////////////////
        // One or more headers with the matching C++ declarations for the
        // enclosing extern "C++" block. Our code generators don't read it
        // but it gets #include'd and used in static assertions to ensure
        // our picture of the FFI boundary is accurate.
        ////////////////////////////////
        include!("rippled-bridge/include/rippled_api.h");

        ////////////////////////////////
        // Zero or more opaque types which both languages can pass around
        // but only C++ can see the fields.
        // type BlobstoreClient;
        ////////////////////////////////
        type TEFcodes = super::TEFcodes;
        type TEMcodes = super::TEMcodes;
        type TELcodes = super::TELcodes;
        type TERcodes = super::TERcodes;
        type TEScodes = super::TEScodes;
        type TECcodes = super::TECcodes;
        type AccountID = super::AccountID;
        type NotTEC = super::NotTEC;
        type TER = super::TER;
        pub type PreflightContext;
        pub type PreclaimContext;
        pub type ApplyContext;
        pub type XRPAmount = super::XRPAmount;
        pub type ReadView;
        pub type ApplyView;
        pub type STTx;
        pub type Rules;
        pub type uint256;
        type Transactor;
        pub type SField;
        pub type STObject;
        type Keylet = super::Keylet;
        type LedgerEntryType = super::LedgerEntryType;
        pub(crate) type SLE;
        type Application;
        type Fees;
        type ApplyFlags = super::ApplyFlags;
        pub type FakeSOElement /*= super::FakeSOElement*/;
        type SOEStyle = super::SOEStyle;
        pub type SFieldInfo;
        pub type SerialIter;
        pub type STBase;
        #[namespace = "Json"]
        pub type Value;
        pub type Buffer;
        pub type STPluginType;

        ////////////////////////////////
        // Functions implemented in C++.
        ////////////////////////////////

        pub fn preflight1(ctx: &PreflightContext) -> NotTEC;
        pub fn preflight2(ctx: &PreflightContext) -> NotTEC;

        // In AccountId.h --> AccountID const & xrpAccount();
        pub fn xrpAccount<'a>() -> &'a AccountID;

        pub fn data(self: &AccountID) -> *const u8;
        pub fn begin(self: &AccountID) -> *const u8;
        pub fn end(self: &AccountID) -> *const u8;

        #[namespace = "ripple::keylet"]
        pub fn account(id: &AccountID) -> Keylet;

        #[namespace = "ripple::keylet"]
        pub fn signers(id: &AccountID) -> Keylet;
    }

    unsafe extern "C++" {
        include!("rippled-bridge/include/rippled_api.h");

        pub type STypeExport;
        pub type CreateNewSFieldPtr = super::CreateNewSFieldPtr;
        pub type ParseLeafTypeFnPtr = super::ParseLeafTypeFnPtr;
        pub type STypeFromSITFnPtr = super::STypeFromSITFnPtr;
        pub type STypeFromSFieldFnPtr = super::STypeFromSFieldFnPtr;
        pub type OptionalSTVar;

        pub fn base64_decode_ptr(s: &CxxString) -> UniquePtr<CxxString>;

        pub fn fixMasterKeyAsRegularKey() -> &'static uint256;

        pub fn tfUniversalMask() -> u32;

        pub fn enabled(self: &Rules, s_field: &uint256) -> bool;
        pub fn getRules(self: &PreflightContext) -> &Rules;

        pub fn drops(self: &XRPAmount) -> i64;

        pub fn defaultCalculateBaseFee(view: &ReadView, tx: &STTx) -> XRPAmount;

        pub fn getTx(self: &PreflightContext) -> &STTx;

        // Note: getFlags is a method on STObject. In C++, we can call getFlags on anything
        // that extends STObject, but we can't call getFlags on an STTx in rust. We _could_
        // pass in self: &STTx here, but we'd have to duplicate this function for everything
        // we want to map that extends STObject. Instead, I defined an `upcast` function
        // in rippled_api.h that casts an STTx to an STObject, which I can call and then
        // use the returned STObject to call getFlags on.
        // See solution here: https://github.com/dtolnay/cxx/issues/797
        pub fn getFlags(self: &STObject) -> u32;

        pub fn isFieldPresent(self: &STObject, field: &SField) -> bool;
        pub fn isFlag(self: &SLE, f: u32) -> bool;

        pub fn getAccountID(self: &STObject, field: &SField) -> AccountID;
        pub fn getPluginType(self: &STObject, field: &SField) -> &'static STPluginType;

        pub fn sfRegularKey() -> &'static SField;
        pub fn sfAccount() -> &'static SField;
        // pub fn sfTicketSequence() -> &'static SField;
        pub fn getSField(type_id: i32, field_id: i32) -> &'static SField;

        pub fn getCode(self: &SField) -> i32;

        pub fn upcast(stTx: &STTx) -> &STObject;

        pub fn toBase58(v: &AccountID) -> UniquePtr<CxxString>;
        pub fn view(self: Pin<&mut ApplyContext>) -> Pin<&mut ApplyView>;
        pub fn getBaseFee<'a>(self: Pin<&'a mut ApplyContext>) -> XRPAmount;
        pub fn getTx(self: &ApplyContext) -> &STTx;
        pub fn getApp<'a, 'b>(self: Pin<&'a mut ApplyContext>) -> Pin<&'b mut Application>;

        pub fn peek(self: Pin<&mut ApplyView>, k: &Keylet) -> SharedPtr<SLE>;
        pub fn fees(self: &ApplyView) -> &Fees;
        pub fn flags(self: &ApplyView) -> ApplyFlags;

        // pub fn setFlag(self: Pin<&mut SLE>, f: u32) -> bool;

        pub fn setFlag(sle: &SharedPtr<SLE>, f: u32) -> bool;
        pub fn setAccountID(sle: &SharedPtr<SLE>, field: &SField, v: &AccountID);
        pub fn setPluginType(sle: &SharedPtr<SLE>, field: &SField, v: &STPluginType);

        pub fn makeFieldAbsent(sle: &SharedPtr<SLE>, field: &SField);
        pub fn minimumFee(app: Pin<&mut Application>, baseFee: XRPAmount, fees: &Fees, flags: ApplyFlags) -> XRPAmount;

        pub fn push_soelement(field_code: i32, style: SOEStyle, vec: Pin<&mut CxxVector<FakeSOElement>>);
        pub unsafe fn push_stype_export(
            tid: i32,
            create_new_sfield_fn: CreateNewSFieldPtr,
            parse_leaf_type_fn: ParseLeafTypeFnPtr,
            from_sit_constructor_ptr: STypeFromSITFnPtr,
            from_sfield_constructor_ptr: STypeFromSFieldFnPtr,
            vec: Pin<&mut CxxVector<STypeExport>>
        );
        pub unsafe fn push_sfield_info(tid: i32, fv: i32, txt_name: *const c_char, vec: Pin<&mut CxxVector<SFieldInfo>>);

        pub fn isString(self: &Value) -> bool;
        pub fn asString(json_value: &Value) -> UniquePtr<CxxString>;
        pub fn make_empty_stvar_opt() -> UniquePtr<OptionalSTVar>;
        pub fn make_stvar(field: &SField, data: &[u8]) -> UniquePtr<OptionalSTVar>;
        pub fn bad_type(error: Pin<&mut Value>, json_name: &CxxString, field_name: &CxxString);
        pub fn invalid_data(error: Pin<&mut Value>, json_name: &CxxString, field_name: &CxxString);
        pub fn getVLBuffer(sit: Pin<&mut SerialIter>) -> UniquePtr<Buffer>;
        pub fn make_stype(field: &SField, buffer: UniquePtr<Buffer>) -> UniquePtr<STPluginType>;
        pub fn make_empty_stype(field: &SField) -> UniquePtr<STBase>;
        pub unsafe fn constructSField(tid: i32, fv: i32, fname: *const c_char) -> &'static SField;

        pub unsafe fn data(self: &STPluginType) -> *const u8;
        pub fn size(self: &STPluginType) -> usize;
    }
}

#[repr(transparent)]
pub struct CreateNewSFieldPtr(
    pub extern "C" fn(
        tid: i32,
        fv: i32,
        field_name: *const c_char
    ) -> &'static SField
);

unsafe impl ExternType for CreateNewSFieldPtr {
    type Id = type_id!("CreateNewSFieldPtr");
    type Kind = Trivial;
}

// https://github.com/dtolnay/cxx/issues/895#issuecomment-913095541
#[repr(transparent)]
pub struct ParseLeafTypeFnPtr(
    pub extern "C" fn(
        field: &SField,
        json_name: &CxxString,
        field_name: &CxxString,
        name: &SField,
        value: &Value,
        error: Pin<&mut Value>
    ) -> *mut OptionalSTVar
);

unsafe impl ExternType for ParseLeafTypeFnPtr {
    type Id = type_id!("ParseLeafTypeFnPtr");
    type Kind = Trivial;
}

#[repr(transparent)]
pub struct STypeFromSITFnPtr(
    pub extern "C" fn(
        sit: Pin<&mut SerialIter>,
        name: &SField
    ) -> *mut STPluginType
);

unsafe impl ExternType for STypeFromSITFnPtr {
    type Id = type_id!("STypeFromSITFnPtr");
    type Kind = Trivial;
}

#[repr(transparent)]
pub struct STypeFromSFieldFnPtr(
    pub extern "C" fn(
        name: &SField
    ) -> *mut STBase
);

unsafe impl ExternType for STypeFromSFieldFnPtr {
    type Id = type_id!("STypeFromSFieldFnPtr");
    type Kind = Trivial;
}

/*pub struct SLE {
    sle: SharedPtr<rippled::SLE>
}

impl SLE {
    pub fn setFlag(&self, f: u32) -> bool {
        rippled::setFlag(&self.sle, f)
    }
}*/

/*#[repr(C)]
pub struct STypeExport {
    pub type_id: i32,
    pub create_ptr: fn(tid: i32, fv: i32, f_name: *const c_char) -> &'static TypedSTPluginType,
}*/

#[repr(C)]
#[derive(PartialEq)]
pub struct AccountID {
    data_: [u8; 20]
}

impl From<AccountID> for AccountId {
    fn from(value: AccountID) -> Self {
        AccountId::from(value.data_)
    }
}

unsafe impl cxx::ExternType for AccountID {
    type Id = type_id!("ripple::AccountID");
    type Kind = Trivial;
}

#[repr(C)]
#[derive(PartialEq)]
pub struct XRPAmount {
    drops_: i64
}

impl XRPAmount {
    pub fn zero() -> Self {
        XRPAmount { drops_: 0 }
    }
}

impl From<XrpAmount> for XRPAmount {
    fn from(value: XrpAmount) -> Self {
        XRPAmount {
            drops_: value.get_drops() as i64
        }
    }
}

impl From<XRPAmount> for XrpAmount {
    fn from(value: XRPAmount) -> Self {
        XrpAmount::of_drops(value.drops_ as u64).unwrap()
    }
}

unsafe impl cxx::ExternType for XRPAmount {
    type Id = type_id!("ripple::XRPAmount");
    type Kind = Trivial;
}

#[repr(i16)]
#[derive(Clone, Copy)]
pub enum LedgerEntryType {
    /// A ledger object which describes an account.
    /// \sa keylet::account

    ltACCOUNT_ROOT = 0x0061,

    /// A ledger object which contains a list of object identifiers.
    ///       \sa keylet::page, keylet::quality, keylet::book, keylet::next and
    ///           keylet::ownerDir

    ltDIR_NODE = 0x0064,

    /// A ledger object which describes a bidirectional trust line.
    ///       @note Per Vinnie Falco this should be renamed to ltTRUST_LINE
    ///       \sa keylet::line

    ltRIPPLE_STATE = 0x0072,

    /// A ledger object which describes a ticket.

    ///    \sa keylet::ticket
    ltTICKET = 0x0054,

    /// A ledger object which contains a signer list for an account.
    /// \sa keylet::signers
    ltSIGNER_LIST = 0x0053,

    /// A ledger object which describes an offer on the DEX.
    /// \sa keylet::offer
    ltOFFER = 0x006f,

    /// A ledger object that contains a list of ledger hashes.
    ///
    ///       This type is used to store the ledger hashes which the protocol uses
    ///       to implement skip lists that allow for efficient backwards (and, in
    ///       theory, forward) forward iteration across large ledger ranges.
    ///       \sa keylet::skip
    ltLEDGER_HASHES = 0x0068,

    /// The ledger object which lists details about amendments on the network.
    ///       \note This is a singleton: only one such object exists in the ledger.
    ///
    ///       \sa keylet::amendments
    ltAMENDMENTS = 0x0066,

    /// The ledger object which lists the network's fee settings.
    ///
    ///       \note This is a singleton: only one such object exists in the ledger.
    ///
    ///       \sa keylet::fees
    ltFEE_SETTINGS = 0x0073,

    /// A ledger object describing a single escrow.
    ///
    ///       \sa keylet::escrow
    ltESCROW = 0x0075,

    /// A ledger object describing a single unidirectional XRP payment channel.
    ///
    ///       \sa keylet::payChan
    ltPAYCHAN = 0x0078,

    /// A ledger object which describes a check.
    ///       \sa keylet::check
    ltCHECK = 0x0043,

    /// A ledger object which describes a deposit preauthorization.
    ///
    ///       \sa keylet::depositPreauth
    ltDEPOSIT_PREAUTH = 0x0070,

    /// The ledger object which tracks the current negative UNL state.
    ///
    ///       \note This is a singleton: only one such object exists in the ledger.
    ///
    ///       \sa keylet::negativeUNL

    ltNEGATIVE_UNL = 0x004e,

    /// A ledger object which contains a list of NFTs
    ///       \sa keylet::nftpage_min, keylet::nftpage_max, keylet::nftpage
    ltNFTOKEN_PAGE = 0x0050,

    /// A ledger object which identifies an offer to buy or sell an NFT.
    ///       \sa keylet::nftoffer
    ltNFTOKEN_OFFER = 0x0037,

    //---------------------------------------------------------------------------
    /// A special type, matching any ledger entry type.
    ///
    ///       The value does not represent a concrete type, but rather is used in
    ///       contexts where the specific type of a ledger object is unimportant,
    ///       unknown or unavailable.
    ///
    ///      Objects with this special type cannot be created or stored on the
    ///      ledger.
    ///
    ///      \sa keylet::unchecked
    ltANY = 0,

    /// A special type, matching any ledger type except directory nodes.
    ///
    ///      The value does not represent a concrete type, but rather is used in
    ///       contexts where the ledger object must not be a directory node but
    ///       its specific type is otherwise unimportant, unknown or unavailable.
    ///
    ///      Objects with this special type cannot be created or stored on the
    ///     ledger.
    ///
    ///      \sa keylet::child
    ltCHILD = 0x1CD2,

    //---------------------------------------------------------------------------
    /// A legacy, deprecated type.
    ///
    ///      \deprecated **This object type is not supported and should not be used.**
    ///                   Support for this type of object was never implemented.
    ///                  No objects of this type were ever created.
    ltNICKNAME = 0x006e,

    /// A legacy, deprecated type.
    ///
    ///   \deprecated **This object type is not supported and should not be used.**
    ///               Support for this type of object was never implemented.
    ///               No objects of this type were ever created.
    ltCONTRACT = 0x0063,

    /// A legacy, deprecated type.
    ///
    ///   \deprecated **This object type is not supported and should not be used.**
    ///             Support for this type of object was never implemented.
    ///            No objects of this type were ever created.
    ltGENERATOR_MAP = 0x0067,
}

unsafe impl cxx::ExternType for LedgerEntryType {
    type Id = type_id!("ripple::LedgerEntryType");
    type Kind = Trivial;
}

#[repr(C)]
pub struct Keylet {
    // TODO: Consider making uint256 a shared struct
    // Also test this, since key is a uint256 which has a data_ field
    key: [u8; 32],
    r#type: LedgerEntryType
}

unsafe impl cxx::ExternType for Keylet {
    type Id = type_id!("ripple::Keylet");
    type Kind = Trivial;
}

#[repr(C)]
pub struct FakeSOElement2 {
    pub field_code: i32,
    pub style: SOEStyle
}

unsafe impl cxx::ExternType for FakeSOElement2 {
    type Id = type_id!("ripple::FakeSOElement2");
    type Kind = Trivial;
}

#[repr(C)]
#[derive(PartialEq, Clone, Copy)]
pub enum SOEStyle {
    soeINVALID = -1,
    soeREQUIRED = 0,  // required
    soeOPTIONAL = 1,  // optional, may be present with default value
    soeDEFAULT = 2,   // optional, if present, must not have default value
}

unsafe impl cxx::ExternType for SOEStyle {
    type Id = type_id!("ripple::SOEStyle");
    type Kind = Trivial;
}