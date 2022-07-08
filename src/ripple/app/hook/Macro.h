/**
 * RH NOTE:
 * This file contains macros for converting the hook api definitions into the currently used wasm runtime.
 * Web assembly runtimes are more or less fungible, and at time of writing hooks has moved to WasmEdge from SSVM
 * and before that from wasmer. 
 * After the first move it was decided there should be a relatively static interface for the definition and
 * programming of the hook api itself, with the runtime-specific behaviour hidden away by templates or macros.
 * Macros are more expressive and can themselves include templates so macros were then used.
 */

#define LPAREN (
#define LPAREN (
#define RPAREN )
#define COMMA ,
#define EXPAND(...) __VA_ARGS__
#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define CAT2(L, R) CAT2_(L, R)
#define CAT2_(L, R) L ## R
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__
#define EMPTY()
#define DEFER(id) id EMPTY()
#define OBSTRUCT(...) __VA_ARGS__ DEFER(EMPTY)()
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(__drop, ...) VA_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define FIRST(a, b) a
#define SECOND(a, b) b
#define STRIP_TYPES(...) FOR_VARS(SECOND, 0, __VA_ARGS__) 

#define DELIM_0 ,
#define DELIM_1 
#define DELIM_2 ;
#define DELIM(S) DELIM_##S

#define FOR_VAR_1(T, S, D) SEP(T, D)
#define FOR_VAR_2(T, S, a, b)    FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_1(T, S, b)
#define FOR_VAR_3(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_2(T, S, __VA_ARGS__)
#define FOR_VAR_4(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_3(T, S, __VA_ARGS__)
#define FOR_VAR_5(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_4(T, S, __VA_ARGS__)
#define FOR_VAR_6(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_5(T, S, __VA_ARGS__)
#define FOR_VAR_7(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_6(T, S, __VA_ARGS__)
#define FOR_VAR_8(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_7(T, S, __VA_ARGS__)
#define FOR_VAR_9(T, S, a, ...)  FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_8(T, S, __VA_ARGS__)
#define FOR_VAR_10(T, S, a, ...) FOR_VAR_1(T, S, a) DELIM(S) FOR_VAR_9(T, S, __VA_ARGS__)
#define FOR_VARS(T, S, ...)\
    DEFER(CAT(FOR_VAR_,VA_NARGS(NULL, __VA_ARGS__))CAT(LPAREN T COMMA S COMMA OBSTRUCT(__VA_ARGS__) RPAREN))

#define SEP(OP, D) EXPAND(OP CAT2(SEP_, D) RPAREN)
#define SEP_uint32_t    LPAREN uint32_t COMMA
#define SEP_int32_t     LPAREN int32_t COMMA
#define SEP_uint64_t    LPAREN uint64_t COMMA
#define SEP_int64_t     LPAREN int64_t COMMA

#define VAL_uint32_t    WasmEdge_ValueGetI32(in[_stack++])
#define VAL_int32_t     WasmEdge_ValueGetI32(in[_stack++])
#define VAL_uint64_t    WasmEdge_ValueGetI64(in[_stack++])
#define VAL_int64_t     WasmEdge_ValueGetI64(in[_stack++])

#define VAR_ASSIGN(T, V)\
    T V = CAT(VAL_ ##T)

#define RET_uint32_t(return_code)   WasmEdge_ValueGenI32(return_code)
#define RET_int32_t(return_code)    WasmEdge_ValueGenI32(return_code)
#define RET_uint64_t(return_code)   WasmEdge_ValueGenI64(return_code)
#define RET_int64_t(return_code)    WasmEdge_ValueGenI64(return_code)

#define RET_ASSIGN(T, return_code)\
    CAT2(RET_,T(return_code))

#define TYP_uint32_t WasmEdge_ValType_I32
#define TYP_int32_t WasmEdge_ValType_I32
#define TYP_uint64_t WasmEdge_ValType_I64
#define TYP_int64_t WasmEdge_ValType_I64

#define WASM_VAL_TYPE(T, b)\
    CAT2(TYP_,T)

#define DECLARE_HOOK_FUNCTION(R, F, ...)\
    R F(hook::HookContext& hookCtx, WasmEdge_MemoryInstanceContext& memoryCtx, __VA_ARGS__);\
    extern WasmEdge_Result WasmFunction##F(\
        void *data_ptr, WasmEdge_MemoryInstanceContext *memCtx,\
        const WasmEdge_Value *in, WasmEdge_Value *out);\
    extern WasmEdge_ValType WasmFunctionParams##F[];\
    extern WasmEdge_ValType WasmFunctionResult##F[];\
    extern WasmEdge_FunctionTypeContext* WasmFunctionType##F;\
    extern WasmEdge_String WasmFunctionName##F;


