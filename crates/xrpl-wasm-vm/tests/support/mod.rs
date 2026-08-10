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
use xrpl_wasm_vm::{RunFailure, RunOutcome};

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

    /// Writes `bytes` and reports `len` regardless — a host whose value is longer
    /// than what it put in the buffer, which the engine has to refuse without
    /// letting those bytes reach the guest.
    pub fn writing_but_claiming(bytes: impl Into<Vec<u8>>, len: usize) -> Answer {
        Answer::Value {
            bytes: bytes.into(),
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
    /// What `get_parent_ledger_time` answers.
    pub parent_ledger_time: Answer,
    /// What `get_parent_ledger_hash` answers.
    pub parent_ledger_hash: Answer,
    /// What `get_base_fee` answers.
    pub base_fee: Answer,
    /// What `is_amendment_enabled` answers, whatever amendment it is given.
    pub amendment_enabled: HostResult<i32>,
    /// Every amendment `is_amendment_enabled` was asked about.
    pub amendments_asked: RefCell<Vec<Vec<u8>>>,
    /// What `cache_ledger_obj` answers: the slot it "used".
    pub cache_slot: HostResult<i32>,
    /// Every (object id, requested slot) `cache_ledger_obj` was asked to cache.
    pub cached: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `get_tx_field` answers, by field selector. An unlisted selector answers
    /// `FieldNotFound`.
    pub tx_fields: HashMap<i32, Answer>,
    /// Every field selector `get_tx_field` was asked for.
    pub tx_fields_asked: RefCell<Vec<i32>>,
    /// What `get_current_ledger_obj_field` answers, by field selector. An
    /// unlisted selector answers `FieldNotFound`.
    pub fields: HashMap<i32, Answer>,
    /// What `get_ledger_obj_field` answers, by (cache slot, field selector). An
    /// unlisted key answers `FieldNotFound`.
    pub le_fields: HashMap<(i32, i32), Answer>,
    /// Every (cache slot, field selector) `get_ledger_obj_field` was asked for.
    pub le_fields_asked: RefCell<Vec<(i32, i32)>>,
    /// What `get_tx_nested_field` answers, by locator bytes. An unlisted locator
    /// answers `FieldNotFound`.
    pub tx_nested: HashMap<Vec<u8>, Answer>,
    /// Every locator `get_tx_nested_field` was asked for.
    pub tx_nested_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_current_ledger_obj_nested_field` answers, by locator bytes. An
    /// unlisted locator answers `FieldNotFound`.
    pub home_le_nested: HashMap<Vec<u8>, Answer>,
    /// Every locator `get_current_ledger_obj_nested_field` was asked for.
    pub home_le_nested_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_ledger_obj_nested_field` answers, by (cache slot, locator bytes). An
    /// unlisted key answers `FieldNotFound`.
    pub le_nested: HashMap<(i32, Vec<u8>), Answer>,
    /// Every (cache slot, locator) `get_ledger_obj_nested_field` was asked for.
    pub le_nested_asked: RefCell<Vec<(i32, Vec<u8>)>>,
    /// What `get_tx_array_len` answers, by field selector. An unlisted selector
    /// answers `NoArray`.
    pub tx_arr_lens: HashMap<i32, i32>,
    /// Every field selector `get_tx_array_len` was asked for.
    pub tx_arr_lens_asked: RefCell<Vec<i32>>,
    /// What `get_current_ledger_obj_array_len` answers, by field selector. An
    /// unlisted selector answers `NoArray`.
    pub home_le_arr_lens: HashMap<i32, i32>,
    /// Every field selector `get_current_ledger_obj_array_len` was asked for.
    pub home_le_arr_lens_asked: RefCell<Vec<i32>>,
    /// What `get_ledger_obj_array_len` answers, by (cache slot, field selector). An
    /// unlisted key answers `NoArray`.
    pub le_arr_lens: HashMap<(i32, i32), i32>,
    /// Every (cache slot, field selector) `get_ledger_obj_array_len` was asked for.
    pub le_arr_lens_asked: RefCell<Vec<(i32, i32)>>,
    /// What `get_tx_nested_array_len` answers, by locator bytes. An unlisted locator
    /// answers `NoArray`.
    pub tx_nested_arr_lens: HashMap<Vec<u8>, i32>,
    /// Every locator `get_tx_nested_array_len` was asked for.
    pub tx_nested_arr_lens_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_current_ledger_obj_nested_array_len` answers, by locator bytes. An
    /// unlisted locator answers `NoArray`.
    pub home_le_nested_arr_lens: HashMap<Vec<u8>, i32>,
    /// Every locator `get_current_ledger_obj_nested_array_len` was asked for.
    pub home_le_nested_arr_lens_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_ledger_obj_nested_array_len` answers, by (cache slot, locator bytes).
    /// An unlisted key answers `NoArray`.
    pub le_nested_arr_lens: HashMap<(i32, Vec<u8>), i32>,
    /// Every (cache slot, locator) `get_ledger_obj_nested_array_len` was asked for.
    pub le_nested_arr_lens_asked: RefCell<Vec<(i32, Vec<u8>)>>,
    /// What `check_signature` answers, whatever it is given.
    pub sig_valid: HostResult<i32>,
    /// Every (message, signature, pubkey) `check_signature` was asked to verify.
    pub sigs_checked: RefCell<Vec<(Vec<u8>, Vec<u8>, Vec<u8>)>>,
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
            // A distinct value from the sequence number, so a test cannot pass by
            // reading one where it meant the other.
            parent_ledger_time: Answer::bytes(9u32.to_le_bytes()),
            // 32 bytes counting up from 0, the length of a real ledger hash.
            parent_ledger_hash: Answer::filler(32),
            // A distinct value again, so no getter can pass by reading another's answer.
            base_fee: Answer::bytes(10u32.to_le_bytes()),
            // Enabled by default; the id-or-name dispatch is the host's job, not the ABI's.
            amendment_enabled: Ok(1),
            amendments_asked: RefCell::new(Vec::new()),
            // Slot 1 by default; slot assignment is the host's job, not the ABI's.
            cache_slot: Ok(1),
            cached: RefCell::new(Vec::new()),
            tx_fields: HashMap::new(),
            tx_fields_asked: RefCell::new(Vec::new()),
            fields: HashMap::new(),
            le_fields: HashMap::new(),
            le_fields_asked: RefCell::new(Vec::new()),
            tx_nested: HashMap::new(),
            tx_nested_asked: RefCell::new(Vec::new()),
            home_le_nested: HashMap::new(),
            home_le_nested_asked: RefCell::new(Vec::new()),
            le_nested: HashMap::new(),
            le_nested_asked: RefCell::new(Vec::new()),
            tx_arr_lens: HashMap::new(),
            tx_arr_lens_asked: RefCell::new(Vec::new()),
            home_le_arr_lens: HashMap::new(),
            home_le_arr_lens_asked: RefCell::new(Vec::new()),
            le_arr_lens: HashMap::new(),
            le_arr_lens_asked: RefCell::new(Vec::new()),
            tx_nested_arr_lens: HashMap::new(),
            tx_nested_arr_lens_asked: RefCell::new(Vec::new()),
            home_le_nested_arr_lens: HashMap::new(),
            home_le_nested_arr_lens_asked: RefCell::new(Vec::new()),
            le_nested_arr_lens: HashMap::new(),
            le_nested_arr_lens_asked: RefCell::new(Vec::new()),
            // Valid by default; the verification itself is the host's job, not the ABI's.
            sig_valid: Ok(1),
            sigs_checked: RefCell::new(Vec::new()),
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

    pub fn answering_parent_ledger_time(mut self, answer: Answer) -> FakeHost {
        self.parent_ledger_time = answer;
        self
    }

    pub fn answering_parent_ledger_hash(mut self, answer: Answer) -> FakeHost {
        self.parent_ledger_hash = answer;
        self
    }

    pub fn answering_base_fee(mut self, answer: Answer) -> FakeHost {
        self.base_fee = answer;
        self
    }

    pub fn answering_amendment_enabled(mut self, answer: HostResult<i32>) -> FakeHost {
        self.amendment_enabled = answer;
        self
    }

    pub fn answering_cache_slot(mut self, answer: HostResult<i32>) -> FakeHost {
        self.cache_slot = answer;
        self
    }

    pub fn answering_tx_field(mut self, field: i32, answer: Answer) -> FakeHost {
        self.tx_fields.insert(field, answer);
        self
    }

    pub fn answering_field(mut self, field: i32, answer: Answer) -> FakeHost {
        self.fields.insert(field, answer);
        self
    }

    pub fn answering_le_field(mut self, cache_idx: i32, field: i32, answer: Answer) -> FakeHost {
        self.le_fields.insert((cache_idx, field), answer);
        self
    }

    pub fn answering_tx_nested(mut self, locator: Vec<u8>, answer: Answer) -> FakeHost {
        self.tx_nested.insert(locator, answer);
        self
    }

    pub fn answering_home_le_nested(mut self, locator: Vec<u8>, answer: Answer) -> FakeHost {
        self.home_le_nested.insert(locator, answer);
        self
    }

    pub fn answering_le_nested(
        mut self,
        cache_idx: i32,
        locator: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.le_nested.insert((cache_idx, locator), answer);
        self
    }

    pub fn answering_tx_arr_len(mut self, field: i32, len: i32) -> FakeHost {
        self.tx_arr_lens.insert(field, len);
        self
    }

    pub fn answering_home_le_arr_len(mut self, field: i32, len: i32) -> FakeHost {
        self.home_le_arr_lens.insert(field, len);
        self
    }

    pub fn answering_le_arr_len(mut self, cache_idx: i32, field: i32, len: i32) -> FakeHost {
        self.le_arr_lens.insert((cache_idx, field), len);
        self
    }

    pub fn answering_tx_nested_arr_len(mut self, locator: Vec<u8>, len: i32) -> FakeHost {
        self.tx_nested_arr_lens.insert(locator, len);
        self
    }

    pub fn answering_home_le_nested_arr_len(mut self, locator: Vec<u8>, len: i32) -> FakeHost {
        self.home_le_nested_arr_lens.insert(locator, len);
        self
    }

    pub fn answering_le_nested_arr_len(
        mut self,
        cache_idx: i32,
        locator: Vec<u8>,
        len: i32,
    ) -> FakeHost {
        self.le_nested_arr_lens.insert((cache_idx, locator), len);
        self
    }

    pub fn answering_check_sig(mut self, answer: HostResult<i32>) -> FakeHost {
        self.sig_valid = answer;
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

    fn get_parent_ledger_time(&self, out: &mut [u8]) -> HostResult<usize> {
        self.parent_ledger_time.fill(out)
    }

    fn get_parent_ledger_hash(&self, out: &mut [u8]) -> HostResult<usize> {
        self.parent_ledger_hash.fill(out)
    }

    fn get_base_fee(&self, out: &mut [u8]) -> HostResult<usize> {
        self.base_fee.fill(out)
    }

    fn is_amendment_enabled(&self, amendment: &[u8]) -> HostResult<i32> {
        self.amendments_asked.borrow_mut().push(amendment.to_vec());
        self.amendment_enabled
    }

    fn cache_ledger_obj(&self, obj_id: &[u8], cache_idx: i32) -> HostResult<i32> {
        self.cached.borrow_mut().push((obj_id.to_vec(), cache_idx));
        self.cache_slot
    }

    fn get_tx_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        self.tx_fields_asked.borrow_mut().push(field);
        match self.tx_fields.get(&field) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        self.fields_asked.borrow_mut().push(field);
        match self.fields.get(&field) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn get_ledger_obj_field(
        &self,
        cache_idx: i32,
        field: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        self.le_fields_asked.borrow_mut().push((cache_idx, field));
        match self.le_fields.get(&(cache_idx, field)) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn get_tx_nested_field(&self, locator: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.tx_nested_asked.borrow_mut().push(locator.to_vec());
        match self.tx_nested.get(locator) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn get_current_ledger_obj_nested_field(
        &self,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        self.home_le_nested_asked
            .borrow_mut()
            .push(locator.to_vec());
        match self.home_le_nested.get(locator) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn get_ledger_obj_nested_field(
        &self,
        cache_idx: i32,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        self.le_nested_asked
            .borrow_mut()
            .push((cache_idx, locator.to_vec()));
        match self.le_nested.get(&(cache_idx, locator.to_vec())) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::FieldNotFound),
        }
    }

    fn get_tx_array_len(&self, field: i32) -> HostResult<i32> {
        self.tx_arr_lens_asked.borrow_mut().push(field);
        match self.tx_arr_lens.get(&field) {
            Some(&len) => Ok(len),
            None => Err(HostError::NoArray),
        }
    }

    fn get_current_ledger_obj_array_len(&self, field: i32) -> HostResult<i32> {
        self.home_le_arr_lens_asked.borrow_mut().push(field);
        match self.home_le_arr_lens.get(&field) {
            Some(&len) => Ok(len),
            None => Err(HostError::NoArray),
        }
    }

    fn get_ledger_obj_array_len(&self, cache_idx: i32, field: i32) -> HostResult<i32> {
        self.le_arr_lens_asked.borrow_mut().push((cache_idx, field));
        match self.le_arr_lens.get(&(cache_idx, field)) {
            Some(&len) => Ok(len),
            None => Err(HostError::NoArray),
        }
    }

    fn get_tx_nested_array_len(&self, locator: &[u8]) -> HostResult<i32> {
        self.tx_nested_arr_lens_asked
            .borrow_mut()
            .push(locator.to_vec());
        match self.tx_nested_arr_lens.get(locator) {
            Some(&len) => Ok(len),
            None => Err(HostError::NoArray),
        }
    }

    fn get_current_ledger_obj_nested_array_len(&self, locator: &[u8]) -> HostResult<i32> {
        self.home_le_nested_arr_lens_asked
            .borrow_mut()
            .push(locator.to_vec());
        match self.home_le_nested_arr_lens.get(locator) {
            Some(&len) => Ok(len),
            None => Err(HostError::NoArray),
        }
    }

    fn get_ledger_obj_nested_array_len(&self, cache_idx: i32, locator: &[u8]) -> HostResult<i32> {
        self.le_nested_arr_lens_asked
            .borrow_mut()
            .push((cache_idx, locator.to_vec()));
        match self.le_nested_arr_lens.get(&(cache_idx, locator.to_vec())) {
            Some(&len) => Ok(len),
            None => Err(HostError::NoArray),
        }
    }

    fn check_signature(&self, message: &[u8], signature: &[u8], pubkey: &[u8]) -> HostResult<i32> {
        self.sigs_checked.borrow_mut().push((
            message.to_vec(),
            signature.to_vec(),
            pubkey.to_vec(),
        ));
        self.sig_valid
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

/// One `(import …)` declaration per host function, spelled with the module name
/// and signature it is registered under and binding the `$name` call sites use. A
/// wrong module name or signature fails instantiation.
pub mod import {
    pub const LDGR_INDEX: &str =
        r#"(import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))"#;
    pub const PARENT_LDGR_TIME: &str = r#"(import "host_lib" "parent_ldgr_time" (func $parent_ldgr_time (param i32 i32) (result i32)))"#;
    pub const PARENT_LDGR_HASH: &str = r#"(import "host_lib" "parent_ldgr_hash" (func $parent_ldgr_hash (param i32 i32) (result i32)))"#;
    pub const BASE_FEE: &str =
        r#"(import "host_lib" "base_fee" (func $base_fee (param i32 i32) (result i32)))"#;
    pub const AMENDMENT_ENABLED: &str = r#"(import "host_lib" "amendment_enabled" (func $amendment_enabled (param i32 i32) (result i32)))"#;
    pub const CACHE_LE: &str =
        r#"(import "host_lib" "cache_le" (func $cache_le (param i32 i32 i32) (result i32)))"#;
    pub const TX_FIELD: &str =
        r#"(import "host_lib" "tx_field" (func $tx_field (param i32 i32 i32) (result i32)))"#;
    pub const HOME_LE_FIELD: &str = r#"(import "host_lib" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))"#;
    pub const LE_FIELD: &str =
        r#"(import "host_lib" "le_field" (func $le_field (param i32 i32 i32 i32) (result i32)))"#;
    pub const TX_INNER: &str =
        r#"(import "host_lib" "tx_inner" (func $tx_inner (param i32 i32 i32 i32) (result i32)))"#;
    pub const HOME_LE_INNER: &str = r#"(import "host_lib" "home_le_inner" (func $home_le_inner (param i32 i32 i32 i32) (result i32)))"#;
    pub const LE_INNER: &str = r#"(import "host_lib" "le_inner" (func $le_inner (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const TX_ARR_LEN: &str =
        r#"(import "host_lib" "tx_arr_len" (func $tx_arr_len (param i32) (result i32)))"#;
    pub const HOME_LE_ARR_LEN: &str =
        r#"(import "host_lib" "home_le_arr_len" (func $home_le_arr_len (param i32) (result i32)))"#;
    pub const LE_ARR_LEN: &str =
        r#"(import "host_lib" "le_arr_len" (func $le_arr_len (param i32 i32) (result i32)))"#;
    pub const TX_INNER_ARR_LEN: &str = r#"(import "host_lib" "tx_inner_arr_len" (func $tx_inner_arr_len (param i32 i32) (result i32)))"#;
    pub const HOME_LE_INNER_ARR_LEN: &str = r#"(import "host_lib" "home_le_inner_arr_len" (func $home_le_inner_arr_len (param i32 i32) (result i32)))"#;
    pub const LE_INNER_ARR_LEN: &str = r#"(import "host_lib" "le_inner_arr_len" (func $le_inner_arr_len (param i32 i32 i32) (result i32)))"#;
    pub const CHECK_SIG: &str = r#"(import "host_lib" "check_sig" (func $check_sig (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const SHA512_HALF: &str = r#"(import "host_lib" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))"#;
    pub const TRACE: &str =
        r#"(import "host_lib" "trace" (func $trace (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const TRACE_NUM: &str =
        r#"(import "host_lib" "trace_num" (func $trace_num (param i32 i32 i64) (result i32)))"#;
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
pub fn run(wat: &str, host: &FakeHost) -> Result<RunOutcome, RunFailure> {
    run_with_gas(wat, PLENTY_OF_GAS, host)
}

/// Runs `wat`'s `finish` against `host` with exactly `gas` to spend.
pub fn run_with_gas(wat: &str, gas: u64, host: &FakeHost) -> Result<RunOutcome, RunFailure> {
    xrpl_wasm_vm::run(&assemble(wat), gas, host, ENTRY)
}

/// Runs the export named `entry` rather than `finish`.
pub fn run_entry(wat: &str, host: &FakeHost, entry: &str) -> Result<RunOutcome, RunFailure> {
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

/// The failure from a run that was expected not to complete.
pub fn failure(wat: &str, host: &FakeHost) -> RunFailure {
    match run(wat, host) {
        Err(failure) => failure,
        Ok(outcome) => panic!(
            "expected a failure, but the module returned {}",
            outcome.result
        ),
    }
}
