#ifndef TEMPLATES_HPP
#define TEMPLATES_HPP

#include "java/bytecodes.hpp"
#include "runtime/jvm_runtime.hpp"

class CodeletsEntry {
    public:
        enum Codelet {
            _illegal = -1,
            _unimplemented_bytecode = 0,
            _illegal_bytecode_sequence = 1,

            _return = 2,
            _invoke_return = 3,
            _invokeinterface_return = 4,
            _invokedynamic_return = 5,

            _result_handlers_for_native_calls = 6,
            _rethrow_exception = 7,
            _throw_exception = 8,
            _remove_activation_preserving_args = 9,
            _remove_activation = 10,
            _throw_ArrayIndexOutOfBoundsException = 11,
            _throw_ArrayStoreException = 12,
            _throw_ArithmeticException = 13,
            _throw_ClassCastException = 14,
            _throw_NullPointerException = 15,
            _throw_StackOverflowError = 16,

            _method_entry = 17,

            _deopt = 18,
            _deopt_reexecute_return = 19,

            _bytecode = 20,

            _jitcode_entry = 21,
            _jitcode_osr_entry = 22,
            _jitcode = 23,

        };

    private:
        static JVMRuntime::CodeletsInfo _entries;

        static int single_match(address *table, int size, address ip);
    public:
        static void initialize(JVMRuntime::CodeletsInfo *entries);

        static Codelet entry_match(address ip, Bytecodes::Code &code);
};

#endif