#define DECLARE_HOOK_FUNCNARG(R, F)\
    R F(hook::HookContext& hookCtx, WasmEdge_MemoryInstanceContext& memoryCtx);\
    extern WasmEdge_Result WasmFunction##F(\
        void *data_ptr, WasmEdge_MemoryInstanceContext *memCtx,\
        const WasmEdge_Value *in, WasmEdge_Value *out);\
    extern WasmEdge_ValType WasmFunctionResult##F[];\
    extern WasmEdge_FunctionTypeContext* WasmFunctionType##F;\
    extern WasmEdge_String WasmFunctionName##F;

#define DEFINE_HOOK_FUNCTION(R, F, ...)\
    WasmEdge_Result hook_api::WasmFunction##F(\
        void *data_ptr, WasmEdge_MemoryInstanceContext *memCtx,\
        const WasmEdge_Value *in, WasmEdge_Value *out)\
    {\
        int _stack = 0;\
        FOR_VARS(VAR_ASSIGN, 2, __VA_ARGS__);\
        hook::HookContext* hookCtx = reinterpret_cast<hook::HookContext*>(data_ptr);\
        R return_code = hook_api::F(*hookCtx, *memCtx, STRIP_TYPES(__VA_ARGS__));\
        if (return_code == RC_ROLLBACK || return_code == RC_ACCEPT)\
            return WasmEdge_Result_Terminate;\
        out[0] = RET_ASSIGN(R, return_code);\
        return WasmEdge_Result_Success;\
    };\
    WasmEdge_ValType hook_api::WasmFunctionParams##F[] = { FOR_VARS(WASM_VAL_TYPE, 0, __VA_ARGS__) };\
    WasmEdge_ValType hook_api::WasmFunctionResult##F[1] = { WASM_VAL_TYPE(R, dummy) };\
    WasmEdge_FunctionTypeContext* hook_api::WasmFunctionType##F = WasmEdge_FunctionTypeCreate(\
            WasmFunctionParams##F, VA_NARGS(NULL, __VA_ARGS__),\
            WasmFunctionResult##F, 1);\
    WasmEdge_String hook_api::WasmFunctionName##F = WasmEdge_StringCreateByCString(#F);\
    R hook_api::F(hook::HookContext& hookCtx, WasmEdge_MemoryInstanceContext& memoryCtx, __VA_ARGS__)

#define DEFINE_HOOK_FUNCNARG(R, F)\
    WasmEdge_Result hook_api::WasmFunction##F(\
        void *data_ptr, WasmEdge_MemoryInstanceContext *memCtx,\
        const WasmEdge_Value *in, WasmEdge_Value *out)\
    {\
        hook::HookContext* hookCtx = reinterpret_cast<hook::HookContext*>(data_ptr);\
        R return_code = hook_api::F(*hookCtx, *memCtx);\
        if (return_code == RC_ROLLBACK || return_code == RC_ACCEPT)\
            return WasmEdge_Result_Terminate;\
        out[0] = CAT2(RET_,R(return_code));\
        return WasmEdge_Result_Success;\
    };\
    WasmEdge_ValType hook_api::WasmFunctionResult##F[1] = { WASM_VAL_TYPE(R, dummy) };\
    WasmEdge_FunctionTypeContext* hook_api::WasmFunctionType##F = \
        WasmEdge_FunctionTypeCreate({}, 0, WasmFunctionResult##F, 1);\
    WasmEdge_String hook_api::WasmFunctionName##F = WasmEdge_StringCreateByCString(#F);\
    R hook_api::F(hook::HookContext& hookCtx, WasmEdge_MemoryInstanceContext& memoryCtx)




#define COMPUTE_HOOK_DATA_OWNER_COUNT(state_count)\
    (std::ceil( (double)state_count/(double)5.0 ))

#define HOOK_SETUP()\
    [[maybe_unused]] ApplyContext& applyCtx = hookCtx.applyCtx;\
    [[maybe_unused]] auto& view = applyCtx.view();\
    [[maybe_unused]] auto j = applyCtx.app.journal("View");\
    [[maybe_unused]] unsigned char* memory = WasmEdge_MemoryInstanceGetPointer(&memoryCtx, 0, 0);\
    [[maybe_unused]] const uint64_t memory_length = WasmEdge_MemoryInstanceGetPageSize(&memoryCtx) * \
        WasmEdge_kPageSize;

#define WRITE_WASM_MEMORY(bytes_written, guest_dst_ptr, guest_dst_len,\
        host_src_ptr, host_src_len, host_memory_ptr, guest_memory_length)\
{\
    int64_t bytes_to_write = std::min(static_cast<int64_t>(host_src_len), static_cast<int64_t>(guest_dst_len));\
    if (guest_dst_ptr + bytes_to_write > guest_memory_length)\
    {\
        JLOG(j.warn())\
            << "HookError[" << HC_ACC() << "]: "\
            << __func__ << " tried to retreive blob of " << host_src_len\
            << " bytes past end of wasm memory";\
        return OUT_OF_BOUNDS;\
    }\
    WasmEdge_MemoryInstanceSetData(&memoryCtx, \
            reinterpret_cast<const uint8_t*>(host_src_ptr), guest_dst_ptr, bytes_to_write);\
    bytes_written += bytes_to_write;\
}

