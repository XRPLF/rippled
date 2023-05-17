use std::ffi::{c_char, CString};
use std::pin::Pin;
use std::str::Utf8Error;
use std::vec;
use cxx::{CxxString, CxxVector, let_cxx_string, UniquePtr};
use once_cell::sync::OnceCell;
use xrpl_rust_sdk_core::core::crypto::ToFromBase58;
use xrpl_rust_sdk_core::core::types::AccountId;
use plugin_transactor::{Feature, PreclaimContext, preflight1, preflight2, PreflightContext, SField, STTx, TF_UNIVERSAL_MASK, Transactor};
use plugin_transactor::transactor::SOElement;
use rippled_bridge::{CreateNewSFieldPtr, NotTEC, ParseLeafTypeFnPtr, rippled, SOEStyle, STypeFromSFieldFnPtr, STypeFromSITFnPtr, TEMcodes, TER, TEScodes, XRPAmount};
use rippled_bridge::rippled::{account, asString, FakeSOElement, getVLBuffer, make_empty_stype, make_stvar, make_stype, OptionalSTVar, push_soelement, SerialIter, SFieldInfo, sfRegularKey, STBase, STPluginType, STypeExport, Value};


struct CFTokenIssuanceCreate;

impl Transactor for CFTokenIssuanceCreate {
    fn pre_flight(ctx: PreflightContext) -> NotTEC {

        let preflight1 = preflight1(&ctx);
        if preflight1 != TEScodes::tesSUCCESS {
            return preflight1;
        }

        if ctx.tx().flags() & TF_UNIVERSAL_MASK != 0 {
            return TEMcodes::temINVALID_FLAG.into();
        }

        // let sf_regular_key = SField::get_plugin_field(STI_ACCOUNT2, REGULAR_KEY2);
        // println!("RegularKey: {:?}", ctx.tx().get_plugin_type(&sf_regular_key).encode_base58());
        // println!("Account: {:?}", ctx.tx().get_account_id(&SField::sf_account()).encode_base58());

        // if ctx.rules().enabled(&Feature::fix_master_key_as_regular_key()) &&
        //     ctx.tx().is_field_present(&sf_regular_key) &&
        //     ctx.tx().get_plugin_type(&sf_regular_key) == ctx.tx().get_account_id(&SField::sf_account()) {
        //     return TEMcodes::temBAD_REGKEY.into();
        // }

        preflight2(&ctx)
    }

    fn pre_claim(ctx: PreclaimContext) -> TER {
        TEScodes::tesSUCCESS.into()
    }

    fn tx_format() -> Vec<SOElement> {
        vec![
            SOElement {
                field_code: field_code(24, 1),
                style: SOEStyle::soeOPTIONAL,
            },
            /*SOElement {
                field_code: sfTicketSequence().getCode(),
                style: SOEStyle::soeOPTIONAL,
            },*/
        ]
    }
}

pub fn field_code(type_id: i32, field_id: i32) -> i32 {
    (type_id << 16) | field_id
}

static FIELD_NAMES_ONCE: OnceCell<Vec<CString>> = OnceCell::new();

/// This method is called by rippled to get the SField information from this Plugin Transactor.
#[no_mangle]
pub fn getSFields(mut s_fields: Pin<&mut CxxVector<SFieldInfo>>) {
    let field_names = FIELD_NAMES_ONCE.get_or_init(|| {
        vec![CString::new("CFTokenIssuanceCreate").unwrap()]
    });
    // TODO: Might need to create a new sfield like sfQualityIn2() in SetTrust
    unsafe {
        rippled::push_sfield_info(24, 1, field_names.get(0).unwrap().as_ptr(), s_fields)
    }
    /*s_fields.as_mut().push(SFieldInfo {
        type_id: 24,
        field_value: 1,
        txt_name: CString::new("RegularKey2").unwrap().as_ptr()
    });*/

    /*s_fields.as_mut().push(SFieldInfo {
        foo: 4
    });*/
}

