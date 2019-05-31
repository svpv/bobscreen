#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#include <algorithm>
#include "jit.h"

//
// try to find an adequate long-message mixing function for SpookyHash
//

class UInt64Helper
{
public:
  static inline uint64_t Bswap64(uint64_t x)
  {
    return __builtin_bswap64(x);
  }
  // return x rotated left by k
  static inline uint64_t Rot64(uint64_t x, int k)
  {
    if (k == 0 || k == 64)
	return Bswap64(x);
    return (x << k) | (x >> (64-(k)));
  }

  // count how many bits are set in an 8-byte value
  static uint64_t Popcnt(uint64_t x)
  {
#ifdef __GNUC__
    return __builtin_popcountll(x);
#endif
    x = ((x & 0x5555555555555555LL) + ((x & 0xaaaaaaaaaaaaaaaaLL) >> 1));
    x = ((x & 0x3333333333333333LL) + ((x & 0xccccccccccccccccLL) >> 2));
    x = ((x & 0x0f0f0f0f0f0f0f0fLL) + ((x & 0xf0f0f0f0f0f0f0f0LL) >> 4));
    x = ((x & 0x00ff00ff00ff00ffLL) + ((x & 0xff00ff00ff00ff00LL) >> 8));
    x = ((x & 0x0000ffff0000ffffLL) + ((x & 0xffff0000ffff0000LL) >> 16));
    x = ((x & 0x00000000ffffffffLL) + ((x & 0xffffffff00000000LL) >> 32));
    return x;
  }
};


// random number generator
class Random : UInt64Helper
{ 
public:
    inline uint64_t Value()
    {
        uint64_t e = m_a - Rot64(m_b, 23);
        m_a = m_b ^ Rot64(m_c, 16);
        m_b = m_c + Rot64(m_d, 11);
        m_c = m_d + e;
        m_d = e + m_a;
        return m_d;
    }

    inline void Init( uint64_t seed)
    {
        m_a = 0xdeadbeef; 
        m_b = m_c = m_d = seed;
        for (int i=0; i<20; ++i) 
            (void)Value();
    }

private:
    uint64_t m_a;
    uint64_t m_b;
    uint64_t m_c;
    uint64_t m_d;
};


// generate, test, and report mixing functions
class Sieve : UInt64Helper
{
  enum OP_e { OP_ADD, OP_SUB, OP_XOR, OP_ROT };
  enum { MOD_ADDSUB = OP_XOR, MOD_BINOP = OP_ROT };

  inline void EmitOp(int iOp, int OP)
  {
    _op[iOp] = OP;
  }
  inline void SetBinopVars(int iOp, int L, int R)
  {
    assert(_op[iOp] < MOD_BINOP);
    _v1[iOp] = L;
    _v2[iOp] = R;
  }
  inline void EmitRot(int iOp, int LR)
  {
    _op[iOp] = OP_ROT;
    _v1[iOp] = _v2[iOp] = LR;
  }

public:
  Sieve(int seed, FILE *fp)
  {
    _r.Init(seed);
    _fp = fp;
  }

  ~Sieve()
  {
  }

  // Restore to the original SpookyMix function.
  void PreloadSpooky()
  {
    assert(_ops == 5);
    assert(_vars == 12);

    EmitOp(0, OP_ADD);
    EmitOp(1, OP_XOR); SetBinopVars(1, 2, 10);
    EmitOp(2, OP_XOR); SetBinopVars(2, 11, 0);
    EmitOp(3, OP_ROT); EmitRot(3, 0);
    EmitOp(4, OP_ADD); SetBinopVars(4, 11, 1);

    const uint8_t shifts[] = { 11, 32, 43, 31, 17, 28, 39, 57, 55, 54, 22, 46, };
    for (int iVar=0; iVar<_vars; ++iVar)
      _s[iVar] = _s[iVar + _vars] = shifts[iVar];
  }

  // Examine the mixing function from SpookyAlpha.
  void PreloadAlpha()
  {
    assert(_ops == 5);
    assert(_vars == 12);

    EmitOp(0, OP_ADD);
    EmitOp(1, OP_ROT); EmitRot(1, 11);
    EmitOp(2, OP_XOR); SetBinopVars(2, 9, 1);
    EmitOp(3, OP_ADD); SetBinopVars(3, 11, 10);
    EmitOp(4, OP_ADD); SetBinopVars(4, 1, 10);

    const uint8_t shifts[] = { 32, 41, 12, 24, 8, 42, 32, 13, 30, 20, 47, 16, };
    for (int iVar=0; iVar<_vars; ++iVar)
      _s[iVar] = _s[iVar + _vars] = shifts[iVar];
  }

