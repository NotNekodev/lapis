#include "syscall.h"

#include <log/log.h>
#include <arch/cpu.h>

void setup_syscall(void) {
    uint32_t eax, ebx, ecx, edx;

    _cpuid(0x80000000, 0, eax, ebx, ecx, edx);

    if (eax >= 0x80000001) {
        _cpuid(0x80000001, 0, eax, ebx, ecx, edx);

        if (edx & (1u << 11)) {
            // TODO: fix context switch bug from july 2026 and implement sysret correctly
            /*
            debug("syscall/sysret supported! registering syscall handler\n");

            uint64_t efer = _rdmsr(0xC0000080);
            efer |= 1;              // SCE
            _wrmsr(0xC0000080, efer);

            // write LSTAR (handler address)
            _wrmsr(0xC0000082, (uint64_t)sysret_syscall_handler);

            // write STAR (syscall segment selectors)
            uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
            _wrmsr(0xC0000081, star);

            // write FMASK
            _wrmsr(0xC0000084, (1 << 9)); // clear IF
            */
        } else {
            warn("syscall/sysret not cpuid subleaf not available!\n");
        }
    } else {
        warn("syscall/sysret not supported!\n");
    }

    info("registered syscall handlers!\n");
}

void generic_syscall_handler(syscall_frame_t *frame) {
    debug("syscall/sysret handler called on cpu %d\n", get_current_cpuid());
    debug("rax: %lx, rdi: %lx, rsi: %lx, rdx: %lx, r10: %lx, r8: %lx, r9: %lx\n",
        frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);
}
