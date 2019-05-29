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
#if defined(_WIN32) || defined(__CYGWIN__)
    jins86_PUSH(jit, RSI);
    jins86_PUSH(jit, RDI);
#endif
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
#if defined(_WIN32) || defined(__CYGWIN__)
    jins86_POP(jit, RDI);
    jins86_POP(jit, RSI);
#endif
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
    RAX, RBX, RBP,
#if defined(_WIN32) || defined(__CYGWIN__)
    RSI, RDI,
    R10, R11, R12, R13, R14, R15,
    R9, R8, RDX, RCX,
#else
    R10, R11, R12, R13, R14, R15,
    R9, R8, RCX, RDX, RSI, RDI,
#endif
};

#define JRto86(reg) (assert(reg >= 0 && reg < sizeof JRto86map), JRto86map[reg])

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

static void jins86_OPrs(struct jit *jit, int mod, enum R86_e reg, int imm8)
{
    int rex = 0x48;
    rex |= (reg >= 8);
    *jit->cur++ = rex;
    *jit->cur++ = 0xc1;

    int modrm = 3 << 6;
    modrm |= (mod << 3);
    modrm |= (reg & 7);
    *jit->cur++ = modrm;
    *jit->cur++ = imm8;
}

#define ShiftVal(imm8) (assert(imm8 >= 0 && imm8 < 64), imm8)
#define OPrs(mod) jins86_OPrs(jit, mod, JRto86(reg), ShiftVal(imm8))

void jins_ROTL(struct jit *jit, enum JR_e reg, int imm8) { OPrs(0); }
void jins_ROTR(struct jit *jit, enum JR_e reg, int imm8) { OPrs(1); }

static void jins86_OPr(struct jit *jit, int op, int modrm, enum JR_e reg)
{
    int rex = 0x48;
    rex |= (reg >= 8);
    *jit->cur++ = rex;
    *jit->cur++ = op;

    modrm |= (reg & 7);
    *jit->cur++ = modrm;
}

#define OPr(op, modrm) jins86_OPr(jit, op, modrm, JRto86(reg))

void jins_BSWAP(struct jit *jit, enum JR_e reg) { OPr(0x0f, 0xc8); }

static void jins86_OPrm(struct jit *jit, int op, enum R86_e reg, enum R86_e mem, int disp8)
{
    int rex = 0x48;
    rex |= (reg >= R8) << 2;
    rex |= (mem >= R8) << 0;
    *jit->cur++ = rex;
    *jit->cur++ = op;

    int has8 = (disp8 != 0);
    int modrm = (has8 << 6);
    modrm |= (reg & 7) << 3;
    modrm |= (mem & 7) << 0;
    *jit->cur++ = modrm;
    *jit->cur = disp8;
    jit->cur += has8;
}

#define DispVal(disp8) (assert(disp8 >= 0 && disp8 < 128), disp8)
#define OPrm(op) jins86_OPrm(jit, op, JRto86(dst), JRto86(mem), DispVal(disp8))
#define OPmr(op) jins86_OPrm(jit, op, JRto86(src), JRto86(mem), DispVal(disp8))

void jins_MOVrm(struct jit *jit, enum JR_e dst, JINS_MEM_ARG) { OPrm(0x8b); }
void jins_MOVmr(struct jit *jit, JINS_MEM_ARG, enum JR_e src) { OPmr(0x89); }
