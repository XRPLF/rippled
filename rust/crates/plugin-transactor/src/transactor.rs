use std::pin::Pin;
use xrpl_rust_sdk_core::core::types::XrpAmount;
use rippled_bridge::{NotTEC, SOEStyle, TER};
use crate::{PreclaimContext, PreflightContext, STTx};

pub trait Transactor {
    fn pre_flight(ctx: PreflightContext) -> NotTEC;
    fn pre_claim(ctx: PreclaimContext) -> TER;
    // TODO: Wrap ReadView
    unsafe fn calculateBaseFee(view: &rippled_bridge::rippled::ReadView, tx: STTx) -> XrpAmount {
        rippled_bridge::rippled::defaultCalculateBaseFee(view, tx.instance).into()
    }
    fn tx_format() -> Vec<SOElement>;
}

pub struct SOElement {
    pub field_code: i32,
    pub style: SOEStyle
}