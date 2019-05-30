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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// This JIT virtual machine provides 15 general-purpose registers,
// coincidentally numbered starting with 0 (so you don't have to use
// their names).
enum JR_e {
    JR0, JR1, JR2, JR3, JR4, JR5, JR6, JR7,
    JR8, JR9, JR10, JR11, JR12, JR13, JR14,
};

// The calling convention: arguments are passed in R14, R13, R12, R11,
// i.e. in reverse order; up to 4 arguments can be passed.  The return
// value is passed back in JR0.
#define JR_ARG0 JR14
#define JR_ARG1 JR13
#define JR_ARG2 JR12
#define JR_ARG3 JR11

struct jit *jit_new(void);
void jit_free(struct jit *jit);

// A memory reference: base register with displacement.
#define JINS_MEM(reg, disp8) reg, disp8
#define JINS_MEM0(reg) reg, 0
#define JINS_MEM_ARG enum JR_e mem, int disp8

// Feed some instructions into the JIT compiler.
void jins_ADD(struct jit *jit, enum JR_e dst, enum JR_e src);
void jins_SUB(struct jit *jit, enum JR_e dst, enum JR_e src);
void jins_XOR(struct jit *jit, enum JR_e dst, enum JR_e src);

void jins_SHL(struct jit *jit, enum JR_e reg, int imm8);
void jins_SHR(struct jit *jit, enum JR_e reg, int imm8);
void jins_ROTL(struct jit *jit, enum JR_e reg, int imm8);
void jins_ROTR(struct jit *jit, enum JR_e reg, int imm8);
void jins_BSWAP(struct jit *jit, enum JR_e reg);

void jins_ADDrm(struct jit *jit, enum JR_e dst, JINS_MEM_ARG);
void jins_SUBrm(struct jit *jit, enum JR_e dst, JINS_MEM_ARG);
void jins_XORrm(struct jit *jit, enum JR_e dst, JINS_MEM_ARG);

void jins_MOV(struct jit *jit, enum JR_e dst, enum JR_e src);
void jins_MOVrm(struct jit *jit, enum JR_e dst, JINS_MEM_ARG);
void jins_MOVmr(struct jit *jit, JINS_MEM_ARG, enum JR_e src);

// After all the instruction are added, obtain a callable function.
void *jit_compile(struct jit *jit);

#ifdef __cplusplus
}
#endif