  // Another Bob's brainchild was AkronHash.
  void PreloadAkron()
  {
    assert(_ops == 5);
    assert(_vars == 12);

    EmitOp(0, OP_ADD);
    EmitOp(1, OP_ROT); EmitRot(1, 2);
    EmitOp(2, OP_XOR); SetBinopVars(2, 2, 0);
    EmitOp(3, OP_ADD); SetBinopVars(3, 4, 0);
    EmitOp(4, OP_ADD); SetBinopVars(4, 0, 3);

    const uint8_t shifts[] = { 32, 37, 27, 48, 5, 7, 50, 18, 9, 44, 14, 30, };
    for (int iVar=0; iVar<_vars; ++iVar)
      _s[iVar] = _s[iVar + _vars] = shifts[iVar];
  }

  // generate a new function at random
  void Generate()
  {
    // We need to perfrom the following steps:
    // -  s0 ?= data[0]   data injection
    // -  s2 ?= s10       long-distance mix, s10 from the last iteration
    // -  s0 = Rot64(s0)  permute after injection
    // -  s11 ?= s0       mixing the data into the previous var
    // -  s11 ?= s1       mix old next var into the previous var

    // Unlike in the original construction, ours is an early ROT.
    int rotpos = 2;
    EmitRot(rotpos, 0);

    // Need at least one ADD/SUB and at least one XOR.
    int addop = _r.Value() % MOD_BINOP;
    int addpos = 0;
    int xorpos = 1 + _r.Value() % (_ops - 2);
    xorpos += (xorpos >= rotpos);
    if (addop == OP_XOR) {
      addpos = xorpos;
      xorpos = 0;
      addop = _r.Value() % MOD_ADDSUB;
    }
    EmitOp(addpos, addop);
    EmitOp(xorpos, OP_XOR);

    // The rest are either ADD/SUB or XOR.
    for (int iOp = 0; iOp < _ops; iOp++) {
      if (iOp == addpos || iOp == xorpos || iOp == rotpos)
	continue;
      EmitOp(iOp, _r.Value() % MOD_BINOP);
    }

    // Ops have been filled, connect vars to binops.
    int iOp = 1;
    iOp += (iOp == rotpos);
    SetBinopVars(iOp++, 2, _vars - 2); // s2 ?= s10
    iOp += (iOp == rotpos);
    SetBinopVars(iOp++, _vars - 1, 0); // s11 ?= s0
    iOp += (iOp == rotpos);
    SetBinopVars(iOp++, _vars - 1, 1); // s11 ?= s1

    // Fill in the rotation constatns.
    for (int iVar=0; iVar<_vars; ++iVar)
    {
      _s[iVar] = _s[iVar + _vars] = (_r.Value() % 65);
    }
  }

  int Test()
  {
    int minVal = INT_MAX;

    for (int iVar=0; iVar<_vars; ++iVar)
    {
      static const int tries = 5;
      int try0[tries], try1[tries];

      JitMixFunc Mix0(*this, 1, iVar);
      int aVal0 = OneTest(Mix0);
      if (aVal0 == 0) return 0;
      try0[0] = aVal0;

      JitMixFunc Mix1(*this, 0, iVar);
      int aVal1 = OneTest(Mix1);
      if (aVal1 == 0) return 0;
      try1[0] = aVal1;

      for (int i = 1; i < tries; i++) {
	aVal0 = OneTest(Mix0);
	if (aVal0 == 0) return 0;
	try0[i] = aVal0;
	aVal1 = OneTest(Mix1);
	if (aVal1 == 0) return 0;
	try1[i] = aVal1;
      }

      std::sort(try0, try0 + tries);
      std::sort(try1, try1 + tries);

      // Robust estimate, ignore outliers at [0].
      int e0 = (try0[1] + try0[2]) / 2;
      int e1 = (try1[1] + try1[2]) / 2;
      minVal = std::min(minVal, e0);
      minVal = std::min(minVal, e1);
    }
    printf("// minVal = %d\n", minVal);
    return 1;
  }

