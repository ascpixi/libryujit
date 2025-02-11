# libryujit
`libryujit` is an experiment to create a static version if `libclrjit` which may be easily embedded inside various environments, e.g. kernels.

The goals of this experiment are:
- remove all dependencies on user-mode libraries like glibc,
- expose a simpler C interop layer,
- document lesser-known internals of RyuJIT.

The project is a fork of `dotnet/runtime`, and tries to make as little changes to the existing codebase as possible to simplify keeping track with upstream. Instead of modifying existing .NET code in large capacities (that go beyond simple patching), we try to build on top of it.

In order to build `libclrjit.a`, run `"./src/coreclr/build-runtime.sh" <arch> <configuration> -os <OS> -outputrid <RID> -component jit`.

For example:
```sh
"./src/coreclr/build-runtime.sh" -x64 -debug -os linux -outputrid linux-x64 -component jit`.
```
