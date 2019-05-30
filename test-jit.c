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

#undef NDEBUG
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "jit.h"

#define TEST_OP(JOP, COP)				\
do {							\
    struct jit *jit = jit_new();			\
    jins_MOV(jit, JR0, JR_ARG0);			\
    jins_##JOP(jit, JR0, JR_ARG1);			\
    uint64_t (*func)(uint64_t a, uint64_t b) =		\
	jit_compile(jit);				\
    uint64_t a = random(), b = random();		\
    uint64_t c = func(a, b);				\
    assert((a COP b) == c);				\
    jit_free(jit);					\
} while (0)

#define TEST_OPm(JOP, COP)				\
do {							\
    struct jit *jit = jit_new();			\
    jins_MOVrm(jit, JR0, JINS_MEM0(JR_ARG0));		\
    jins_##JOP##rm(jit, JR0, JINS_MEM0(JR_ARG1));	\
    uint64_t (*func)(uint64_t *a, uint64_t *b) =	\
	jit_compile(jit);				\
    uint64_t a = random(), b = random();		\
    uint64_t c = func(&a, &b);				\
    assert((a COP b) == c);				\
    jit_free(jit);					\
} while (0)

#define COP_SHL(x, s) (x << s)
#define COP_SHR(x, s) (x >> s)
#define COP_ROTL(x, s) (x << s | x >> (64 - s))
#define COP_ROTR(x, s) (x >> s | x << (64 - s))

#define TEST_OPs(JOP)					\
do {							\
    struct jit *jit = jit_new();			\
    int s = 1 + random() % 63;				\
    jins_##JOP(jit, JR_ARG0, s);			\
    jins_MOV(jit, JR0, JR_ARG0);			\
    uint64_t (*func)(uint64_t x) =			\
	jit_compile(jit);				\
    uint64_t x = random();				\
    uint64_t y = func(x);				\
    assert(COP_##JOP(x, s) == y);			\
    jit_free(jit);					\
} while (0)

#define COP_BSWAP __builtin_bswap64

#define TEST_OPr(JOP)					\
do {							\
    struct jit *jit = jit_new();			\
    jins_MOV(jit, JR0, JR_ARG0);			\
    jins_##JOP(jit, JR0);				\
    uint64_t (*func)(uint64_t x) =			\
	jit_compile(jit);				\
    uint64_t x = random();				\
    uint64_t y = func(x);				\
    assert(COP_##JOP(x) == y);				\
    jit_free(jit);					\
} while (0)

static void test_swap(void)
{
    struct jit *jit = jit_new();
    jins_MOVrm(jit, JR3, JINS_MEM0(JR_ARG0));
    jins_MOVrm(jit, JR4, JINS_MEM0(JR_ARG1));
    jins_MOVmr(jit, JINS_MEM0(JR_ARG0), JR4);
    jins_MOVmr(jit, JINS_MEM0(JR_ARG1), JR3);
    void (*swap)(uint64_t *a, uint64_t *b) =
	jit_compile(jit);
    const uint64_t a0 = random(), b0 = random();
    uint64_t a = a0, b = b0;
    swap(&a, &b);
    assert(a == b0);
    assert(b == a0);
    jit_free(jit);
}

static void test_XORswap(void)
{
    struct jit *jit = jit_new();
    jins_MOVrm(jit, JR5, JINS_MEM0(JR_ARG0));
    jins_MOVrm(jit, JR9, JINS_MEM0(JR_ARG1));
    jins_XOR(jit, JR5, JR9);
    jins_XOR(jit, JR9, JR5);
    jins_XOR(jit, JR5, JR9);
    jins_MOVmr(jit, JINS_MEM0(JR_ARG0), JR5);
    jins_MOVmr(jit, JINS_MEM0(JR_ARG1), JR9);
    void (*swap)(uint64_t *a, uint64_t *b) =
	jit_compile(jit);
    const uint64_t a0 = random(), b0 = random();
    uint64_t a = a0, b = b0;
    swap(&a, &b);
    assert(a == b0);
    assert(b == a0);
    jit_free(jit);
}

int main()
{
    for (int i = 0; i < 9; i++) {
	TEST_OP(ADD, +);
	TEST_OP(SUB, -);
	TEST_OP(XOR, ^);
	TEST_OPm(ADD, +);
	TEST_OPm(SUB, -);
	TEST_OPm(XOR, ^);
	TEST_OPs(SHL);
	TEST_OPs(SHR);
	TEST_OPs(ROTL);
	TEST_OPs(ROTR);
	TEST_OPr(BSWAP);
	test_swap();
	test_XORswap();
    }
    return 0;
}
