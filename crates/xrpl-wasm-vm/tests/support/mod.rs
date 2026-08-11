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
    /// What `account_keylet` answers, by account bytes. An unlisted account answers
    /// `InvalidAccount`.
    pub account_keylets: HashMap<Vec<u8>, Answer>,
    /// Every account `account_keylet` was asked for.
    pub account_keylets_asked: RefCell<Vec<Vec<u8>>>,
    /// What `amm_keylet` answers, by (asset1, asset2) bytes. An unlisted pair answers
    /// `InvalidParams`.
    pub amm_keylets: HashMap<(Vec<u8>, Vec<u8>), Answer>,
    /// Every (asset1, asset2) pair `amm_keylet` was asked for.
    pub amm_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>)>>,
    /// What `check_keylet` answers, by (account bytes, seq). An unlisted key answers
    /// `InvalidAccount`.
    pub check_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `check_keylet` was asked for.
    pub check_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `credential_keylet` answers, by (subject, issuer, type) bytes. An unlisted
    /// key answers `InvalidAccount`.
    pub credential_keylets: HashMap<(Vec<u8>, Vec<u8>, Vec<u8>), Answer>,
    /// Every (subject, issuer, type) `credential_keylet` was asked for.
    pub credential_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>, Vec<u8>)>>,
    /// What `delegate_keylet` answers, by (account, authorize) bytes. An unlisted key
    /// answers `InvalidAccount`.
    pub delegate_keylets: HashMap<(Vec<u8>, Vec<u8>), Answer>,
    /// Every (account, authorize) `delegate_keylet` was asked for.
    pub delegate_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>)>>,
    /// What `deposit_preauth_keylet` answers, by (account, authorize) bytes. An
    /// unlisted key answers `InvalidAccount`.
    pub deposit_preauth_keylets: HashMap<(Vec<u8>, Vec<u8>), Answer>,
    /// Every (account, authorize) `deposit_preauth_keylet` was asked for.
    pub deposit_preauth_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>)>>,
    /// What `did_keylet` answers, by account bytes. An unlisted account answers
    /// `InvalidAccount`.
    pub did_keylets: HashMap<Vec<u8>, Answer>,
    /// Every account `did_keylet` was asked for.
    pub did_keylets_asked: RefCell<Vec<Vec<u8>>>,
    /// What `escrow_keylet` answers, by (account bytes, seq). An unlisted key answers
    /// `InvalidAccount`.
    pub escrow_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `escrow_keylet` was asked for.
    pub escrow_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `trust_line_keylet` answers, by (account1, account2, currency) bytes. An
    /// unlisted key answers `InvalidAccount`.
    pub trust_line_keylets: HashMap<(Vec<u8>, Vec<u8>, Vec<u8>), Answer>,
    /// Every (account1, account2, currency) `trust_line_keylet` was asked for.
    pub trust_line_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>, Vec<u8>)>>,
    /// What `mptoken_issuance_keylet` answers, by (issuer bytes, seq). An unlisted key
    /// answers `InvalidAccount`.
    pub mpt_issuance_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (issuer, seq) `mptoken_issuance_keylet` was asked for.
    pub mpt_issuance_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `mptoken_keylet` answers, by (mptid, holder) bytes. An unlisted key answers
    /// `InvalidParams`.
    pub mptoken_keylets: HashMap<(Vec<u8>, Vec<u8>), Answer>,
    /// Every (mptid, holder) `mptoken_keylet` was asked for.
    pub mptoken_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>)>>,
    /// What `nftoken_offer_keylet` answers, by (account bytes, seq). An unlisted key
    /// answers `InvalidAccount`.
    pub nft_offer_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `nftoken_offer_keylet` was asked for.
    pub nft_offer_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `offer_keylet` answers, by (account bytes, seq). An unlisted key
    /// answers `InvalidAccount`.
    pub offer_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `offer_keylet` was asked for.
    pub offer_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `oracle_keylet` answers, by (account bytes, doc id). An unlisted key
    /// answers `InvalidAccount`.
    pub oracle_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, doc id) `oracle_keylet` was asked for.
    pub oracle_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `paychannel_keylet` answers, by (account, destination, seq). An unlisted
    /// key answers `InvalidAccount`.
    pub paychannel_keylets: HashMap<(Vec<u8>, Vec<u8>, i32), Answer>,
    /// Every (account, destination, seq) `paychannel_keylet` was asked for.
    pub paychannel_keylets_asked: RefCell<Vec<(Vec<u8>, Vec<u8>, i32)>>,
    /// What `permissioned_domain_keylet` answers, by (account bytes, seq). An unlisted
    /// key answers `InvalidAccount`.
    pub domain_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `permissioned_domain_keylet` was asked for.
    pub domain_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `signer_list_keylet` answers, by account bytes. An unlisted account
    /// answers `InvalidAccount`.
    pub signer_list_keylets: HashMap<Vec<u8>, Answer>,
    /// Every account `signer_list_keylet` was asked for.
    pub signer_list_keylets_asked: RefCell<Vec<Vec<u8>>>,
    /// What `ticket_keylet` answers, by (account bytes, seq). An unlisted key answers
    /// `InvalidAccount`.
    pub ticket_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `ticket_keylet` was asked for.
    pub ticket_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `vault_keylet` answers, by (account bytes, seq). An unlisted key answers
    /// `InvalidAccount`.
    pub vault_keylets: HashMap<(Vec<u8>, i32), Answer>,
    /// Every (account, seq) `vault_keylet` was asked for.
    pub vault_keylets_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// What `sha512_half` answers, whatever it is given.
    pub digest: Answer,
    /// Every field selector `get_current_ledger_obj_field` was asked for.
    pub fields_asked: RefCell<Vec<i32>>,
    /// Every input `sha512_half` was given.
    pub digested: RefCell<Vec<Vec<u8>>>,
    /// Every `trace`/`trace_num` call, in order.
    pub traces: RefCell<Vec<Trace>>,
    /// What `update_data` answers, whatever data it is given.
    pub update_data_answer: HostResult<i32>,
    /// Every data blob `update_data` was given.
    pub update_data_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_nft` answers, by (account, nft id) bytes. An unlisted key answers
    /// `InvalidParams`.
    pub nfts: HashMap<(Vec<u8>, Vec<u8>), Answer>,
    /// Every (account, nft id) `get_nft` was asked for.
    pub nfts_asked: RefCell<Vec<(Vec<u8>, Vec<u8>)>>,
    /// What `get_nft_issuer` answers, by nft id. An unlisted id answers `InvalidParams`.
    pub nft_issuers: HashMap<Vec<u8>, Answer>,
    /// Every nft id `get_nft_issuer` was asked for.
    pub nft_issuers_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_nft_taxon` answers, by nft id. An unlisted id answers `InvalidParams`.
    pub nft_taxons: HashMap<Vec<u8>, Answer>,
    /// Every nft id `get_nft_taxon` was asked for.
    pub nft_taxons_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_nft_flags` answers, whatever nft id it is given.
    pub nft_flags_answer: HostResult<i32>,
    /// Every nft id `get_nft_flags` was asked for.
    pub nft_flags_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_nft_transfer_fee` answers, whatever nft id it is given.
    pub nft_fee_answer: HostResult<i32>,
    /// Every nft id `get_nft_transfer_fee` was asked for.
    pub nft_fee_asked: RefCell<Vec<Vec<u8>>>,
    /// What `get_nft_sequence` answers, by nft id. An unlisted id answers
    /// `InvalidParams`.
    pub nft_sequences: HashMap<Vec<u8>, Answer>,
    /// Every nft id `get_nft_sequence` was asked for.
    pub nft_sequences_asked: RefCell<Vec<Vec<u8>>>,

    /// What every float-producing call writes.
    pub float_answer: Answer,
    /// Every `(x, mode)` `float_from_int` was asked for.
    pub float_from_int_asked: RefCell<Vec<(i64, i32)>>,
    /// Every `(x, mode)` `float_from_uint` was asked for.
    pub float_from_uint_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// Every `(amount, mode)` `float_from_stamount` was asked for.
    pub float_from_stamount_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// Every `(number, mode)` `float_from_stnumber` was asked for.
    pub float_from_stnumber_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// Every `(x, mode)` `float_to_int` was asked for.
    pub float_to_int_asked: RefCell<Vec<(Vec<u8>, i32)>>,
    /// The mantissa and exponent bytes `float_to_mant_exp` writes to its two regions.
    pub float_mant_exp_answer: (Vec<u8>, Vec<u8>),
    /// Every float `float_to_mant_exp` was asked for.
    pub float_to_mant_exp_asked: RefCell<Vec<Vec<u8>>>,
    /// Every `(mantissa, exponent, mode)` `float_from_mant_exp` was asked for.
    pub float_from_mant_exp_asked: RefCell<Vec<(i64, i32, i32)>>,
    /// What `float_compare` answers, whatever floats it is given.
    pub float_compare_answer: HostResult<i32>,
    /// Every `(x, y)` `float_compare` was asked for.
    pub float_compare_asked: RefCell<Vec<(Vec<u8>, Vec<u8>)>>,
    /// Every `(x, y, mode)` the four binary float operators were asked for, tagged by
    /// operator name.
    pub float_binops_asked: RefCell<Vec<(&'static str, Vec<u8>, Vec<u8>, i32)>>,
    /// Every `(x, n, mode)` `float_root` and `float_power` were asked for, tagged by
    /// operator name.
    pub float_unops_asked: RefCell<Vec<(&'static str, Vec<u8>, i32, i32)>>,
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
            account_keylets: HashMap::new(),
            account_keylets_asked: RefCell::new(Vec::new()),
            amm_keylets: HashMap::new(),
            amm_keylets_asked: RefCell::new(Vec::new()),
            check_keylets: HashMap::new(),
            check_keylets_asked: RefCell::new(Vec::new()),
            credential_keylets: HashMap::new(),
            credential_keylets_asked: RefCell::new(Vec::new()),
            delegate_keylets: HashMap::new(),
            delegate_keylets_asked: RefCell::new(Vec::new()),
            deposit_preauth_keylets: HashMap::new(),
            deposit_preauth_keylets_asked: RefCell::new(Vec::new()),
            did_keylets: HashMap::new(),
            did_keylets_asked: RefCell::new(Vec::new()),
            escrow_keylets: HashMap::new(),
            escrow_keylets_asked: RefCell::new(Vec::new()),
            trust_line_keylets: HashMap::new(),
            trust_line_keylets_asked: RefCell::new(Vec::new()),
            mpt_issuance_keylets: HashMap::new(),
            mpt_issuance_keylets_asked: RefCell::new(Vec::new()),
            mptoken_keylets: HashMap::new(),
            mptoken_keylets_asked: RefCell::new(Vec::new()),
            nft_offer_keylets: HashMap::new(),
            nft_offer_keylets_asked: RefCell::new(Vec::new()),
            offer_keylets: HashMap::new(),
            offer_keylets_asked: RefCell::new(Vec::new()),
            oracle_keylets: HashMap::new(),
            oracle_keylets_asked: RefCell::new(Vec::new()),
            paychannel_keylets: HashMap::new(),
            paychannel_keylets_asked: RefCell::new(Vec::new()),
            domain_keylets: HashMap::new(),
            domain_keylets_asked: RefCell::new(Vec::new()),
            signer_list_keylets: HashMap::new(),
            signer_list_keylets_asked: RefCell::new(Vec::new()),
            ticket_keylets: HashMap::new(),
            ticket_keylets_asked: RefCell::new(Vec::new()),
            vault_keylets: HashMap::new(),
            vault_keylets_asked: RefCell::new(Vec::new()),
            digest: Answer::filler(32),
            fields_asked: RefCell::new(Vec::new()),
            digested: RefCell::new(Vec::new()),
            traces: RefCell::new(Vec::new()),
            update_data_answer: Ok(0),
            update_data_asked: RefCell::new(Vec::new()),
            nfts: HashMap::new(),
            nfts_asked: RefCell::new(Vec::new()),
            nft_issuers: HashMap::new(),
            nft_issuers_asked: RefCell::new(Vec::new()),
            nft_taxons: HashMap::new(),
            nft_taxons_asked: RefCell::new(Vec::new()),
            nft_flags_answer: Ok(0),
            nft_flags_asked: RefCell::new(Vec::new()),
            nft_fee_answer: Ok(0),
            nft_fee_asked: RefCell::new(Vec::new()),
            nft_sequences: HashMap::new(),
            nft_sequences_asked: RefCell::new(Vec::new()),
            float_answer: Answer::filler(8),
            float_from_int_asked: RefCell::new(Vec::new()),
            float_from_uint_asked: RefCell::new(Vec::new()),
            float_from_stamount_asked: RefCell::new(Vec::new()),
            float_from_stnumber_asked: RefCell::new(Vec::new()),
            float_to_int_asked: RefCell::new(Vec::new()),
            float_mant_exp_answer: (vec![0u8; 8], vec![0u8; 4]),
            float_to_mant_exp_asked: RefCell::new(Vec::new()),
            float_from_mant_exp_asked: RefCell::new(Vec::new()),
            float_compare_answer: Ok(0),
            float_compare_asked: RefCell::new(Vec::new()),
            float_binops_asked: RefCell::new(Vec::new()),
            float_unops_asked: RefCell::new(Vec::new()),
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

    pub fn answering_account_keylet(mut self, account: Vec<u8>, answer: Answer) -> FakeHost {
        self.account_keylets.insert(account, answer);
        self
    }

    pub fn answering_amm_keylet(
        mut self,
        asset1: Vec<u8>,
        asset2: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.amm_keylets.insert((asset1, asset2), answer);
        self
    }

    pub fn answering_check_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.check_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_credential_keylet(
        mut self,
        subject: Vec<u8>,
        issuer: Vec<u8>,
        credential_type: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.credential_keylets
            .insert((subject, issuer, credential_type), answer);
        self
    }

    pub fn answering_delegate_keylet(
        mut self,
        account: Vec<u8>,
        authorize: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.delegate_keylets.insert((account, authorize), answer);
        self
    }

    pub fn answering_deposit_preauth_keylet(
        mut self,
        account: Vec<u8>,
        authorize: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.deposit_preauth_keylets
            .insert((account, authorize), answer);
        self
    }

    pub fn answering_did_keylet(mut self, account: Vec<u8>, answer: Answer) -> FakeHost {
        self.did_keylets.insert(account, answer);
        self
    }

    pub fn answering_escrow_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.escrow_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_trust_line_keylet(
        mut self,
        account1: Vec<u8>,
        account2: Vec<u8>,
        currency: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.trust_line_keylets
            .insert((account1, account2, currency), answer);
        self
    }

    pub fn answering_mpt_issuance_keylet(
        mut self,
        issuer: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.mpt_issuance_keylets.insert((issuer, seq), answer);
        self
    }

    pub fn answering_mptoken_keylet(
        mut self,
        mptid: Vec<u8>,
        holder: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.mptoken_keylets.insert((mptid, holder), answer);
        self
    }

    pub fn answering_nft_offer_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.nft_offer_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_offer_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.offer_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_oracle_keylet(
        mut self,
        account: Vec<u8>,
        doc_id: i32,
        answer: Answer,
    ) -> FakeHost {
        self.oracle_keylets.insert((account, doc_id), answer);
        self
    }

    pub fn answering_paychannel_keylet(
        mut self,
        account: Vec<u8>,
        destination: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.paychannel_keylets
            .insert((account, destination, seq), answer);
        self
    }

    pub fn answering_permissioned_domain_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.domain_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_signer_list_keylet(mut self, account: Vec<u8>, answer: Answer) -> FakeHost {
        self.signer_list_keylets.insert(account, answer);
        self
    }

    pub fn answering_ticket_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.ticket_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_vault_keylet(
        mut self,
        account: Vec<u8>,
        seq: i32,
        answer: Answer,
    ) -> FakeHost {
        self.vault_keylets.insert((account, seq), answer);
        self
    }

    pub fn answering_digest(mut self, answer: Answer) -> FakeHost {
        self.digest = answer;
        self
    }

    pub fn answering_update_data(mut self, answer: HostResult<i32>) -> FakeHost {
        self.update_data_answer = answer;
        self
    }

    pub fn answering_get_nft(
        mut self,
        account: Vec<u8>,
        nft_id: Vec<u8>,
        answer: Answer,
    ) -> FakeHost {
        self.nfts.insert((account, nft_id), answer);
        self
    }

    pub fn answering_nft_issuer(mut self, nft_id: Vec<u8>, answer: Answer) -> FakeHost {
        self.nft_issuers.insert(nft_id, answer);
        self
    }

    pub fn answering_nft_taxon(mut self, nft_id: Vec<u8>, answer: Answer) -> FakeHost {
        self.nft_taxons.insert(nft_id, answer);
        self
    }

    pub fn answering_nft_flags(mut self, answer: HostResult<i32>) -> FakeHost {
        self.nft_flags_answer = answer;
        self
    }

    pub fn answering_nft_transfer_fee(mut self, answer: HostResult<i32>) -> FakeHost {
        self.nft_fee_answer = answer;
        self
    }

    pub fn answering_nft_sequence(mut self, nft_id: Vec<u8>, answer: Answer) -> FakeHost {
        self.nft_sequences.insert(nft_id, answer);
        self
    }

    pub fn answering_float(mut self, answer: Answer) -> FakeHost {
        self.float_answer = answer;
        self
    }

    pub fn answering_float_mant_exp(mut self, mantissa: Vec<u8>, exponent: Vec<u8>) -> FakeHost {
        self.float_mant_exp_answer = (mantissa, exponent);
        self
    }

    pub fn answering_float_compare(mut self, answer: HostResult<i32>) -> FakeHost {
        self.float_compare_answer = answer;
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

    fn account_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.account_keylets_asked
            .borrow_mut()
            .push(account.to_vec());
        match self.account_keylets.get(account) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn amm_keylet(&self, asset1: &[u8], asset2: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.amm_keylets_asked
            .borrow_mut()
            .push((asset1.to_vec(), asset2.to_vec()));
        match self.amm_keylets.get(&(asset1.to_vec(), asset2.to_vec())) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidParams),
        }
    }

    fn check_keylet(&self, account: &[u8], seq: i32, out: &mut [u8]) -> HostResult<usize> {
        self.check_keylets_asked
            .borrow_mut()
            .push((account.to_vec(), seq));
        match self.check_keylets.get(&(account.to_vec(), seq)) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn credential_keylet(
        &self,
        subject: &[u8],
        issuer: &[u8],
        credential_type: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (subject.to_vec(), issuer.to_vec(), credential_type.to_vec());
        self.credential_keylets_asked.borrow_mut().push(key.clone());
        match self.credential_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn delegate_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (account.to_vec(), authorize.to_vec());
        self.delegate_keylets_asked.borrow_mut().push(key.clone());
        match self.delegate_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn deposit_preauth_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (account.to_vec(), authorize.to_vec());
        self.deposit_preauth_keylets_asked
            .borrow_mut()
            .push(key.clone());
        match self.deposit_preauth_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn did_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.did_keylets_asked.borrow_mut().push(account.to_vec());
        match self.did_keylets.get(account) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn escrow_keylet(&self, account: &[u8], seq: i32, out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), seq);
        self.escrow_keylets_asked.borrow_mut().push(key.clone());
        match self.escrow_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn trust_line_keylet(
        &self,
        account1: &[u8],
        account2: &[u8],
        currency: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (account1.to_vec(), account2.to_vec(), currency.to_vec());
        self.trust_line_keylets_asked.borrow_mut().push(key.clone());
        match self.trust_line_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn mptoken_issuance_keylet(
        &self,
        issuer: &[u8],
        seq: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (issuer.to_vec(), seq);
        self.mpt_issuance_keylets_asked
            .borrow_mut()
            .push(key.clone());
        match self.mpt_issuance_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn mptoken_keylet(&self, mptid: &[u8], holder: &[u8], out: &mut [u8]) -> HostResult<usize> {
        let key = (mptid.to_vec(), holder.to_vec());
        self.mptoken_keylets_asked.borrow_mut().push(key.clone());
        match self.mptoken_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidParams),
        }
    }

    fn nftoken_offer_keylet(&self, account: &[u8], seq: i32, out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), seq);
        self.nft_offer_keylets_asked.borrow_mut().push(key.clone());
        match self.nft_offer_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn offer_keylet(&self, account: &[u8], seq: i32, out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), seq);
        self.offer_keylets_asked.borrow_mut().push(key.clone());
        match self.offer_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn oracle_keylet(&self, account: &[u8], doc_id: i32, out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), doc_id);
        self.oracle_keylets_asked.borrow_mut().push(key.clone());
        match self.oracle_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn paychannel_keylet(
        &self,
        account: &[u8],
        destination: &[u8],
        seq: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (account.to_vec(), destination.to_vec(), seq);
        self.paychannel_keylets_asked.borrow_mut().push(key.clone());
        match self.paychannel_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn permissioned_domain_keylet(
        &self,
        account: &[u8],
        seq: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        let key = (account.to_vec(), seq);
        self.domain_keylets_asked.borrow_mut().push(key.clone());
        match self.domain_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn signer_list_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.signer_list_keylets_asked
            .borrow_mut()
            .push(account.to_vec());
        match self.signer_list_keylets.get(account) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn ticket_keylet(&self, account: &[u8], seq: i32, out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), seq);
        self.ticket_keylets_asked.borrow_mut().push(key.clone());
        match self.ticket_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
        }
    }

    fn vault_keylet(&self, account: &[u8], seq: i32, out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), seq);
        self.vault_keylets_asked.borrow_mut().push(key.clone());
        match self.vault_keylets.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidAccount),
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

    fn update_data(&self, data: &[u8]) -> HostResult<i32> {
        self.update_data_asked.borrow_mut().push(data.to_vec());
        self.update_data_answer
    }

    fn get_nft(&self, account: &[u8], nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        let key = (account.to_vec(), nft_id.to_vec());
        self.nfts_asked.borrow_mut().push(key.clone());
        match self.nfts.get(&key) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidParams),
        }
    }

    fn get_nft_issuer(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.nft_issuers_asked.borrow_mut().push(nft_id.to_vec());
        match self.nft_issuers.get(nft_id) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidParams),
        }
    }

    fn get_nft_taxon(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.nft_taxons_asked.borrow_mut().push(nft_id.to_vec());
        match self.nft_taxons.get(nft_id) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidParams),
        }
    }

    fn get_nft_flags(&self, nft_id: &[u8]) -> HostResult<i32> {
        self.nft_flags_asked.borrow_mut().push(nft_id.to_vec());
        self.nft_flags_answer
    }

    fn get_nft_transfer_fee(&self, nft_id: &[u8]) -> HostResult<i32> {
        self.nft_fee_asked.borrow_mut().push(nft_id.to_vec());
        self.nft_fee_answer
    }

    fn get_nft_sequence(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        self.nft_sequences_asked.borrow_mut().push(nft_id.to_vec());
        match self.nft_sequences.get(nft_id) {
            Some(answer) => answer.fill(out),
            None => Err(HostError::InvalidParams),
        }
    }

    fn float_from_int(&self, x: i64, mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_from_int_asked.borrow_mut().push((x, mode));
        self.float_answer.fill(out)
    }

    fn float_from_uint(&self, x: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_from_uint_asked
            .borrow_mut()
            .push((x.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_from_stamount(&self, amount: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_from_stamount_asked
            .borrow_mut()
            .push((amount.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_from_stnumber(&self, number: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_from_stnumber_asked
            .borrow_mut()
            .push((number.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_to_int(&self, x: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_to_int_asked
            .borrow_mut()
            .push((x.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_to_mant_exp(
        &self,
        x: &[u8],
        mantissa_out: &mut [u8],
        exponent_out: &mut [u8],
    ) -> HostResult<usize> {
        self.float_to_mant_exp_asked.borrow_mut().push(x.to_vec());
        let (mantissa, exponent) = &self.float_mant_exp_answer;
        mantissa_out[..mantissa.len()].copy_from_slice(mantissa);
        exponent_out[..exponent.len()].copy_from_slice(exponent);
        Ok(mantissa.len() + exponent.len())
    }

    fn float_from_mant_exp(
        &self,
        mantissa: i64,
        exponent: i32,
        mode: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        self.float_from_mant_exp_asked
            .borrow_mut()
            .push((mantissa, exponent, mode));
        self.float_answer.fill(out)
    }

    fn float_compare(&self, x: &[u8], y: &[u8]) -> HostResult<i32> {
        self.float_compare_asked
            .borrow_mut()
            .push((x.to_vec(), y.to_vec()));
        self.float_compare_answer
    }

    fn float_add(&self, x: &[u8], y: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_binops_asked
            .borrow_mut()
            .push(("add", x.to_vec(), y.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_subtract(&self, x: &[u8], y: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_binops_asked
            .borrow_mut()
            .push(("sub", x.to_vec(), y.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_multiply(&self, x: &[u8], y: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_binops_asked
            .borrow_mut()
            .push(("mult", x.to_vec(), y.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_divide(&self, x: &[u8], y: &[u8], mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_binops_asked
            .borrow_mut()
            .push(("div", x.to_vec(), y.to_vec(), mode));
        self.float_answer.fill(out)
    }

    fn float_root(&self, x: &[u8], n: i32, mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_unops_asked
            .borrow_mut()
            .push(("root", x.to_vec(), n, mode));
        self.float_answer.fill(out)
    }

    fn float_power(&self, x: &[u8], n: i32, mode: i32, out: &mut [u8]) -> HostResult<usize> {
        self.float_unops_asked
            .borrow_mut()
            .push(("pow", x.to_vec(), n, mode));
        self.float_answer.fill(out)
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
    pub const ACCOUNTROOT_ID: &str = r#"(import "host_lib" "accountroot_id" (func $accountroot_id (param i32 i32 i32 i32) (result i32)))"#;
    pub const AMM_ID: &str = r#"(import "host_lib" "amm_id" (func $amm_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const CHECK_ID: &str = r#"(import "host_lib" "check_id" (func $check_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const CREDENTIAL_ID: &str = r#"(import "host_lib" "credential_id" (func $credential_id (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const DELEGATE_ID: &str = r#"(import "host_lib" "delegate_id" (func $delegate_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const DEPOSIT_PREAUTH_ID: &str = r#"(import "host_lib" "deposit_preauth_id" (func $deposit_preauth_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const DID_ID: &str =
        r#"(import "host_lib" "did_id" (func $did_id (param i32 i32 i32 i32) (result i32)))"#;
    pub const ESCROW_ID: &str = r#"(import "host_lib" "escrow_id" (func $escrow_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const TRUSTLINE_ID: &str = r#"(import "host_lib" "trustline_id" (func $trustline_id (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const MPT_ISSUANCE_ID: &str = r#"(import "host_lib" "mpt_issuance_id" (func $mpt_issuance_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const MPTOKEN_ID: &str = r#"(import "host_lib" "mptoken_id" (func $mptoken_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const NFT_OFFER_ID: &str = r#"(import "host_lib" "nft_offer_id" (func $nft_offer_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const OFFER_ID: &str = r#"(import "host_lib" "offer_id" (func $offer_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const ORACLE_ID: &str = r#"(import "host_lib" "oracle_id" (func $oracle_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const PAYCHAN_ID: &str = r#"(import "host_lib" "paychan_id" (func $paychan_id (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const PERMISSIONED_DOMAIN_ID: &str = r#"(import "host_lib" "permissioned_domain_id" (func $permissioned_domain_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const SIGNERS_ID: &str = r#"(import "host_lib" "signers_id" (func $signers_id (param i32 i32 i32 i32) (result i32)))"#;
    pub const TICKET_ID: &str = r#"(import "host_lib" "ticket_id" (func $ticket_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const VAULT_ID: &str = r#"(import "host_lib" "vault_id" (func $vault_id (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const SHA512_HALF: &str = r#"(import "host_lib" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))"#;
    pub const TRACE: &str =
        r#"(import "host_lib" "trace" (func $trace (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const TRACE_NUM: &str =
        r#"(import "host_lib" "trace_num" (func $trace_num (param i32 i32 i64) (result i32)))"#;
    pub const SET_DATA: &str =
        r#"(import "host_lib" "set_data" (func $set_data (param i32 i32) (result i32)))"#;
    pub const NFT_URI: &str = r#"(import "host_lib" "nft_uri" (func $nft_uri (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const NFT_ISSUER: &str = r#"(import "host_lib" "nft_issuer" (func $nft_issuer (param i32 i32 i32 i32) (result i32)))"#;
    pub const NFT_TAXON: &str =
        r#"(import "host_lib" "nft_taxon" (func $nft_taxon (param i32 i32 i32 i32) (result i32)))"#;
    pub const NFT_FLAGS: &str =
        r#"(import "host_lib" "nft_flags" (func $nft_flags (param i32 i32) (result i32)))"#;
    pub const NFT_XFER_FEE: &str =
        r#"(import "host_lib" "nft_xfer_fee" (func $nft_xfer_fee (param i32 i32) (result i32)))"#;
    pub const NFT_SERIAL: &str = r#"(import "host_lib" "nft_serial" (func $nft_serial (param i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_FROM_INT: &str = r#"(import "host_lib" "float_from_int" (func $float_from_int (param i64 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_FROM_UINT: &str = r#"(import "host_lib" "float_from_uint" (func $float_from_uint (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_FROM_STAMOUNT: &str = r#"(import "host_lib" "float_from_stamount" (func $float_from_stamount (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_FROM_STNUMBER: &str = r#"(import "host_lib" "float_from_stnumber" (func $float_from_stnumber (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_TO_INT: &str = r#"(import "host_lib" "float_to_int" (func $float_to_int (param i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_TO_MANT_EXP: &str = r#"(import "host_lib" "float_to_mant_exp" (func $float_to_mant_exp (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_FROM_MANT_EXP: &str = r#"(import "host_lib" "float_from_mant_exp" (func $float_from_mant_exp (param i64 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_CMP: &str =
        r#"(import "host_lib" "float_cmp" (func $float_cmp (param i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_ADD: &str = r#"(import "host_lib" "float_add" (func $float_add (param i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_SUB: &str = r#"(import "host_lib" "float_sub" (func $float_sub (param i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_MULT: &str = r#"(import "host_lib" "float_mult" (func $float_mult (param i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_DIV: &str = r#"(import "host_lib" "float_div" (func $float_div (param i32 i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_ROOT: &str = r#"(import "host_lib" "float_root" (func $float_root (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
    pub const FLOAT_POW: &str = r#"(import "host_lib" "float_pow" (func $float_pow (param i32 i32 i32 i32 i32 i32) (result i32)))"#;
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
