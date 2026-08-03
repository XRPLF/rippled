//! The escrow wasm VM: compile a contract, meter it, and serve its host calls.
//!
//! Every guest access goes through `abi.rs` and reaches linear memory only by
//! wasmi's bounds-checked slice operations; `forbid(unsafe_code)` makes that a
//! property rather than a claim. The cast lints are on for the same reason — on a
//! consensus path a truncating or sign-losing cast changes what a contract is
//! charged or told, so each one is argued for at its site.
#![forbid(unsafe_code)]
#![deny(rustdoc::broken_intra_doc_links)]
#![deny(unreachable_pub)]
#![deny(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss,
    clippy::cast_lossless
)]

mod abi;
mod register;
mod vm;

pub use vm::{
    MAX_FIELD_BYTES, MAX_MEMORY_BYTES, MAX_MEMORY_PAGES, RunError, RunFailure, RunOutcome,
    TRANSFER_LIMIT_BYTES, run,
};
