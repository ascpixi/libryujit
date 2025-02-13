#pragma once

#include <vector>
#include <csetjmp>
#include <functional>

#include "./host.h"

typedef void* exc_jmp_buf[64];

// Represents a `catch` handler. `catch` handlers redirect the flow of the
// program, meaning they get jumped to instead of called.
//
// As `__builtin_setjmp` trashes all local variables of the target function
// when jumping, this also stores needed locals.
typedef struct
{
    // The `__builtin_setjmp` buffer.
    exc_jmp_buf* buf;

    // The actual `catch` function body.
    void (*body)(int, void*);

    // The captures of the handler.
    void* captures;
} exc_catch_handler_t;

// Represents a `finally` handler. `finally` handlers are called when the
// execution of a guarded block would usually be terminated because of an
// exception.
typedef std::function<void()> exc_finally_handler_t;

enum class exc_handler_type_t
{
    Catch,
    Finally
};

struct exc_handler_t
{
public:
    exc_handler_type_t type;

    exc_catch_handler_t   catch_handler;
    exc_finally_handler_t finally_handler;

    exc_handler_t(exc_catch_handler_t x)
    {
        type = exc_handler_type_t::Catch;

        catch_handler   = x;
        finally_handler = {};
    }

    exc_handler_t(exc_finally_handler_t x)
    {
        type = exc_handler_type_t::Finally;

        catch_handler   = {};
        finally_handler = x;
    }
};

// Represents a stack of guard blocks.
typedef std::vector<exc_handler_t, RyujitHostAllocator<exc_handler_t>> exc_stack_t;

// Provides a light-weight equivalent of a `try`-`catch` error trap.
void try_catch(void* capture, void (*block)(void*), void (*handler)(int, void*));

// Provides a light-weight equivalent of a `try`-`finally` error trap.
void try_finally(std::function<void()> block, std::function<void()> finally);

// Throws an exception which may be caught with `try_catch` or `try_finally`.
[[noreturn]] void exc_throw(int val);

#define TRY(capture_from, capture_as_type, capture_as_name)                                                            \
    {                                                                                                                  \
        auto _capture_from = capture_from;                                                                             \
        auto block         = [](void* __raw_capture) {                                  \
            capture_as_type capture_as_name = (capture_as_type)__raw_capture;

#define CATCH(errno_name, capture_as_type, capture_as_name)                                                            \
    }                                                                                                                  \
    ; try_catch(_capture_from, block, [](int errno_name, void* __raw_capture) {  \
        capture_as_type capture_as_name = (capture_as_type)__raw_capture;

#define END_CATCH                                                                                                      \
    });                                                                                                  \
    }

#define GUARD                                                                                                          \
    {                                                                                                                  \
        auto block = [&]()

#define FINALLY                                                                                                        \
    ; try_finally(block, [&]

#define END_FINALLY );                                                                                                 \
    }
