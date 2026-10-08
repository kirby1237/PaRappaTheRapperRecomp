/* SCUS-94183 judgement assist for kirby1237/PaRappaTheRapperRecomp.
 * No ROM bytes or generated C are copied into this implementation.
 */
#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_cycles.h"
#include "parappa_timing_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKAGE "parappa.accessibility.timing"
#define FEATURE "judgement"
#define PLUGIN "parappa.timing"

static int offset_ms, early_ms, late_ms;
static FILE *trace_file;
static struct {
    int active, converted, stock_half, early, late, offset, phase, slot, gate;
    uint32_t original_tick, tempo, buttons;
    uint64_t cycle;
} pending;

static int option(const char *id, int fallback, int min, int max) {
    char value[32], *end;
    if (!psx_mod_option_value(PACKAGE, FEATURE, id, value, sizeof value))
        return fallback;
    long result = strtol(value, &end, 10);
    if (end == value || *end || result < min || result > max) return fallback;
    return (int)result;
}

static void activate(void) {
    offset_ms = option("offset_ms", 0, -300, 300);
    early_ms = option("early_ms", 10, 0, 60);
    late_ms = option("late_ms", 10, 0, 60);
    memset(&pending, 0, sizeof pending);
    if (trace_file) fclose(trace_file);
    trace_file = NULL;
    const char *path = getenv("PARAPPA_TIMING_TRACE");
    if (path && *path) {
        trace_file = fopen(path, "w");
        if (trace_file)
            fputs("cycle,input_tick,tempo,buttons,stock_half,offset_ticks,extra_early,extra_late,phase,slot,turn_gate,converted,result\n", trace_file);
    }
    fprintf(stdout, "parappa timing: offset=%d ms, extra early=%d ms, extra late=%d ms\n",
            offset_ms, early_ms, late_ms);
}

static void restored(void) { memset(&pending, 0, sizeof pending); }

static void entry(CPUState *cpu, uint32_t address) {
    (void)address;
    memset(&pending, 0, sizeof pending);
    uint32_t context = cpu->gpr[4];
    /* Match the retail stage context, not menu/BIOS data or a replay callback. */
    if ((context & 0x1fffffffU) != 0x001c3640U) return;
    /* +0x52 selects scripted/recorded playback in the retail stage loop. */
    if (cpu->read_word(context + 0x50) >> 16) return;
    pending.active = 1;
    pending.cycle = psx_cycle_count;
    pending.original_tick = cpu->read_word(context + 0x10);
    pending.buttons = cpu->read_word(context + 0x18);
    pending.phase = pending.slot = pending.gate = -1;
    psx_mod_counter_add("parappa.timing.inputs", 1);
}

static void shift(CPUState *cpu, uint32_t address) {
    (void)address;
    if (!pending.active) return;
    int half = (int32_t)cpu->gpr[5];
    uint32_t song = cpu->read_word(0x8006edb8U);
    if ((song & 0x1fffffffU) > 0x00200000U - 0x160U || (song & 3U) || half < 0 || half > 24) {
        pending.active = 0;
        psx_mod_counter_add("parappa.timing.guard_reject", 1);
        return;
    }
    uint32_t tempo = cpu->read_word(song + 0x15c);
    if (tempo < 96U * 30U || tempo > 96U * 300U) {
        pending.active = 0;
        psx_mod_counter_add("parappa.timing.guard_reject", 1);
        return;
    }
    pending.tempo = tempo;
    pending.stock_half = half;
    pending.offset = parappa_ms_to_ticks(offset_ms, tempo);
    pending.early = parappa_extra_ticks(early_ms, tempo, half);
    pending.late = parappa_extra_ticks(late_ms, tempo, half);
    /* Retail uses signed division and table indices. At the start of a song,
     * a large positive offset could produce a negative cell index. Preserve
     * stock judgement until the adjusted position is inside the song grid.
     */
    int64_t biased = (int64_t)(int32_t)cpu->gpr[3] - pending.offset + half + pending.early;
    if (biased < 0 || biased > INT32_MAX) {
        pending.active = 0;
        psx_mod_counter_add("parappa.timing.guard_reject", 1);
        return;
    }
    pending.converted = 1;
    /* The patched ADDU has commuted operands and identical stock semantics.
     * Modify only local judgement registers; the song clock stays untouched.
     */
    cpu->gpr[3] -= (uint32_t)pending.offset;
    cpu->gpr[5] = (uint32_t)(half + pending.early);
}

static void window(CPUState *cpu, uint32_t address) {
    (void)address;
    if (!pending.active || !pending.converted) return;
    /* Before SLT: v0 = remainder in a 24-tick cell; v1 = cell index.
     * The stock instruction compares 2*half < remainder, inclusively accepting
     * the boundary. Separate early/late additions retain that convention.
     */
    cpu->gpr[5] = (uint32_t)(2 * pending.stock_half + pending.early + pending.late);
    pending.phase = (int32_t)cpu->gpr[2];
    pending.slot = (int32_t)cpu->gpr[3];
    psx_mod_counter_add(pending.phase <= (int)cpu->gpr[5]
        ? "parappa.timing.in_window" : "parappa.timing.outside_window", 1);
}

static void gate(CPUState *cpu, uint32_t address) {
    (void)address;
    if (pending.active && pending.converted) pending.gate = (int32_t)cpu->gpr[19];
}

static void finish(CPUState *cpu, uint32_t address) {
    (void)address;
    if (!pending.active) return;
    int result = (int32_t)cpu->gpr[2];
    psx_mod_counter_add(result == 0 ? "parappa.timing.judged" : "parappa.timing.other", 1);
    if (trace_file) {
        fprintf(trace_file, "%llu,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                (unsigned long long)pending.cycle, pending.original_tick,
                pending.tempo, pending.buttons, pending.stock_half, pending.offset,
                pending.early, pending.late, pending.phase, pending.slot,
                pending.gate, pending.converted, result);
        fflush(trace_file);
    }
    pending.active = 0;
}

PSX_MOD_CONSTRUCTOR(parappa_register_timing) {
    (void)psx_mod_register_activation_plugin(PLUGIN, activate);
    (void)psx_mod_register_savestate_plugin(PLUGIN, restored);
    (void)psx_mod_register_function_entry_plugin(PLUGIN, 0x80014614U, entry);
    (void)psx_mod_register_instruction_plugin(PLUGIN, 0x80014718U, 0x00a31821U, shift);
    (void)psx_mod_register_instruction_plugin(PLUGIN, 0x8001476cU, 0x00a2282aU, window);
    (void)psx_mod_register_instruction_plugin(PLUGIN, 0x80014798U, 0x00000000U, gate);
    (void)psx_mod_register_instruction_plugin(PLUGIN, 0x80014a5cU, 0x8fbf0044U, finish);
}
