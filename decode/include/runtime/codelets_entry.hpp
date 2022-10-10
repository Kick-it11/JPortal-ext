#ifndef TEMPLATES_HPP
#define TEMPLATES_HPP

#include "java/bytecodes.hpp"

const static int number_of_states = 10;
const static int number_of_return_entries = 6;
const static int number_of_return_addrs = 10;
const static int number_of_method_entries = 34;
const static int number_of_result_handlers = 10;
const static int number_of_deopt_entries = 7;
const static int dispatch_length = 256;
const static int number_of_codes = 239;

struct CodeletsInfo {
    // [low, high]
    address _low_bound;
    address _high_bound;

    // [error]
    address _unimplemented_bytecode_entry;
    address _illegal_bytecode_sequence_entry;

    // return
    address _return_entry[number_of_return_entries][number_of_states];
    address _invoke_return_entry[number_of_return_addrs];
    address _invokeinterface_return_entry[number_of_return_addrs];
    address _invokedynamic_return_entry[number_of_return_addrs];

    address _native_abi_to_tosca[number_of_result_handlers];

    // exception
    address _rethrow_exception_entry;
    address _throw_exception_entry;
    address _remove_activation_preserving_args_entry;
    address _remove_activation_entry;
    address _throw_ArrayIndexOutOfBoundsException_entry;
    address _throw_ArrayStoreException_entry;
    address _throw_ArithmeticException_entry;
    address _throw_ClassCastException_entry;
    address _throw_NullPointerException_entry;
    address _throw_StackOverflowError_entry;

    // method entry
    address _entry_table[number_of_method_entries];

    // bytecode template
    address _normal_table[dispatch_length][number_of_states];
    address _wentry_point[dispatch_length];

    // deoptimization
    address _deopt_entry[number_of_deopt_entries][number_of_states];
    address _deopt_reexecute_return_entry;

};

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
        static CodeletsInfo _entries;

        static int single_match(address *table, int size, address ip);
    public:
        static void initialize(CodeletsInfo *entries);
        
        static Codelet entry_match(address ip, Bytecodes::Code &code);
};

#endif