  void Pre()
  {
    fprintf(_fp, "#include <stdio.h>\n");
    fprintf(_fp, "#include <stdint.h>\n");
    fprintf(_fp, "\n");
    fprintf(_fp, "#define VAR %d\n", _vars);
    fprintf(_fp, "#define ITERS (100000000)\n");
    fprintf(_fp, "#define CUT 4000\n");
    fprintf(_fp, "#define Rot64(x,k) (((x)<<(k)) | ((x)>>(64-(k))))\n");
    fprintf(_fp, "\n");
  }

  // print the function in C++ code
  void ReportCode(int version)
  {
    fprintf(_fp, "void function%d(uint64_t *data, uint64_t *state)\n", version);
    fprintf(_fp, "{\n");
    
    for (int iVar=0; iVar<_vars; ++iVar)
    {
      fprintf(_fp, "    uint64_t s%d = state[%d];\n", iVar, iVar);
    }

    for (int iIter=0; iIter<_iters; ++iIter)
    {
      for (int iVar=0; iVar<_vars; ++iVar)
      {
	const char opc[] = "+-^????";
	fprintf(_fp, "    s%d %c= data[%d];", iVar, opc[_op[0]], iVar);
	for (int iOp=1; iOp<_ops; ++iOp)
	{
	  PrintOp(_fp, _op[iOp], 
		  (_v1[iOp] + iVar) % _vars, 
		  (_v2[iOp] + iVar) % _vars, 
		  _s[iVar]);
	}
	fprintf(_fp, "\n");
      }
    }
    
    for (int iVar=0; iVar<_vars; ++iVar)
    {
      fprintf(_fp, "    state[%d] = s%d;\n", iVar, iVar);
    }

    fprintf(_fp, "}\n");
    fprintf(_fp, "\n");
    fprintf(_fp, "void wrapper%d(uint64_t *data, uint64_t *state)\n", version);
    fprintf(_fp, "{\n");
    fprintf(_fp, "  uint64_t a = GetTickCount();\n");
    fprintf(_fp, "  for (int i=0; i<ITERS; ++i) {\n");
    fprintf(_fp, "    function%d(data, state);\n", version);
    fprintf(_fp, "  }\n");
    fprintf(_fp, "  uint64_t z = GetTickCount();\n");
    fprintf(_fp, "  if (z-a < CUT) {\n");
    fprintf(_fp, "    printf(\"");
    ReportStructure(version);
    fprintf(_fp, "  %%lld\\n\", z-a);\n");
    fprintf(_fp, "  }\n");
    fprintf(_fp, "}\n");
    fprintf(_fp, "\n");
  }

  void ReportStructure(int version)
  {
    for (int iOp=0; iOp<_ops; ++iOp)
    {
      fprintf(_fp, "%1d %2d %2d ", _op[iOp], _v1[iOp], _v2[iOp]);
    }
    fprintf(_fp, " ");
    for (int iVar=0; iVar<_vars; ++iVar)
    {
      fprintf(_fp, "%2d ", _s[iVar]);
    }
  }

  void Post(int numFunctions)
  {
    int i;
    fprintf(_fp, "\n");
    fprintf(_fp, "int main(int argc, char **argv)\n");
    fprintf(_fp, "{\n");
    fprintf(_fp, "  uint64_t a, state[VAR], data[VAR];\n");
    fprintf(_fp, "  int i;\n");
    fprintf(_fp, "  for (int i=0; i<VAR; ++i) state[i] = data[i] = i+argc;\n");
    for (i=0; i<numFunctions; ++i)
    {
      fprintf(_fp, "  wrapper%d(data, state);\n", i);
    }
    fprintf(_fp, "}\n");
    fprintf(_fp, "\n");
  }


private:

  // print operation
  static void inline PrintOp(FILE *fp, int k, int x, int y, int s)
  {
    switch (k) {
    case OP_ADD: fprintf(fp, "    s%d += s%d;", x, y); break;
    case OP_SUB: fprintf(fp, "    s%d -= s%d;", x, y); break;
    case OP_XOR: fprintf(fp, "    s%d ^= s%d;", x, y); break;
    default: assert(k == OP_ROT);
      if (s == 0 || s == 64)
	fprintf(fp, "    s%d = Bswap64(s%d);", x, x);
      else
	fprintf(fp, "    s%d = Rot64(s%d, %d);", x, x, s);
    }
  }

  class JitMixFunc;

