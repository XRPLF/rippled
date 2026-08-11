//! The `macro_rules!` behind the two hand-listed enums, [`crate::HostError`] and
//! [`crate::TraceDataType`].
//!
//! Each takes one list of `Variant = code,` and expands the enum together with the
//! `ALL`/`code`/`from_code` set that must not fall behind it. The lists themselves stay
//! in `lib.rs`, beside the `host_functions!` block.

/// Declares [`crate::HostError`] from one list: the variants, `HostError::ALL` and
/// `HostError::from_code`'s table all expand from the codes given.
///
/// One list is what makes `ALL` complete. Rust cannot enumerate an enum's
/// variants — an exhaustive `match` forces an arm per variant but gives nothing to
/// iterate — so a hand-written `ALL` beside a hand-written enum could only be kept
/// in step by review, and `ALL`'s whole purpose is to be the set a test can trust.
/// A code added to the list gains its `ALL` entry and its `from_code` arm by
/// construction. `HostFunctionSpec::ALL` is complete the same way, from the
/// `host_functions!` block.
macro_rules! host_errors {
    ($($(#[$doc:meta])* $variant:ident = $code:literal,)+) => {
        /// Error codes a host function may return.
        ///
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        #[repr(i32)]
        pub enum HostError {
            $($(#[$doc])* $variant = $code,)+
        }

        impl HostError {
            /// Every error a host function may return, in code order.
            ///
            /// The complete set, and complete by construction: a wasm engine's
            /// split between the codes it hands the guest and the conditions it
            /// traps on is a decision per variant, so the test that checks the
            /// split iterates this and a code added to the ABI cannot slip past it.
            pub const ALL: &'static [HostError] = &[$(HostError::$variant,)+];

            /// The negative wire value the guest sees as the function's return code.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

            /// Reconstruct a `HostError` from its wire code.
            ///
            /// A code this ABI does not define is `Unimplemented`: an answer the
            /// caller cannot act on is the call not having been served. Positive
            /// values are not errors at all and go the same way, since this is
            /// reached only once a negative return has been read as a failure.
            pub const fn from_code(code: i32) -> HostError {
                match code {
                    $($code => HostError::$variant,)+
                    _ => HostError::Unimplemented,
                }
            }
        }
    };
}

/// Declares [`crate::TraceDataType`] from one list, so `TraceDataType::ALL`,
/// `TraceDataType::code` and `TraceDataType::from_code` cannot fall behind the
/// variants — the reason `host_errors!` above is written this way.
macro_rules! trace_data_types {
    ($($(#[$doc:meta])* $variant:ident = $code:literal,)+) => {
        /// How [`HostFunctions::trace`] is to read its data buffer.
        ///
        /// The discriminants are wire values shared with the guest stdlib: append only,
        /// never renumber. They start at 1, so a zeroed argument names no type rather
        /// than the first one.
        ///
        /// This is the declaration a guest and a host both compile against. The host
        /// side needs a second one — `cxx` cannot be a dependency here, since this
        /// crate also links into the guest — so `xrpl-wasm-vm-ffi` declares a shared
        /// enum for C++ and converts, exhaustively, from this.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        #[repr(i32)]
        pub enum TraceDataType {
            $($(#[$doc])* $variant = $code,)+
        }

        impl TraceDataType {
            /// Every data type a guest may name, in code order.
            pub const ALL: &'static [TraceDataType] = &[$(TraceDataType::$variant,)+];

            /// The wire value a guest passes to name this type.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

            /// The type `code` names, or `None`: the engine drops a call it cannot
            /// read rather than guessing at a rendering the guest did not ask for.
            pub const fn from_code(code: i32) -> Option<TraceDataType> {
                match code {
                    $($code => Some(TraceDataType::$variant),)+
                    _ => None,
                }
            }
        }
    };
}