// To Register SField, need to: (All doable easily)
//   Define a type_id, ie STI_UINT32_2
//   Define field_id, ie 47
//   Define field name, ie sfQualityIn2

// To Register SType, need to:
//  Define a type_id, ie STI_UINT32_2 (already done)
//  Define a function that constructs a new SField of a given TypedField<T>
//  Define a function that parses the SF from JSON (ie parseLeafTypeNew)
//      In order to do this, need to be able to call detail::make_stvar (which is templated C++ code)
//      with an STBase somehow
//  Define a function that constructs a T: STBase from a SerialIter and SField
//

static NAME_ONCE: OnceCell<CString> = OnceCell::new();
static TT_ONCE: OnceCell<CString> = OnceCell::new();

#[no_mangle]
pub unsafe fn getTxName() -> *const i8 {
    let c_string = NAME_ONCE.get_or_init(|| {
        CString::new("CFTokenIssuanceCreate").unwrap()
    });
    let ptr = c_string.as_ptr();
    ptr
}

#[no_mangle]
pub unsafe fn getTTName() -> *const i8 {
    let c_string = TT_ONCE.get_or_init(|| {
        CString::new("ttCF_TOKEN_ISSUANCE_CREATE").unwrap()
    });
    let ptr = c_string.as_ptr();
    ptr
}

#[no_mangle]
pub unsafe fn getTxFormat(mut elements: Pin<&mut CxxVector<FakeSOElement>>) {
    let tx_format = CFTokenIssuanceCreate::tx_format();
    for element in tx_format {
        push_soelement(element.field_code, element.style, elements.as_mut());
    }
    // getTxFormat must take a Pin<&mut CxxVector<FakeSOElement>> that we can push FakeSOElements on to.
    // FakeSOElement must be an opaque type because CxxVectors cannot consist of shared types (CXX won't compile).
    // Therefore, we cannot construct an instance of a shared FakeSOElement and push it onto `elements`,
    // so we must call `push_soelement`, which will call a C++ function that constructs a C++ FakeSOElement
    // and pushes it onto the `elements` `std::vector`.
    // push_soelement(8, SOEStyle::soeOPTIONAL, elements.as_mut());
    // push_soelement(41, SOEStyle::soeOPTIONAL, elements.as_mut());
}



//////////////
// TODO: Figure Out where these go.
//////////////

// LedgerFormats.h
// /** A ledger object which identifies an the issuance details of a CFT.
//
//        \sa keylet::cftissuance
//  */
// ltCFTOKEN_ISSUANCE = 0x0033,

// /** A ledger object which contains a list of CFTs

       // \sa keylet::cftpage_min, keylet::cftpage_max, keylet::cftpage
 // */
// ltCFTOKEN_PAGE = 0x0034,


////////////
// For LAter
////////////

// /**
// @ingroup protocol
//  */
// enum LedgerSpecificFlags {

// }

// TODO: Define SField in xrpl-rs?
// TODO: Where to define the TxType? TxFormats.h?
    // ttCFTOKEN_ISSUANCE_CREATE = 30


// SFields for CFTokenIssuance
// sfFlags --> already exists in SField.cpp
// sfIssuer ==> "Issuer" | ACCOUNT, 4 --> already exists in SField.cpp?
// sfCurrencyCode ==> "CFTCurrencyCode" | UINT160 | 5
// sfAssetScale ==> "AssetScale" | UINT8 | 4
// sfTransferFee ==> "TransferFee" | UINT16 |
// sfMaximumAmount ==> "MaximumAmount" | UINT64 | 14
// sfOutstandingAmount ==> "OutstandingAmount" | UINT64 | 15
// sfLockedAmount ==> "LockedAmount" | UINT64 | 16

// sfMetadata => "Metadata" | BLOB | --> CONSTRUCT_UNTYPED_SFIELD(sfMetadata,            "Metadata",             METADATA,    257);
// CONSTRUCT_TYPED_SFIELD(sfOwnerNode,             "OwnerNode",            UINT64,     4);