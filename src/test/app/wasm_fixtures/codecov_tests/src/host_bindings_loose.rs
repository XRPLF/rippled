//TODO add docs after discussing the interface
//Note that Craft currently does not honor the rounding modes
#[allow(unused)]
pub const FLOAT_ROUNDING_MODES_TO_NEAREST: i32 = 0;
#[allow(unused)]
pub const FLOAT_ROUNDING_MODES_TOWARDS_ZERO: i32 = 1;
#[allow(unused)]
pub const FLOAT_ROUNDING_MODES_DOWNWARD: i32 = 2;
#[allow(unused)]
pub const FLOAT_ROUNDING_MODES_UPWARD: i32 = 3;

// pub enum RippledRoundingModes{
//     ToNearest = 0,
//     TowardsZero = 1,
//     DOWNWARD = 2,
//     UPWARD = 3
// }

#[allow(unused)]
#[link(wasm_import_module = "host_lib")]
unsafe extern "C" {
    pub fn parent_ldgr_hash(out_buff_ptr: i32, out_buff_len: i32) -> i32;

    pub fn cache_le(keylet_ptr: i32, keylet_len: i32, cache_num: i32) -> i32;

    pub fn tx_inner_arr_len(locator_ptr: i32, locator_len: i32) -> i32;

    pub fn accountroot_id(
        account_ptr: i32,
        account_len: i32,
        out_buff_ptr: *mut u8,
        out_buff_len: usize,
    ) -> i32;

    pub fn trustline_id(
        account1_ptr: *const u8,
        account1_len: usize,
        account2_ptr: *const u8,
        account2_len: usize,
        currency_ptr: i32,
        currency_len: i32,
        out_buff_ptr: *mut u8,
        out_buff_len: usize,
    ) -> i32;

    // Same wasm functype as the real binding, so this is not a second import of
    // host_lib.trace. Loose i32 pointers exercise the out-of-bounds path.
    #[link_name = "trace"]
    pub fn trace_loose(
        msg_read_ptr: i32,
        msg_read_len: i32,
        data_type: i32,
        data_read_ptr: i32,
        data_read_len: i32,
    );
}
