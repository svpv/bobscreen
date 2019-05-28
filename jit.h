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

// The set of registers is not particularly abstract, it is rather tailored
// for the purpose of screening SpookyMix-like functions.  We provide 13
// registers for state variables, starting with 0, and 2 registers to pass
// arguments.
enum JR_e {
    JR_S0, JR_S1, JR_S2, JR_S3, JR_S4, JR_S5, JR_S6, JR_S7,
    JR_S8, JR_S9, JR_S10, JR_S11, JR_S12,
    JR_ARG0, JR_ARG1,
};

struct jit *jit_new(void);
void jit_free(struct jit *jit);

// Feed some instructions into the JIT compiler.
void jins_ADD(struct jit *jit, enum JR_e dst, enum JR_e src);
void jins_SUB(struct jit *jit, enum JR_e dst, enum JR_e src);
void jins_XOR(struct jit *jit, enum JR_e dst, enum JR_e src);

void jins_ROT(struct jit *jit, enum JR_e dst, int imm8);
void jins_BSWAP(struct jit *jit, enum JR_e dst);

void jins_ADDrm(struct jit *jit, enum JR_e dst, enum JR_e mem, int imm8);
void jins_SUBrm(struct jit *jit, enum JR_e dst, enum JR_e mem, int imm8);
void jins_XORrm(struct jit *jit, enum JR_e dst, enum JR_e mem, int imm8);

void jins_MOVrm(struct jit *jit, enum JR_e dst, enum JR_e mem, int imm8);
void jins_MOVmr(struct jit *jit, enum JR_e mem, int imm8, enum JR_e src);

// After all the instruction are added, obtain a callable function.
void (*jit_compile(struct jit *jit))();
