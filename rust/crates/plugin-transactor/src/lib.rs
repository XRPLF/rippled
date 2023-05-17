extern crate core;

pub mod transactor;

use core::slice;
pub use transactor::Transactor;

use xrpl_rust_sdk_core::core::types::AccountId;

pub struct PreflightContext<'a> {
    instance: &'a rippled_bridge::rippled::PreflightContext,
}

impl PreflightContext<'_> {
    pub fn new<'a>(instance: &'a rippled_bridge::rippled::PreflightContext) -> PreflightContext<'a> {
        PreflightContext { instance }
    }

    pub fn rules(&self) -> Rules {
        Rules::new(self.instance.getRules())
    }

    pub fn tx(&self) -> STTx {
        STTx::new(self.instance.getTx())
    }
}

pub struct PreclaimContext<'a> {
    instance: &'a rippled_bridge::rippled::PreclaimContext,
}

impl PreclaimContext<'_> {
    pub fn new<'a>(instance: &'a rippled_bridge::rippled::PreclaimContext) -> PreclaimContext<'a> {
        PreclaimContext { instance }
    }
}

pub struct STTx<'a> {
    instance: &'a rippled_bridge::rippled::STTx,
}

impl STTx<'_> {
    pub fn new<'a>(instance: &'a rippled_bridge::rippled::STTx) -> STTx<'a> {
        STTx { instance }
    }

    pub fn flags(&self) -> u32 {
        rippled_bridge::rippled::upcast(self.instance).getFlags()
    }

    pub fn get_account_id(&self, field: &SField) -> AccountId {
        rippled_bridge::rippled::upcast(self.instance).getAccountID(field.instance).into()
    }

    pub fn get_plugin_type(&self, field: &SField) -> STPluginType {
        STPluginType::new(self.as_st_object().getPluginType(field.instance))
    }

    pub fn is_field_present(&self, field: &SField) -> bool {
        self.as_st_object().isFieldPresent(field.instance)
    }

    fn as_st_object(&self) -> &rippled_bridge::rippled::STObject {
        rippled_bridge::rippled::upcast(self.instance)
    }
}

pub struct STPluginType<'a> {
    instance: &'a rippled_bridge::rippled::STPluginType
}

impl AsRef<[u8]> for STPluginType<'_> {
    fn as_ref(&self) -> &[u8] {
        unsafe {
            let data: *const u8 = self.instance.data();
            let size: usize = self.instance.size();
            slice::from_raw_parts(data, size)
        }
    }
}

impl <T> PartialEq<T> for STPluginType<'_> where T: AsRef<[u8]> {
    fn eq(&self, other: &T) -> bool {
        self.as_ref() == other.as_ref()
    }
}

impl STPluginType<'_> {
    pub(crate) fn new<'a>(instance: &'a rippled_bridge::rippled::STPluginType) -> STPluginType<'a> {
        STPluginType { instance }
    }
}

pub struct SField<'a> {
    instance: &'a rippled_bridge::rippled::SField
}

impl SField<'_> {
    pub fn sf_regular_key() -> Self {
        SField {
            instance: rippled_bridge::rippled::sfRegularKey()
        }
    }

    pub fn sf_account() -> Self {
        SField {
            instance: rippled_bridge::rippled::sfAccount()
        }
    }

    pub fn get_plugin_field(type_id: i32, field_id: i32) -> Self {
        SField {
            instance: rippled_bridge::rippled::getSField(type_id, field_id)
        }
    }
}

pub struct Rules<'a> {
    instance: &'a rippled_bridge::rippled::Rules,
}

impl Rules<'_> {
    pub(crate) fn new<'a>(instance: &'a rippled_bridge::rippled::Rules) -> Rules<'a> {
        Rules { instance }
    }

    pub fn enabled(&self, feature: &Feature) -> bool {
        self.instance.enabled(feature.value)
    }
}

pub struct Feature<'a> {
    value: &'a rippled_bridge::rippled::uint256,
}

impl Feature<'_> {
    pub fn fix_master_key_as_regular_key() -> Self {
        Feature {
            value: rippled_bridge::rippled::fixMasterKeyAsRegularKey()
        }
    }
}

pub fn preflight1(ctx: &PreflightContext) -> rippled_bridge::NotTEC {
    rippled_bridge::rippled::preflight1(ctx.instance)
}

pub fn preflight2(ctx: &PreflightContext) -> rippled_bridge::NotTEC {
    rippled_bridge::rippled::preflight2(ctx.instance)
}

pub const TF_FULLY_CANONICAL_SIG: u32 = 0x80000000;
pub const TF_UNIVERSAL: u32 = TF_FULLY_CANONICAL_SIG;
pub const TF_UNIVERSAL_MASK: u32 = !TF_UNIVERSAL;
