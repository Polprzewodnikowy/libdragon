#include <stdbool.h>
#include "debug.h"
#include "minidragon.h"

register uint32_t ipl2_tvType asm ("s4");

static volatile uint32_t *vi_regs = (uint32_t *) (0xA4400000);

static const uint32_t vi_regs_p[3][7] = {
    { /* PAL */   0x0404233a, 0x00000271, 0x00150c69, 0x0c6f0c6e, 0x00800300, 0x005f0239, 0x0009026b },
    { /* NTSC */  0x03e52239, 0x0000020d, 0x00000c15, 0x0c150c15, 0x006c02ec, 0x002501ff, 0x000e0204 },
    { /* MPAL */  0x04651e39, 0x0000020d, 0x00040c11, 0x0c190c1a, 0x006c02ec, 0x002501ff, 0x000e0204 },
};

#define TEST_WINDOW_SIZE (8 * 1024)

__attribute__((section(".text.memtest"), noreturn))
void memtest(int memsize) {
    si_write(0x7FC, 0x8);

    debugf("Memtest start");

    vi_regs[1] = 0 & (~(64 - 1));
    vi_regs[2] = 640;
    vi_regs[12] = 0x400;
    vi_regs[13] = 0x400;
    #pragma GCC unroll 0
    for (int reg = 0; reg < 7; reg++) {
        vi_regs[reg+5] = vi_regs_p[ipl2_tvType][reg];
    }
    vi_regs[0] = 0x3202;

    void *RDRAM = (void *) (0x80000000);

    uint64_t *start = (uint64_t *) (RDRAM + 0x1000);
    uint64_t *end = (uint64_t *) (RDRAM + memsize);

    uint64_t *ptr;

    debugf("Initial zero test");
    cop0_clear_dcache();
    ptr = start;
    while (ptr < end) {
        uint64_t c = *ptr;
        if (c != 0) {
            debugf("Memtest initial zero error address/value/expected ", ptr, c, 0);
        }
        ptr++;
    }

    uint64_t *sp_payload = (uint64_t *) (RDRAM);
    for (int i = 0; i < 0x1000; i += sizeof(*sp_payload)) {
        *sp_payload++ = 0xFFFFFFFFFFFFFFFF;
    }
    data_cache_hit_writeback_invalidate(RDRAM, 0x1000);

    uint32_t offset = 0;

    while (true) {
        if (offset % ((4 * 1024) / sizeof(*ptr)) == 0) {
            debugf("Testing memory at: ", start + offset);
        }

        uint64_t pattern = 0;

        if (offset % 2) {
            pattern = ~pattern;
        }

        ptr = start + offset;
        while (ptr < (start + offset + (TEST_WINDOW_SIZE / sizeof(*ptr)))) {
            *ptr++ = pattern;
        }

        data_cache_hit_writeback_invalidate(start + offset, TEST_WINDOW_SIZE);

        *SP_DRAM_ADDR = 0;
        *SP_RSP_ADDR = 0x1000;
        *SP_RD_LEN = 0x1000 - 1;
        while (*SP_STATUS & ((1 << 4) | (1 << 3) | (1 << 2)));

        ptr = start + offset;
        while (ptr < (start + offset + (TEST_WINDOW_SIZE / sizeof(*ptr)))) {
            uint64_t c = *ptr;
            if (c != pattern) {
                uint32_t c_hi = (c >> 32) & 0xFFFFFFFF;
                uint32_t c_lo = c & 0xFFFFFFFF;
                uint32_t p_hi = (pattern >> 32) & 0xFFFFFFFF;
                uint32_t p_lo = pattern & 0xFFFFFFFF;
                debugf("Memtest error address/value(h,l)/expected(h,l) ", ptr, c_hi, c_lo, p_hi, p_lo);
            }
            ptr++;
        }

        offset += 1;
        if (start + offset + (TEST_WINDOW_SIZE / sizeof(*ptr)) > end) {
            offset = 0;
        }
    }
}
