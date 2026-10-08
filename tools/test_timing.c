/* Native callback tests: baseline identity, one-sided widening, signed offsets,
 * cell boundaries, stock wide windows, and address/tempo guard rejection. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/parappa_timing.c"

uint64_t psx_cycle_count;
int psx_mod_option_value(const char *p, const char *f, const char *o, char *out, uint32_t size) {
    (void)p; (void)f; (void)o; (void)out; (void)size; return 0;
}
void psx_mod_counter_add(const char *name, uint32_t delta) { (void)name; (void)delta; }
int psx_mod_register_activation_plugin(const char *id, PSXModActivationCallback cb) { (void)id; (void)cb; return 1; }
int psx_mod_register_savestate_plugin(const char *id, PSXModActivationCallback cb) { (void)id; (void)cb; return 1; }
int psx_mod_register_function_entry_plugin(const char *id, uint32_t a, PSXModFunctionEntryCallback cb) { (void)id; (void)a; (void)cb; return 1; }
int psx_mod_register_instruction_plugin(const char *id, uint32_t a, uint32_t w, PSXModFunctionEntryCallback cb) { (void)id; (void)a; (void)w; (void)cb; return 1; }

static uint32_t tempo = 10560;
static int replay;
static uint32_t read_test(uint32_t address) {
    if (address == 0x8006edb8U) return 0x800548b8U;
    if (address == 0x80054a14U) return tempo;
    if (address == 0x801c3690U) return replay ? 0x10000U : 0;
    return 0;
}

static int judge(int tick, int half, int early, int late, int offset, int *slot) {
    CPUState cpu = {0};
    cpu.read_word = read_test;
    cpu.gpr[4] = 0x801c3640U;
    entry(&cpu, 0x80014614U);
    early_ms = early; late_ms = late; offset_ms = offset;
    cpu.gpr[3] = (uint32_t)tick;
    cpu.gpr[5] = (uint32_t)half;
    shift(&cpu, 0x80014718U);
    assert(pending.converted);
    int biased = (int32_t)cpu.gpr[3] + (int32_t)cpu.gpr[5];
    /* Mirror the retail MIPS division's truncation toward zero. */
    int in_bar = biased % 384;
    int cell = in_bar / 24;
    int remainder = in_bar - cell * 24;
    cpu.gpr[2] = (uint32_t)remainder;
    cpu.gpr[3] = (uint32_t)cell;
    window(&cpu, 0x8001476cU);
    *slot = cell;
    return remainder <= (int32_t)cpu.gpr[5];
}

int main(void) {
    int slot, expected_slot;
    for (int half = 0; half <= 24; ++half) {
        for (int tick = 0; tick < 4096; ++tick) {
            int biased = (tick + half) % 384;
            expected_slot = biased / 24;
            int expected = biased % 24 <= 2 * half;
            assert(judge(tick, half, 0, 0, 0, &slot) == expected);
            assert(slot == expected_slot);
        }
    }
    assert(!judge(24 - 9, 8, 0, 0, 0, &slot));
    assert(judge(24 - 9, 8, 10, 0, 0, &slot) && slot == 1);
    assert(!judge(24 + 9, 8, 0, 0, 0, &slot));
    assert(judge(24 + 9, 8, 0, 10, 0, &slot) && slot == 1);
    assert(!judge(24 - 9, 8, 0, 10, 0, &slot));
    assert(!judge(24 + 9, 8, 10, 0, 0, &slot));
    assert(parappa_ms_to_ticks(100, 10560) == 18);
    assert(parappa_ms_to_ticks(-100, 10560) == -18);
    assert(judge(24 + 18, 8, 0, 0, 100, &slot) && slot == 1);
    assert(judge(24 - 18, 8, 0, 0, -100, &slot) && slot == 1);
    assert(parappa_extra_ticks(60, 10560, 8) == 4);
    assert(parappa_extra_ticks(60, 10560, 24) == 0);
    CPUState cpu = {0}; cpu.read_word = read_test;
    cpu.gpr[4] = 0x80000000U; entry(&cpu, 0); assert(!pending.active);
    replay = 1; cpu.gpr[4] = 0x801c3640U; entry(&cpu, 0); assert(!pending.active);
    replay = 0;
    cpu.gpr[4] = 0x801c3640U; entry(&cpu, 0);
    cpu.gpr[3] = 123; cpu.gpr[5] = 8; tempo = 0;
    shift(&cpu, 0); assert(!pending.active && cpu.gpr[3] == 123 && cpu.gpr[5] == 8);
    tempo = 10560; offset_ms = 300; early_ms = late_ms = 10;
    entry(&cpu, 0); cpu.gpr[3] = 0; cpu.gpr[5] = 8;
    shift(&cpu, 0); assert(!pending.active && !pending.converted);
    assert(cpu.gpr[3] == 0 && cpu.gpr[5] == 8);
    offset_ms = 0; entry(&cpu, 0); assert(pending.active);
    restored(); assert(!pending.active);
    puts("PASS: 102400 baseline cases, one-sided boundaries, signed compensation, caps and guards");
}
