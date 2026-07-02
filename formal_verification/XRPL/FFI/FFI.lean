/-
Root module of the FFI exports, one import per FFI file. C++ initializes exactly this module
(`initialize_XRPL_XRPL_FFI_FFI` in LeanSuite.h) and that initializes everything imported
here, so a new FFI file must be added to the imports below to be callable from C++.

Note: this must stay a plain comment. The module docstring form (with the bang) is a
command, and commands may not precede `import`.
-/
import XRPL.FFI.Protocol.NumberFFI
