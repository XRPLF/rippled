//! Shared scaffolding for the integration tests: a host whose every answer the
//! test sets, and the pieces of a wasm module to run against it.
//!
//! `abi.rs`'s guest-memory marshaling is reachable only from a live host call, so
//! each test assembles the smallest module that exercises one rule and reads the
//! verdict out of `finish`'s return value.

#![allow(dead_code)] // Each test binary uses a different part of this module.

use std::cell::RefCell;
use std::collections::HashMap;

use xrpl_host_functions::{HostError, HostFunctions, HostResult};
use xrpl_wasm_vm::RunOutcome;

/// The entry point every test module exports.
pub const ENTRY: &str = "finish";

/// Gas for a test that is not about gas: enough that nothing runs out.
pub const PLENTY_OF_GAS: u64 = 100_000_000;

// ---------------------------------------------------------------------------
// The fake host
// ---------------------------------------------------------------------------

/// What the host does when asked for a value.
#[derive(Clone, Debug)]
pub enum Answer {
    /// Writes `bytes` into the output region if they fit, and reports `len` as
    /// the true length either way. `len` is separate from `bytes.len()` so a
    /// test can reach the over-cap and buffer-fit rules without a value that
    /// large.
    Value { bytes: Vec<u8>, len: usize },
    /// Fails without touching the output region.
    Fail(HostError),
}

impl Answer {
    /// Writes `bytes` and reports their true length.
    pub fn bytes(bytes: impl Into<Vec<u8>>) -> Answer {
        let bytes = bytes.into();
        Answer::Value {
            len: bytes.len(),
            bytes,
        }
    }

    /// Writes nothing and claims a value of `len` bytes. It under-writes relative
    /// to a real host, which writes whenever the value fits `out`, so a test about
    /// what lands in guest memory wants [`Answer::bytes`] instead.
    pub fn claiming(len: usize) -> Answer {
        Answer::Value {
            bytes: Vec::new(),
            len,
        }
    }

    /// `len` bytes counting up from 0, written and reported.
    pub fn filler(len: usize) -> Answer {
        Answer::bytes((0..len).map(|i| i as u8).collect::<Vec<u8>>())
    }

    fn fill(&self, out: &mut [u8]) -> HostResult<usize> {
        match self {
            Answer::Value { bytes, len } => {
                if bytes.len() <= out.len() {
                    out[..bytes.len()].copy_from_slice(bytes);
                }
                Ok(*len)
            }
            Answer::Fail(error) => Err(*error),
        }
    }
}

/// One `trace` or `trace_num` call, as the host received it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Trace {
    Message {
        msg: String,
        data: Vec<u8>,
        as_hex: bool,
    },
    Number {
        msg: String,
        number: i64,
    },
}

/// A `HostFunctions` implementation that answers from what the test put in it and
/// records what it was asked. The ABI's receiver is `&self`, so the recording goes
/// behind `RefCell`, as a real mutating host's would.
pub struct FakeHost {
    /// What `get_ledger_sqn` answers.
    pub ledger_sqn: Answer,
    /// What `get_current_ledger_obj_field` answers, by field selector. An
    /// unlisted selector answers `FieldNotFound`.
    pub fields: HashMap<i32, Answer>,
    /// What `sha512_half` answers, whatever it is given.
    pub digest: Answer,
    /// Every field selector `get_current_ledger_obj_field` was asked for.
    pub fields_asked: RefCell<Vec<i32>>,
    /// Every input `sha512_half` was given.
    pub digested: RefCell<Vec<Vec<u8>>>,
    /// Every `trace`/`trace_num` call, in order.
    pub traces: RefCell<Vec<Trace>>,
}

impl Default for FakeHost {
    fn default() -> FakeHost {
        FakeHost {
            // 4 little-endian bytes, as the declaration's doc comment specifies.
            ledger_sqn: Answer::bytes(7u32.to_le_bytes()),
            fields: HashMap::new(),
            digest: Answer::filler(32),
            fields_asked: RefCell::new(Vec::new()),
            digested: RefCell::new(Vec::new()),
            traces: RefCell::new(Vec::new()),
        }
    }
}

impl FakeHost {
    pub fn new() -> FakeHost {
        FakeHost::default()
    }

    pub fn answering_sqn(mut self, answer: Answer) -> FakeHost {
        self.ledger_sqn = answer;
        self
    }

