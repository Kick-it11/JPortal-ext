#ifndef CODELETS_ENTRY_HPP
#define CODELETS_ENTRY_HPP

#include "structure/java/bytecodes.hpp"

#include <stdint.h>

#define address uint64_t

class CodeletsEntry {
    friend class JvmDumpDecoder;
    public:
        const static int number_of_states = 10;
        const static int number_of_return_entries = 6;
        const static int number_of_return_addrs = 10;
        const static int number_of_method_entries = 34;
        const static int number_of_result_handlers = 10;
        const static int number_of_deopt_entries = 7;
        const static int dispatch_length = 256;
        const static int number_of_codes = 239;

        enum Codelet {
            _illegal = -1,
            _unimplemented_bytecode_entry_points,
            _illegal_bytecode_sequence_entry_points,
            _return_entry_points,
            _invoke_return_entry_points,
            _invokeinterface_return_entry_points,
            _invokedynamic_return_entry_points,
            _result_handlers_for_native_calls,
            _rethrow_exception_entry_entry_points,
            _throw_exception_entry_points,
            _remove_activation_preserving_args_entry_points,
            _remove_activation_entry_points,
            _throw_ArrayIndexOutOfBoundsException_entry_points,
            _throw_ArrayStoreException_entry_points,
            _throw_ArithmeticException_entry_points,
            _throw_ClassCastException_entry_points,
            _throw_NullPointerException_entry_points,
            _throw_StackOverflowError_entry_points,
            _method_entry_points,
            _bytecode,
            _deopt_entry_points,
            _deopt_reexecute_return_entry_points
        };

    private:
        static address low_bound;
        static address high_bound;
        static address unimplemented_bytecode;
        static address illegal_bytecode_sequence;
        static address return_entry[number_of_return_entries][number_of_states];
        static address invoke_return_entry[number_of_return_addrs];
        static address invokeinterface_return_entry[number_of_return_addrs];
        static address invokedynamic_return_entry[number_of_return_addrs];
        static address native_abi_to_tosca[number_of_result_handlers];
        static address rethrow_exception_entry;
        static address throw_exception_entry;
        static address remove_activation_preserving_args_entry;
        static address remove_activation_entry;
        static address throw_ArrayIndexOutOfBoundsException_entry;
        static address throw_ArrayStoreException_entry;
        static address throw_ArithmeticException_entry;
        static address throw_ClassCastException_entry;
        static address throw_NullPointerException_entry;
        static address throw_StackOverflowError_entry;
        static address entry_table[number_of_method_entries];
        static address normal_table[dispatch_length][number_of_states];
        static address wentry_point[dispatch_length];
        static address deopt_entry[number_of_deopt_entries][number_of_states];
        static address deopt_reexecute_return_entry;

    public:
        /* match an instruction pointer address to a codelet
         * return the type(in enum Type{})
         * and if the type is bytecode(_branch_bytecode, ...)
         * argument bytecode saves the bytecode */
        static Codelet entry_match(address ip, Bytecodes::Code &code);
};

#endif