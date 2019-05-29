// Copyright (c) 2019 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include "jit.h"

struct jit {
    uint8_t *page;
    uint8_t *cur;
};

enum R86_e {
    RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15,
};

static void jins86_PUSH(struct jit *jit, enum R86_e reg)
{
    *jit->cur = 0x41;
    jit->cur += (reg >= R8);
    *jit->cur++ = 0x50 + (reg & 7);
}

static void jins86_POP(struct jit *jit, enum R86_e reg)
{
    *jit->cur = 0x41;
    jit->cur += (reg >= R8);
    *jit->cur++ = 0x58 + (reg & 7);
}

static void jins_saveRegs(struct jit *jit)
{
    jins86_PUSH(jit, RBX);
    jins86_PUSH(jit, RBP);
    jins86_PUSH(jit, R12);
    jins86_PUSH(jit, R13);
    jins86_PUSH(jit, R14);
    jins86_PUSH(jit, R15);
}

static void jins_restoreRegs(struct jit *jit)
{
    jins86_POP(jit, R15);
    jins86_POP(jit, R14);
    jins86_POP(jit, R13);
    jins86_POP(jit, R12);
    jins86_POP(jit, RBP);
    jins86_POP(jit, RBX);
}

static void jins_RET(struct jit *jit)
{
    *jit->cur++ = 0xc3;
}

static long pagesize;

struct jit *jit_new(void)
{
    struct jit *jit = malloc(sizeof *jit);
    assert(jit);

    if (pagesize == 0) {
	pagesize = sysconf(_SC_PAGESIZE);
	assert(pagesize >= 4096);
    }

    jit->page = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(jit->page != NULL && jit->page != MAP_FAILED);
    jit->cur = jit->page;

    jins_saveRegs(jit);
    return jit;
}

void jit_free(struct jit *jit)
{
    if (!jit)
	return;
    int rc = munmap(jit->page, pagesize);
    assert(rc == 0);
    free(jit);
} 

void (*jit_compile(struct jit *jit))()
{
    jins_restoreRegs(jit);
    jins_RET(jit);

    int rc = mprotect(jit->page, pagesize, PROT_READ | PROT_EXEC);
    assert(rc == 0);

    return (void *) jit->page;
}

static const uint8_t JRto86map[] = {
    RAX, RCX, RDX, RBX, RBP,
    R8, R9, R10, R11, R12, R13, R14, R15,
    RDI, RSI
};

#define JRto86(reg) (assert(reg < sizeof JRto86map), JRto86map[reg])

static void jins86_OPrr(struct jit *jit, int op, enum R86_e dst, enum R86_e src)
{
    int rex = 0x48;           // REX.W
    rex |= (src >= R8) << 2;  // REX.R
    rex |= (dst >= R8) << 0;  // REX.B
    *jit->cur++ = rex;
    *jit->cur++ = op;

    int modrm = 3 << 6;
    modrm |= (src & 7) << 3;
    modrm |= (dst & 7) << 0;
    *jit->cur++ = modrm;
}

#define OPrr(op) jins86_OPrr(jit, op, JRto86(dst), JRto86(src))

void jins_ADD(struct jit *jit, enum JR_e dst, enum JR_e src) { OPrr(0x01); }
void jins_SUB(struct jit *jit, enum JR_e dst, enum JR_e src) { OPrr(0x29); }
void jins_XOR(struct jit *jit, enum JR_e dst, enum JR_e src) { OPrr(0x31); }
