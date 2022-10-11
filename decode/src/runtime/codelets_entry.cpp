#include "runtime/codelets_entry.hpp"

#include <cassert>

JVMRuntime::CodeletsInfo CodeletsEntry::_entries;

void CodeletsEntry::initialize(JVMRuntime::CodeletsInfo *entries) {
    _entries = *entries;
}

CodeletsEntry::Codelet CodeletsEntry::entry_match(address ip, Bytecodes::Code &code) {
    if (ip < _entries._low_bound || ip >= _entries._high_bound)
        return _illegal;

    if (ip >= _entries._normal_table[0][0] && ip < _entries._wentry_point[0]) {
        int low = 0, high = JVMRuntime::dispatch_length*JVMRuntime::number_of_states-1;
        while (low <= high) {
            int mid = (low+high)/2;
            address addr = _entries._normal_table[mid/JVMRuntime::number_of_states][mid%JVMRuntime::number_of_states];
            if (addr == ip) {
                code = Bytecodes::cast(mid/JVMRuntime::number_of_states);
                return _bytecode;
            } else if (addr > ip) {
                high = mid-1;
            } else {
                low = mid+1;
            }
        }
        return _illegal;
    }

    if (ip >= _entries._wentry_point[0] && ip < _entries._deopt_entry[0][0]) {
        int low = 0, high = JVMRuntime::dispatch_length-1;
        while (low <= high) {
            int mid = (low+high)/2;
            address addr = _entries._wentry_point[mid];
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

    if (ip < _entries._unimplemented_bytecode_entry)
        return _illegal;

    if (ip == _entries._unimplemented_bytecode_entry)
        return _unimplemented_bytecode;

    if (ip == _entries._illegal_bytecode_sequence_entry)
        return _illegal_bytecode_sequence;

    if (ip >= _entries._return_entry[0][0] && ip < _entries._invoke_return_entry[0])
        return _return;

    if (ip >= _entries._invoke_return_entry[0] && ip < _entries._invokeinterface_return_entry[0])
        return _invoke_return;

    if (ip >= _entries._invokeinterface_return_entry[0] && ip < _entries._invokedynamic_return_entry[0])
        return _invokeinterface_return;

    if (ip >= _entries._invokedynamic_return_entry[0] && ip < _entries._native_abi_to_tosca[0])
        return _invokedynamic_return;

    if (ip >= _entries._native_abi_to_tosca[0] && ip < _entries._rethrow_exception_entry)
        return _result_handlers_for_native_calls;

    if (ip >= _entries._entry_table[0] && ip < _entries._normal_table[0][0])
        return _method_entry;

    if (ip >= _entries._deopt_entry[0][0] && ip < _entries._deopt_reexecute_return_entry)
        return _deopt;

    if (ip == _entries._rethrow_exception_entry)
        return _rethrow_exception;

    if (ip == _entries._throw_exception_entry)
        return _throw_exception;

    if (ip == _entries._remove_activation_preserving_args_entry)
        return _remove_activation_preserving_args;

    if (ip == _entries._remove_activation_entry)
        return _remove_activation;

    if (ip == _entries._throw_ArrayIndexOutOfBoundsException_entry)
        return _throw_ArrayIndexOutOfBoundsException;

    if (ip == _entries._throw_ArrayStoreException_entry)
        return _throw_ArrayStoreException;

    if (ip == _entries._throw_ArithmeticException_entry)
        return _throw_ArithmeticException;

    if (ip == _entries._throw_ClassCastException_entry)
        return _throw_ClassCastException;

    if (ip == _entries._throw_NullPointerException_entry)
        return _throw_NullPointerException;

    if (ip == _entries._throw_StackOverflowError_entry)
        return _throw_StackOverflowError;

    if (ip == _entries._deopt_reexecute_return_entry)
        return _deopt_reexecute_return;

    return _illegal;
}
