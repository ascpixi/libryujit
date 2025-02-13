#pragma once

#include <limits>
#include "./stdstreams.h"

/// These functions must be implemented by the consumers of the static library.

// Allocate memory of the given size in bytes.
//      Passed to: `ICorJitHost::allocateMemory`
extern "C" void* ryujit_host_alloc(size_t size);

// Frees memory previous obtained by a call to `ryujit_host_alloc`.
//      Passed to: `ICorJitHost::freeMemory`
extern "C" void ryujit_host_free(void* block);

// Returns an integer config value for the given key, if any exists.
//      Passed to: `ICorJitHost::getIntConfigValue`
extern "C" int ryujit_host_get_int_cfgval(
    const char* name,
    int defaultValue
);

// Return a string config value for the given key, if any exists.
//      Passed to: `ICorJitHost::getStringConfigValue`
extern "C" const char* ryujit_host_get_string_cfgval(const char* name);

// Free a string ConfigValue returned by the runtime. JITs using the getStringConfigValue
// query are required to return the string values to the runtime for deletion.
// This avoids leaking the memory in the JIT.
//      Passed to: `ICorJitHost::freeStringConfigValue`
extern "C" void ryujit_host_free_string_cfgval(const char* value);

// Allocate memory slab of the given size in bytes. The host is expected to pool
// these for good performance.
//      Passed to: `ICorJitHost::allocateSlab`
extern "C" void* ryujit_host_alloc_slab(size_t size, size_t* pActualSize);

// Free memory slab of the given size in bytes.
//      Passed to: `ICorJitHost::freeSlab`
extern "C" void ryujit_host_free_slab(void* slab, size_t actualSize);

// Sets the per-thread pointer dedicated for RyuJIT. If only one thread is allowed
// to use RyuJIT, this may be a simple getter and setter to a global variable.
extern "C" void ryujit_host_set_tls(void* ptr);

// Gets the previously set per-thread pointer.
extern "C" void* ryujit_host_get_tls();

// Invoked when an unrecovable internal error occurs. The `msg` parameter
// describes the cause of the error.
extern "C" [[noreturn]] void ryujit_host_panic(const char* msg);

// Gets a handle to the standard output stream of the JIT.
extern "C" FILE* ryujit_host_get_stdout();

// Writes the given null-terminated buffer of bytes to a stream. The `stream`
// parameter is always obtained via other `ryujit_host_` functions.
extern "C" void ryujit_host_write(FILE* stream, const char* buffer);

// Similar to "strtod", but writes errno into the "errno" parameter.
extern "C" double strtod_errno(const char* __restrict __nptr, char** __restrict __endptr, int* __errno);

// Returns an opaque handle that represents a critical section object.
extern "C" void* ryujit_host_create_lock();

// Frees the resources associated with a previously allocated critical section
// object (taken from "ryujit_host_create_lock").
extern "C" void ryujit_host_delete_lock(void* handle);

// Enters a critical section, identified by the given opaque handle.
extern "C" void ryujit_host_enter_lock(void* handle);

// Exits a critical section, identified by the given opaque handle.
extern "C" void ryujit_host_exit_lock(void* handle);

// Represents an allocator, where all allocation requests are passed through
// `ryujit_host_alloc` and `ryujit_host_free`.
template <typename T>
class RyujitHostAllocator {
public:
    using value_type = T;
    
    RyujitHostAllocator() noexcept {}
    
    template <typename U>
    RyujitHostAllocator(const RyujitHostAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            ryujit_host_panic("attempted to allocate an invalid amount of bytes");
            return nullptr;
        }
            
        if (void* ptr = ryujit_host_alloc(n * sizeof(T)))
            return static_cast<T*>(ptr);
            
        ryujit_host_panic("failed to allocate host memory");
        return nullptr;
    }

    void deallocate(T* p, size_t) noexcept {
        ryujit_host_free(p);
    }
};