  int OneTest(JitMixFunc& Mix)
  {
    static const int _measures = 10;  // number of different ways of looking
    static const int _trials = 3;     // number of pairs of hashes
    static const int _limit =3*64;    // minimum number of bits affected
    uint64_t a[_measures][_vars];
    int minVal = _vars*64;

    // iBit covers just key[0], because that is the variable we start at
    for (int iBit=0; iBit<64; ++iBit)
    {  
      for (int iBit2=iBit; iBit2<_vars*64; ++iBit2)
      {  
	uint64_t total[_measures][_vars] = {};  // accumulated affect per bit
	for (int iTrial=0; iTrial<_trials; ++iTrial)
	{
	  // test one pair of inputs
	  uint64_t data[_vars] = {};
	  for (int iVar=0; iVar<_vars; ++iVar)
	  {
	    uint64_t value = _r.Value();
	    // if (1 || iVar != goose) value = 0;  // hack
	    a[0][iVar] = value;  // input/output of first of pair
	    a[1][iVar] = value;  // input/output of second of pair
	  }
	  
	  // evaluate first of pair
	  Mix(a[0], data);
	  
	  // evaluate second of pair, differing in one bit
	  data[iBit/64] ^= (((uint64_t)1) << (iBit & 63));
	  if (iBit2 != iBit)
	  {
	    data[iBit2/64] ^= (((uint64_t)1) << (iBit2 & 63));
	  }
	  Mix(a[1], data);
	  
	  for (int iVar=0; iVar<_vars; ++iVar)
	  {
	    a[2][iVar] = a[0][iVar] ^ a[1][iVar];  // xor of first and second
	    a[3][iVar] = a[0][iVar] - a[1][iVar];
	    a[3][iVar] ^= a[3][iVar]>>1;   // "-" of first and second, graycoded
	    a[4][iVar] = a[0][iVar] + a[1][iVar];
	    a[4][iVar] ^= a[4][iVar]>>1;   // "+" of first and second, graycoded
	    a[5][iVar] = ~a[0][iVar];      // a[5..9] are complements of a[0..4]
	    a[6][iVar] = ~a[1][iVar];         
	    a[7][iVar] = ~a[2][iVar];
	    a[8][iVar] = ~a[3][iVar];
	    a[9][iVar] = ~a[4][iVar];
	  }
	  for (int iMeasure=0; iMeasure<_measures; ++iMeasure)
	  {
	    for (int iVar=0; iVar<_vars; ++iVar)
	    {
	      total[iMeasure][iVar] |= a[iMeasure][iVar];
	    }
	  }
	}
	for (int iMeasure=0; iMeasure<_measures; ++iMeasure)
	{
	  int counter = 0;
	  for (int iVar=0; iVar<_vars; ++iVar)
	  {
	    counter += Popcnt(total[iMeasure][iVar]);
	  }
	  if (counter < _limit)
	  {
	    if (1)
	    {
	      printf("// fail %d %d %d\n", iMeasure, iBit, counter);
	    }
	    return 0;
	  }
	  if (counter < minVal)
	  {
	    minVal = counter;
	  }
	}
      }
    }
    return minVal;
  }

  class JitMixFunc
  {
    struct jit *jit;
    typedef void (*func_t)(uint64_t *state, const uint64_t *data);
    func_t func;

    // Put the state variables into registers.
    void Unpack()
    {
      for (int iVar=0; iVar <_vars; ++iVar)
	jins_MOVrm(jit, (JR_e) iVar, JINS_MEM(JR_ARG0, 8*iVar));
    }

    // Gather the state back.
    void Bundle()
    {
      for (int iVar=0; iVar <_vars; ++iVar)
	jins_MOVmr(jit, JINS_MEM(JR_ARG0, 8*iVar), (JR_e) iVar);
    }

    // Trickle-feed some data into the state: sX ?= data[X]
    void Feed(OP_e op, int iVar)
    {
      switch (op) {
      case OP_ADD: jins_ADDrm(jit, (JR_e) iVar, JINS_MEM(JR_ARG1, 8*iVar)); break;
      case OP_SUB: jins_SUBrm(jit, (JR_e) iVar, JINS_MEM(JR_ARG1, 8*iVar)); break;
      case OP_XOR: jins_XORrm(jit, (JR_e) iVar, JINS_MEM(JR_ARG1, 8*iVar)); break;
      default: assert(0);
      }
    }

