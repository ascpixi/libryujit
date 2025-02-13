#include <csetjmp>
#include <functional>

#include "./errorhandling.h"
#include "./tls.h"

#if TARGET_ARM64

// If we're on arm64, things get a bit more complicated, since __builtin_longjmp and
// __builtin_setjmp are not supported.

int ryujit_setjmp(void** buf) {
  __asm__ volatile(
    // Save the current stack pointer into buf[0]
    "mov    x2, sp\n"
    "str    x2, [x0]\n"
    // Save x19 and x20 into buf[1] and buf[2]
    "stp    x19, x20, [x0, #8]\n"
    // Save x21 and x22 into buf[3] and buf[4]
    "stp    x21, x22, [x0, #24]\n"
    // Save x23 and x24 into buf[5] and buf[6]
    "stp    x23, x24, [x0, #40]\n"
    // Save x25 and x26 into buf[7] and buf[8]
    "stp    x25, x26, [x0, #56]\n"
    // Save x27 and x28 into buf[9] and buf[10]
    "stp    x27, x28, [x0, #72]\n"
    // Save x29 (fp) and x30 (lr) into buf[11] and buf[12]
    "stp    x29, x30, [x0, #88]\n"
    // Return 0. (w0 holds the lower 32 bits of x0.)
    "mov    w0, #0\n"
    "ret\n"
  );

  __builtin_unreachable();
}

// __builtin_longjmp restores the environment saved in buf and jumps to it.
// If val is 0, it is replaced by 1.
__attribute__((naked, noreturn))
void ryujit_longjmp(void** buf, int val) {
  __asm__ volatile(
    // If w1 (the passed-in 'val') is nonzero, do nothing; if zero, set it to 1.
    "cbnz   w1, 1f\n"
    "mov    w1, #1\n"
    "1:\n"
    // Restore x29 (fp) and x30 (lr) from buf[11] and buf[12]
    "ldp    x29, x30, [x0, #88]\n"
    // Restore x27 and x28 from buf[9] and buf[10]
    "ldp    x27, x28, [x0, #72]\n"
    // Restore x25 and x26 from buf[7] and buf[8]
    "ldp    x25, x26, [x0, #56]\n"
    // Restore x23 and x24 from buf[5] and buf[6]
    "ldp    x23, x24, [x0, #40]\n"
    // Restore x21 and x22 from buf[3] and buf[4]
    "ldp    x21, x22, [x0, #24]\n"
    // Restore x19 and x20 from buf[1] and buf[2]
    "ldp    x19, x20, [x0, #8]\n"
    // Restore the stack pointer from buf[0]
    "ldr    x2, [x0]\n"
    "mov    sp, x2\n"
    // Set the return value (for the resumed setjmp) in x0 to the (possibly adjusted) val.
    "mov    x0, x1\n"
    // Jump to the saved return address.
    "br     x30\n"
  );
}

#define LONGJMP ryujit_longjmp
#define SETJMP ryujit_setjmp

#else

#define LONGJMP __builtin_longjmp
#define SETJMP __builtin_setjmp

#endif

void try_finally(std::function<void()> block, std::function<void()> finally_handler)
{
    ryujit_get_tls()->exc_stack.push_back(exc_handler_t(finally_handler));
    block();

    // If an exception occurs, we shouldn't reach this point.
    ryujit_get_tls()->exc_stack.pop_back();
}

void try_catch(void* capture, void (*block)(void*), void (*handler)(int, void*))
{
    exc_jmp_buf jump = {};
    if (SETJMP(jump) > 0)
    {
        auto tls = ryujit_get_tls();
        tls->exc_catch_handler(tls->exc_val, tls->exc_catch_captures);
        return;
    }

    ryujit_get_tls()->exc_stack.push_back(exc_handler_t(exc_catch_handler_t{&jump, handler, capture}));

    block(capture);

    ryujit_get_tls()->exc_stack.pop_back();
}

[[noreturn]] void exc_throw(int val)
{
    auto tls     = ryujit_get_tls();
    tls->exc_val = val;

    while (!tls->exc_stack.empty())
    {
        exc_handler_t handler = tls->exc_stack[tls->exc_stack.size() - 1];
        tls->exc_stack.pop_back();

        if (handler.type == exc_handler_type_t::Catch)
        {
            tls->exc_catch_handler  = handler.catch_handler.body;
            tls->exc_catch_captures = handler.catch_handler.captures;
            LONGJMP(*handler.catch_handler.buf, 1);

            // We shouldn't really reach this point...
        }
        else
        {
            auto& finally_handler = handler.finally_handler;
            if (!finally_handler)
            {
                ryujit_host_panic("'finally' block handler became empty");
            }

            finally_handler();
        }
    }

    // No (catch) handler is set-up! This is an unhandled exception!
    ryujit_host_panic("unhandled exception");
}