#define WRITE_WASM_MEMORY_AND_RETURN(guest_dst_ptr, guest_dst_len,\
        host_src_ptr, host_src_len, host_memory_ptr, guest_memory_length)\
{\
    int64_t bytes_written = 0;\
    WRITE_WASM_MEMORY(bytes_written, guest_dst_ptr, guest_dst_len, host_src_ptr,\
            host_src_len, host_memory_ptr, guest_memory_length);\
    return bytes_written;\
}

#define RETURN_HOOK_TRACE(read_ptr, read_len, t)\
{\
    int rl = read_len;\
    if (rl > 1024)\
        rl = 1024;\
    if (NOT_IN_BOUNDS(read_ptr, read_len, memory_length))\
    {\
        return OUT_OF_BOUNDS;\
    }\
    else if (read_ptr == 0 && read_len == 0)\
    {\
        JLOG(j.trace()) \
            << "HookTrace[" << HC_ACC() << "]: " << t;\
    }\
    else if (is_UTF16LE(memory + read_ptr, rl))\
    {\
        uint8_t output[1024];\
        int len = rl / 2;\
        for (int i = 0; i < len && i < 512; ++i)\
            output[i] = memory[read_ptr + i * 2];\
        JLOG(j.trace()) \
            << "HookTrace[" << HC_ACC() << "]: "\
            << std::string_view((const char*)output, (size_t)(len)) << " "\
            << t;\
    }\
    else\
    {\
        JLOG(j.trace()) \
            << "HookTrace[" << HC_ACC() << "]: "\
            << std::string_view((const char*)(memory + read_ptr), (size_t)rl) << " "\
            << t;\
    }\
    return 0;\
}
// ptr = pointer inside the wasm memory space
#define NOT_IN_BOUNDS(ptr, len, memory_length)\
    ((static_cast<uint64_t>(ptr) > static_cast<uint64_t>(memory_length)) || \
     ((static_cast<uint64_t>(ptr) + static_cast<uint64_t>(len)) > static_cast<uint64_t>(memory_length)))

#define HOOK_EXIT(read_ptr, read_len, error_code, exit_type)\
{\
    if (read_len > 256) read_len = 256;\
    if (read_ptr) {\
        if (NOT_IN_BOUNDS(read_ptr, read_len, memory_length)) {\
            JLOG(j.warn())\
                << "HookError[" << HC_ACC() << "]: "\
                << "Tried to accept/rollback but specified memory outside of the wasm instance " <<\
                "limit when specifying a reason string";\
            return OUT_OF_BOUNDS;\
        }\
        /* assembly script and some other languages use utf16 for strings */\
        if (is_UTF16LE(read_ptr + memory, read_len))\
        {\
            uint8_t output[128];\
            int len = read_len / 2; /* is_UTF16LE will only return true if read_len is even */\
            for (int i = 0; i < len; ++i)\
                output[i] = memory[read_ptr + i * 2];\
            hookCtx.result.exitReason = std::string((const char*)(output), (size_t)len);\
        } else\
            hookCtx.result.exitReason = std::string((const char*)(memory + read_ptr), (size_t)read_len);\
    }\
    hookCtx.result.exitType = exit_type;\
    hookCtx.result.exitCode = error_code;\
    return (exit_type == hook_api::ExitType::ACCEPT ? RC_ACCEPT : RC_ROLLBACK);\
}
