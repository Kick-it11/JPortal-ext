#include "structure/PT/codelets_entry.hpp"

#include <cstdio>

using namespace std;

address CodeletsEntry::low_bound;
address CodeletsEntry::high_bound;
address CodeletsEntry::unimplemented_bytecode;
address CodeletsEntry::illegal_bytecode_sequence;
address CodeletsEntry::return_entry[6][10];
address CodeletsEntry::invoke_return_entry[10];
address CodeletsEntry::invokeinterface_return_entry[10];
address CodeletsEntry::invokedynamic_return_entry[10];
address CodeletsEntry::native_abi_to_tosca[10];
address CodeletsEntry::rethrow_exception_entry;
address CodeletsEntry::throw_exception_entry;
address CodeletsEntry::remove_activation_preserving_args_entry;
address CodeletsEntry::remove_activation_entry;
address CodeletsEntry::throw_ArrayIndexOutOfBoundsException_entry;
address CodeletsEntry::throw_ArrayStoreException_entry;
address CodeletsEntry::throw_ArithmeticException_entry;
address CodeletsEntry::throw_ClassCastException_entry;
address CodeletsEntry::throw_NullPointerException_entry;
address CodeletsEntry::throw_StackOverflowError_entry;
address CodeletsEntry::entry_table[34];
address CodeletsEntry::normal_table[256][10];
address CodeletsEntry::wentry_point[256];
address CodeletsEntry::deopt_entry[7][10];
address CodeletsEntry::deopt_reexecute_return_entry;

CodeletsEntry::Codelet CodeletsEntry::entry_match(address ip, Bytecodes::Code &code) {
    if (ip < low_bound || ip >= high_bound)
        return _illegal;

    if (ip >= normal_table[0][0] && ip < wentry_point[0]) {
        int low = 0, high = dispatch_length*number_of_states-1;
        while (low <= high) {
            int mid = (low+high)/2;
            address addr = normal_table[mid/number_of_states][mid%number_of_states];
            if (addr == ip) {
                code = Bytecodes::cast(mid/number_of_states);
                return _bytecode;
            } else if (addr > ip) {
                high = mid-1;
            } else {
                low = mid+1;
            }
        }
        return _illegal;
    }

    if (ip >= wentry_point[0] && ip < deopt_entry[0][0]) {
        int low = 0, high = dispatch_length-1;
        while (low <= high) {
            int mid = (low+high)/2;
            address addr = wentry_point[mid];
            if (addr == ip) {
                code = Bytecodes::cast(mid);
                return _bytecode;
            } else if (addr > ip) {
                high = mid-1;
            } else {
                low = mid+1;
            }
        }
        return _illegal;
    }

    if (ip < unimplemented_bytecode)
        return _illegal;
    else if (ip < illegal_bytecode_sequence)
        return _unimplemented_bytecode_entry_points;
    else if (ip < return_entry[0][0])
        return _illegal_bytecode_sequence_entry_points;
    else if (ip < invoke_return_entry[0])
        return _return_entry_points;
    else if (ip < invokeinterface_return_entry[0])
        return _invoke_return_entry_points;
    else if (ip < invokedynamic_return_entry[0])
        return _invokeinterface_return_entry_points;
    else if (ip < native_abi_to_tosca[0])
        return _invokedynamic_return_entry_points;
    else if (ip < rethrow_exception_entry)
        return _result_handlers_for_native_calls;
    else if (ip < throw_exception_entry)
        return _rethrow_exception_entry_entry_points;
    else if (ip < remove_activation_preserving_args_entry)
        return _throw_exception_entry_points;
    else if (ip < remove_activation_entry)
        return _remove_activation_preserving_args_entry_points;
    else if (ip < throw_ArrayIndexOutOfBoundsException_entry)
        return _remove_activation_entry_points;
    else if (ip < throw_ArrayStoreException_entry)
        return _throw_ArrayIndexOutOfBoundsException_entry_points;
    else if (ip < throw_ArithmeticException_entry)
        return _throw_ArrayStoreException_entry_points;
    else if (ip < throw_ClassCastException_entry)
        return _throw_ArithmeticException_entry_points;
    else if (ip < throw_NullPointerException_entry)
        return _throw_ClassCastException_entry_points;
    else if (ip < throw_StackOverflowError_entry)
        return _throw_NullPointerException_entry_points;
    else if (ip < entry_table[0])
        return _throw_StackOverflowError_entry_points;
    else if (ip < normal_table[0][0])
        return _method_entry_points;
    else if (ip < deopt_reexecute_return_entry)
        return _deopt_entry_points;
    else if (ip == deopt_reexecute_return_entry)
        return _deopt_reexecute_return_entry_points;

    return _illegal;
}
