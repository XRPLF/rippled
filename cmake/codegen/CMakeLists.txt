#[===================================================================[
   Protocol Autogen - Standalone project

   Exposes the 'setup_code_gen' and 'code_gen' targets on their own, without
   configuring the rest of xrpl. Code generation is pure Python, so this needs
   neither the dependencies nor a compiler, which makes it usable in CI and by
   contributors who only want to regenerate the protocol wrapper classes:

     cmake -S cmake/codegen -B build/codegen
     cmake --build build/codegen --target setup_code_gen
     cmake --build build/codegen --target code_gen

   The targets are identical to the ones offered by the top-level build, since
   both come from cmake/XrplProtocolAutogen.cmake.
#]===================================================================]

cmake_minimum_required(VERSION 3.16)

project(xrpl_codegen LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../XrplProtocolAutogen.cmake")