    // In the reverse direction, not symmetric (see below)
    void RFeed(OP_e op, int iState, int iData)
    {
      switch (op) {
      case OP_ADD: jins_SUBrm(jit, (JR_e) iState, JINS_MEM(JR_ARG1, 8*iData)); break;
      case OP_SUB: jins_ADDrm(jit, (JR_e) iState, JINS_MEM(JR_ARG1, 8*iData)); break;
      case OP_XOR: jins_XORrm(jit, (JR_e) iState, JINS_MEM(JR_ARG1, 8*iData)); break;
      default: assert(0);
      }
    }

    // A mixing step: sX ?= sY, or possibly sX = permute(sX, param)
    void Op(OP_e op, JR_e dst, JR_e src, int param)
    {
      switch (op) {
      case OP_ADD: jins_ADD(jit, dst, src); break;
      case OP_SUB: jins_SUB(jit, dst, src); break;
      case OP_XOR: jins_XOR(jit, dst, src); break;
      default: assert(op == OP_ROT);
	if (param % 64 == 0)
	  jins_BSWAP(jit, dst);
	else
	  jins_ROTL(jit, dst, param);
      }
    }

    void ROp(OP_e op, JR_e dst, JR_e src, int param)
    {
      switch (op) {
      case OP_ADD: jins_SUB(jit, dst, src); break;
      case OP_SUB: jins_ADD(jit, dst, src); break;
      case OP_XOR: jins_XOR(jit, dst, src); break;
      default: assert(op == OP_ROT);
	if (param % 64 == 0)
	  jins_BSWAP(jit, dst);
	else
	  jins_ROTL(jit, dst, 64 - param);
      }
    }

    void CodegenForward(Sieve const& p, const int *shifts)
    {
      for (int iIter=0; iIter < _iters; ++iIter)
      {
	for (int iVar=0; iVar <_vars; ++iVar)
	{
	  Feed((OP_e) p._op[0], iVar);
	  for (int iOp=1; iOp < _ops; ++iOp)
	  {
	    Op((OP_e) p._op[iOp],
	       (JR_e)((p._v1[iOp] + iVar) % p._vars),
	       (JR_e)((p._v2[iOp] + iVar) % p._vars),
	       shifts[iVar]);
	  }
	}
      }
    }

    void CodegenBackward(Sieve const& p, const int *shifts)
    {
      for (int iIter=_iters; iIter--;)
      {
	for (int iVar=_vars; iVar--;)
	{
	  // the data is not being added symmetrically, but the goal is to test all deltas,
	  // not test them in the reverse order that they were tested forwards.
	  RFeed((OP_e) p._op[0], (iVar + 1) % _vars, _vars - iVar - 1);
	  for (int iOp=_ops; --iOp;)
	  {
	    ROp((OP_e) p._op[iOp],
		(JR_e)((p._v1[iOp] + iVar) % _vars),
		(JR_e)((p._v2[iOp] + iVar) % _vars),
		shifts[iVar]);
	  }
	}
      }
    }

  public:
    JitMixFunc(Sieve const& p, bool forward, int start)
    {
      jit = jit_new();
      Unpack();
      if (forward)
	CodegenForward(p, p._s + start);
      else
	CodegenBackward(p, p._s + start);
      Bundle();
      func = (func_t) jit_compile(jit);
    }

    ~JitMixFunc()
    {
      jit_free(jit);
    }

    void operator()(uint64_t *state, const uint64_t *data)
    {
      func(state, data);
    }
  };

  static const int _vars = 12;
  static const int _ops = 5;
  static const int _iters = 1;

  FILE *_fp;       // output file pointer
  Random _r;       // random number generator

  int _op[_ops];   // what type of operation (values in 0..3)
  int _v1[_ops];   // which variable first (values in 0..VAR-1)
  int _v2[_ops];   // which variable next (values in 0..VAR-1)
  int _s[2*_vars]; // shift constant (values 0..63)
};

  
void driver(uint64_t seed, FILE *fp, int numFunctions)
{
  Sieve sieve(seed, fp);

  sieve.Pre();

  int version = 0;
  while (version < numFunctions)
  {
    sieve.Generate();
    if (sieve.Test())
    {
      sieve.ReportCode(version++);
    }
  }

  sieve.Post(numFunctions);
}

int main(int argc, char **argv)
{
  int n = 3;
  if (argc > 1) {
    assert(argc == 2);
    n = atoi(argv[1]);
    assert(n > 0);
  }
  driver(21, stdout, n);
}
