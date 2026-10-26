/* Static recompiler: Game Boy ROM -> C source.  Used ahead of time by the
 * developer build (tools/recomp/dk94_recomp.c) and at runtime by the
 * player, which compiles the result with TinyCC on the user's machine. */
#ifndef RECOMP_H
#define RECOMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- opcode table (generated from tools/recomp/sm83.py) ---- */

typedef enum {
    K_OP, K_JP, K_JR, K_CALL, K_RET, K_RETI, K_RST, K_JPHL, K_HALT, K_STOP,
    K_EI, K_DI, K_ILLEGAL, K_PREFIX
} Sm83Kind;

typedef struct {
    uint8_t length, kind, cycles, cycles_taken, target, sets_a;
    const char *mnem;   /* disassembly, with {n8} {n16} {e8} {e8:+} placeholders */
    const char *code;   /* C template for K_OP, same placeholders */
    const char *cond;   /* C condition for conditional flow, or NULL */
} Sm83Op;

extern const Sm83Op sm83_main[256], sm83_cb[256];

/* ---- game notes (generated from overrides.toml / coverage.txt) ---- */

typedef struct { uint8_t bank; uint16_t addr; } RecompRef;
typedef struct { uint16_t addr, max; } RecompFarTable;

typedef struct {
    uint32_t rom_crc32;
    const uint16_t *jump_table_routines; size_t n_jump_table_routines;
    const uint16_t *bank_switch_routines; size_t n_bank_switch_routines;
    const RecompRef *noreturn; size_t n_noreturn;
    const RecompRef *entries; size_t n_entries;
    const RecompFarTable *far_tables; size_t n_far_tables;
    const RecompRef *coverage; size_t n_coverage;   /* sorted, unique */
} RecompConfig;

extern const RecompConfig dk94_game;

/* ---- results ---- */

typedef struct {
    uint8_t bank;
    uint16_t entry;
    size_t src_off, src_len;   /* this function's text within RecompResult.src */
} RecompFunc;

typedef struct {
    uint8_t bank;
    uint16_t addr;
    uint32_t fn;               /* index into funcs */
} RecompEntry;

typedef struct {
    char *src;                 /* all functions, ordered by (bank, entry) */
    size_t src_len;
    RecompFunc *funcs;
    uint32_t nfuncs;
    RecompEntry *entries;      /* every instruction, function entries first */
    size_t nentries;
    size_t ninsns, ntables, nseeds;
} RecompResult;

bool recomp_run(const uint8_t *rom, size_t size, const RecompConfig *cfg, RecompResult *out);
void recomp_free(RecompResult *r);
void recomp_fn_name(uint8_t bank, uint16_t entry, char out[16]);

#endif