    pub fn answering_field(mut self, field: i32, answer: Answer) -> FakeHost {
        self.fields.insert(field, answer);
        self
    }

    pub fn answering_digest(mut self, answer: Answer) -> FakeHost {
        self.digest = answer;
        self
    }

    pub fn traces(&self) -> Vec<Trace> {
        self.traces.borrow().clone()
    }
}

impl HostFunctions for FakeHost {
    fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize> {
        self.ledger_sqn.fill(out)
    }

    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        self.fields_asked.borrow_mut().push(field);
        match self.fields.get(&field) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.digested.borrow_mut().push(data.to_vec());
        self.digest.fill(out)
    }

    fn trace(&self, msg: &str, data: &[u8], as_hex: bool) -> HostResult<()> {
        self.traces.borrow_mut().push(Trace::Message {
            msg: msg.to_owned(),
            data: data.to_vec(),
            as_hex,
        });
        Ok(())
    }

    fn trace_num(&self, msg: &str, number: i64) -> HostResult<()> {
        self.traces.borrow_mut().push(Trace::Number {
            msg: msg.to_owned(),
            number,
        });
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Module pieces
// ---------------------------------------------------------------------------

/// One `(import …)` declaration per host function, spelled with the signature it
/// is registered under and binding the `$name` call sites use. A wrong signature
/// fails instantiation.
pub mod import {
    pub const LDGR_INDEX: &str =
        r#"(import "host" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))"#;
    pub const HOME_LE_FIELD: &str =
        r#"(import "host" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))"#;
    pub const SHA512_HALF: &str =
        r#"(import "host" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))"#;
    pub const TRACE: &str =
        r#"(import "host" "trace" (func $trace (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const TRACE_NUM: &str =
        r#"(import "host" "trace_num" (func $trace_num (param i32 i32 i64) (result i32)))"#;
}

/// One page of linear memory, exported under the name the engine looks for.
pub const ONE_PAGE: &str = r#"(memory (export "memory") 1)"#;

/// A module of `parts`, wrapping `body` in an exported `finish` returning `i32`.
pub fn module(parts: &[&str], body: &str) -> String {
    format!(
        "(module {parts}\n  (func (export \"{ENTRY}\") (result i32)\n    {body}))",
        parts = parts.join("\n  ")
    )
}

// ---------------------------------------------------------------------------
// Running
//
// The tests write their modules as text and assemble them here: the VM takes
// binaries only, so the crate builds `wasmi` without its `wat` feature.
// ---------------------------------------------------------------------------

/// Assembles a text-format module into the binary the VM takes.
///
/// Panics rather than returning an error: text that will not assemble is a
/// mistake in the test, not a case under test.
pub fn assemble(wat: &str) -> Vec<u8> {
    wat::parse_str(wat)
        .unwrap_or_else(|e| panic!("this test's module does not assemble: {e}\n{wat}"))
}

/// Runs `wat`'s `finish` against `host` with gas to spare.
pub fn run(wat: &str, host: &FakeHost) -> Result<RunOutcome, String> {
    run_with_gas(wat, PLENTY_OF_GAS, host)
}

/// Runs `wat`'s `finish` against `host` with exactly `gas` to spend.
pub fn run_with_gas(wat: &str, gas: u64, host: &FakeHost) -> Result<RunOutcome, String> {
    xrpl_wasm_vm::run(&assemble(wat), gas, host, ENTRY)
}

/// Runs the export named `entry` rather than `finish`.
pub fn run_entry(wat: &str, host: &FakeHost, entry: &str) -> Result<RunOutcome, String> {
    xrpl_wasm_vm::run(&assemble(wat), PLENTY_OF_GAS, host, entry)
}

/// The value `finish` returned, for a run expected to complete: the host call's
/// status, so a byte count on success or a negative [`HostError`] code.
pub fn status(wat: &str, host: &FakeHost) -> i32 {
    run(wat, host)
        .unwrap_or_else(|e| panic!("expected the module to run, but: {e}\n{wat}"))
        .result
}

/// The wire code a `HostError` reaches the guest as, for readable assertions.
pub fn code(error: HostError) -> i32 {
    error.code()
}

/// The error message from a run that was expected to fail.
pub fn failure(wat: &str, host: &FakeHost) -> String {
    match run(wat, host) {
        Err(message) => message,
        Ok(outcome) => panic!(
            "expected a failure, but the module returned {}",
            outcome.result
        ),
    }
}
