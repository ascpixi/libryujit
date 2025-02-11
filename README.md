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
"./src/coreclr/build-runtime.sh" -x64 -debug -os linux -component jit`.
```

The `-os` argument mostly affects the ABI used - the generated code usually does not rely on OS-provided functionality. For example, using `linux` will result in a System V-compatible library.

> [!NOTE]
> Compiling for Windows hasn't been tested and is not officially supported.

## Required functions
> [!WARNING]
> This section is a work in progress, and does not contain all required functions. You can view the symbols the library requires consumers to define via `comm -23 <(nm -g --undefined-only -A libclrjit.a | awk '{print $NF}' | sort -u) <(nm -g --defined-only  -A libclrjit.a | awk '{print $NF}' | sort -u)`.


The library expects consumers to define the following functions:
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
- `strtod_errno` (`const char*, char**, int*`, where the last parameter is a pointer to where to write the errno code to)
- `tan`
- `tanf`
- `tanh`
- `tanhf`
- `trunc`
- `truncf`

None of the functions are expected to set `errno`, even if they normally would do so - if what would usually be the `errno` value is required, it will usually be provided via a `_errno`-suffixed version.

If consuming the library from a non-C++ environment, these functions must be defined:
- `__cxa_pure_virtual`
- `__cxa_guard_acquire`
- `__cxa_guard_release`
