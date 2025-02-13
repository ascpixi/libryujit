# libryujit
`libryujit` is an experiment to create a static version if `libclrjit` which may be easily embedded inside various environments, e.g. kernels.

The goals of this experiment are:
- remove all dependencies on user-mode libraries like glibc,
- expose a simpler C interop layer,
- document lesser-known internals of RyuJIT.

The project is a fork of `dotnet/runtime`, and tries to make as little changes to the existing codebase as possible to simplify keeping track with upstream. Instead of modifying existing .NET code in large capacities (that go beyond simple patching), we try to build on top of it.

In order to build `libclrjit.a`, run `"./src/coreclr/build-runtime.sh" <arch> <configuration> -os <OS> -component jit`.

For example:
```sh
"./src/coreclr/build-runtime.sh" -x64 -debug -os linux -component jit
```

The `-os` argument mostly affects the ABI used - the generated code usually does not rely on OS-provided functionality. For example, using `linux` will result in a System V-compatible library.

> [!IMPORTANT]
> Compiling for Windows hasn't been tested and is not officially supported.

## Required functions
The library interfaces with its host via functions prefixed with `ryujit_host`. These functions can be found in [`/src/coreclr/jit/libryujit/host.h`](https://github.com/ascpixi/libryujit/blob/main/src/coreclr/jit/libryujit/host.h), and include:

- `void* ryujit_host_alloc(size_t size)`
    - Allocate memory of the given size in bytes.
- `void ryujit_host_free(void* block)`
    - Frees memory previous obtained by a call to `ryujit_host_alloc`.
- `int ryujit_host_get_int_cfgval(const char* name, int defaultValue)`
    - Returns an integer config value for the given key, if any exists.
    - Common key names for JIT configuration values can be found in [`jitconfigvalues.h`](https://github.com/ascpixi/libryujit/blob/main/src/coreclr/jit/jitconfigvalues.h). These are usually constant.
- `const char* ryujit_host_get_string_cfgval(const char* name)`
    - Return a string config value for the given key, if any exists.
    - The returned string may be freed by the JIT.
- `void ryujit_host_free_string_cfgval(const char* value)`
    - Free a string ConfigValue returned by the runtime. JITs using the getStringConfigValue query are required to return the string values to the runtime for deletion. This avoids leaking the memory in the JIT.
- `void* ryujit_host_alloc_slab(size_t size, size_t* pActualSize)`
    - Allocate memory slab of the given size in bytes.
    - The host is expected to pool these for good performance.
- `void ryujit_host_free_slab(void* slab, size_t actualSize)`
    - Free memory slab of the given size in bytes.
- `void ryujit_host_set_tls(void* ptr)`
    - Sets the per-thread pointer dedicated for RyuJIT. If only one thread is allowed to use RyuJIT, this may be a direct setter to a global variable. Otherwise, if the language of the host supports it, this may be a setter to a TLS global variable.
- `void* ryujit_host_get_tls()`
    - Gets the previously set per-thread pointer.
- `void ryujit_host_panic(const char* msg);`
    - Invoked when an unrecovable internal error occurs. The `msg` parameter describes the cause of the error.
- `FILE* ryujit_host_get_stdout();`
    - Gets an opaque handle to the standard output stream of the JIT. The meaning of the returned value only matters to the host.
    - Diagnostic messages are written to the stream identified by the return value of this function.
- `void ryujit_host_write(FILE* stream, const char* buffer);`
    - Writes the given null-terminated buffer of bytes to a stream. The `stream` parameter is always obtained via other `ryujit_host_` functions.
    - Currently, the `stream` parameter is guaranteed to always be equal to the return value of `ryujit_host_get_stdout`.

Depending on the specific compiler used, calls to the following functions may be emitted:
- `__stack_chk_fail`

Additionally, the following standard C functions should also be defined:
- `acos`
- `acosf`
- `acosh`
- `acoshf`
- `asin`
- `asinf`
- `asinh`
- `asinhf`
- `atan`
- `atan2`
- `atan2f`
- `atanf`
- `atanh`
- `atanhf`
- `atoi`
- `cbrt`
- `cbrtf`
- `ceil`
- `ceilf`
- `cos`
- `cosf`
- `cosh`
- `coshf`
- `exp`
- `expf`
- `floor`
- `floorf`
- `fmod`
- `ilogbf`
- `log`
- `log10`
- `log10f`
- `log2`
- `log2f`
- `logf`
- `memchr`
- `memcmp`
- `memcpy`
- `memcpy_s`
- `memmove`
- `memmove_s`
- `memset`
- `pow`
- `powf`
- `sin`
- `sinf`
- `sinh`
- `sprintf_s`
- `sqrt`
- `sqrtf`
- `strcasecmp`
- `strcat`
- `strcmp`
- `strlen`
- `strncmp`
- `strnlen`
- `strrchr`
- `strstr`
- `strtod`
- `strtod_errno` (functionally equivalent to `strtod`, but writes errno to the specified variable; `const char*, char**, int*`, where the last parameter is a pointer to where to write the errno code to)
- `tan`
- `tanf`
- `tanh`
- `tanhf`
- `trunc`
- `truncf`

None of the functions are expected to set `errno`, even if they normally would do so - if what would usually be the `errno` value is required, it will usually be provided via a `_errno`-suffixed version.

When consuming the library from a non-C++ environment, the following functions must also be defined:
- `__cxa_pure_virtual`
- `std::__throw_length_error(char const*)` (mangled: `_ZSt20__throw_length_errorPKc`)
- `std::__throw_bad_function_call()` (mangled: `_ZSt25__throw_bad_function_callv`)
- `operator delete[](void*)` (mangled: `_ZdaPv`)
- `operator delete(void*)` (mangled: `_ZdlPv`)
- `operator new[](unsigned long)` (mangled: `_Znam`)
- `operator new(unsigned long)` (mangled: `_Znwm`)
