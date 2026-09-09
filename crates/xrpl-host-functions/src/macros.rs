//! The `macro_rules!` behind the three hand-listed enums, [`crate::HostError`],
//! [`crate::TraceDataType`] and [`crate::FloatOrdering`].
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

            /// The negative wire value a failed call returns. Every code but
            /// `InternalFatal` is one a guest reads off that value.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

            /// Reconstruct a `HostError` from its wire code.
            ///
            /// A code this ABI does not define is `InternalFatal`: an answer the
            /// caller cannot act on is the call not having been served, and that is
            /// the variant which says so. Positive values are not errors at all and go
            /// the same way, since this is reached only once a negative return has
            /// been read as a failure.
            pub const fn from_code(code: i32) -> HostError {
                match code {
                    $($code => HostError::$variant,)+
                    _ => HostError::InternalFatal,
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

/// Declares [`crate::FloatOrdering`] from one list, so `FloatOrdering::ALL`,
/// `FloatOrdering::code` and `FloatOrdering::from_code` cannot fall behind the
/// variants — the reason the two macros above are written this way.
macro_rules! float_orderings {
    ($($(#[$doc:meta])* $variant:ident = $code:literal,)+) => {
        /// The verdict [`HostFunctions::float_compare`] answers, read as the placing of
        /// `x` against `y`.
        ///
        /// **Not C's `memcmp` convention**, and the sign is why: the wire's negative range
        /// belongs to [`HostError`], so a comparison cannot spell "less" as a negative
        /// without colliding with a code like `FloatInputMalformed`. Every variant is
        /// therefore non-negative, and a guest branches on the value rather than its sign.
        ///
        /// The discriminants are wire values shared with the guest stdlib: append only,
        /// never renumber. As with [`TraceDataType`], the host side needs a second
        /// declaration — `cxx` cannot be a dependency here — so `WasmCommon.h` declares
        /// the same three codes for C++.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        #[repr(i32)]
        pub enum FloatOrdering {
            $($(#[$doc])* $variant = $code,)+
        }

        impl FloatOrdering {
            /// Every verdict a comparison may answer, in code order.
            pub const ALL: &'static [FloatOrdering] = &[$(FloatOrdering::$variant,)+];

            /// The non-negative wire value that names this verdict.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

            /// The verdict `code` names, or `None`.
            ///
            /// `None` is not a comparison a guest can act on: a total order has exactly
            /// these three outcomes, so any other non-negative value is the host
            /// contradicting the ABI. Negative values go the same way, being errors
            /// rather than verdicts.
            pub const fn from_code(code: i32) -> Option<FloatOrdering> {
                match code {
                    $($code => Some(FloatOrdering::$variant),)+
                    _ => None,
                }
            }
        }
    };
}
