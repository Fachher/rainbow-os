#ifndef DISASM_H
#define DISASM_H

#include "include/types.h"

/* Decode one instruction at `code` (whose virtual address is `addr`) into a
   human-readable mnemonic in `out`. Returns the instruction length in bytes.
   Only the opcode subset emitted by the cc codegen is decoded; anything else
   renders as `db 0xNN` with length 1 (so the stream never desyncs). */
int disasm_one(const uint8_t *code, uint32_t addr, char *out, int outsz);

#endif
