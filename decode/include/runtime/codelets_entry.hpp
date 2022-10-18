#ifndef CODELETS_ENTRY_HPP
#define CODELETS_ENTRY_HPP

#include "java/bytecodes.hpp"
#include "runtime/jvm_runtime.hpp"

class CodeletsEntry {
    public:
        enum Codelet {
            _illegal = -1,

            _slow_signature_handler = 0,
            _unimplemented_bytecode = 1,
            _illegal_bytecode_sequence = 2,

            _return = 3,
            _invoke_return = 4,
            _invokeinterface_return = 5,
            _invokedynamic_return = 6,

            _earlyret = 7,

            _result_handlers_for_native_calls = 8,
            _rethrow_exception = 9,
            _throw_exception = 10,
            _remove_activation_preserving_args = 11,
            _remove_activation = 12,
            _throw_ArrayIndexOutOfBoundsException = 13,
            _throw_ArrayStoreException = 14,
            _throw_ArithmeticException = 15,
            _throw_ClassCastException = 16,
            _throw_NullPointerException = 17,
            _throw_StackOverflowError = 18,

            _method_entry = 19,

            _deopt = 20,
            _deopt_reexecute_return = 21,

            _bytecode = 22,

            _jitcode_entry = 23,
            _jitcode_osr_entry = 24,
            _jitcode = 25,

        };

    private:
        static JVMRuntime::CodeletsInfo _entries;

    public:
        static void initialize(JVMRuntime::CodeletsInfo *entries);

        static Codelet entry_match(uint64_t ip, Bytecodes::Code &code);
};

#endif // CODELETS_ENTRY_HPP
