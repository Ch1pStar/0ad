/**
 * =========================================================================
 * File        : bits.cpp
 * Project     : 0 A.D.
 * Description : bit-twiddling.
 * =========================================================================
 */

// license: GPL; see lib/license.txt

#include "precompiled.h"

#if ARCH_IA32
# include "lib/sysdep/ia32/ia32_asm.h"	// ia32_asm_log2_of_pow2
#endif


bool is_pow2(uint n)
{
	// 0 would pass the test below but isn't a POT.
	if(n == 0)
		return false;
	return (n & (n-1ul)) == 0;
}


int log2_of_pow2(uint n)
{
	int bit_index;

#if ARCH_IA32
	bit_index = ia32_asm_log2_of_pow2(n);
#else
	if(!is_pow2(n))
		bit_index = -1;
	else
	{
		bit_index = 0;
		// note: compare against n directly because it is known to be a POT.
		for(uint bit_value = 1; bit_value != n; bit_value *= 2)
			bit_index++;
	}
#endif

	debug_assert(-1 <= bit_index && bit_index < (int)sizeof(int)*CHAR_BIT);
	debug_assert(bit_index == -1 || n == (1u << bit_index));
	return bit_index;
}


uint ceil_log2(uint x)
{
	uint bit = 1;
	uint l = 0;
	while(bit < x && bit != 0)	// must detect overflow
	{
		l++;
		bit += bit;
	}

	return l;
}


int floor_log2(const float x)
{
	const u32 i = *(u32*)&x;
	u32 biased_exp = (i >> 23) & 0xFF;
	return (int)biased_exp - 127;
}


// round_up_to_pow2 implementation assumes 32-bit int.
// if 64, add "x |= (x >> 32);"
cassert(sizeof(int)*CHAR_BIT == 32);

uint round_up_to_pow2(uint x)
{
	// fold upper bit into lower bits; leaves same MSB set but
	// everything below it 1. adding 1 yields next POT.
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	return x+1;
}


// multiple must be a power of two.
uintptr_t round_up(const uintptr_t n, const uintptr_t multiple)
{
	debug_assert(is_pow2((long)multiple));
	const uintptr_t result = (n + multiple-1) & ~(multiple-1);
	debug_assert(n <= result && result < n+multiple);
	return result;
}

// multiple must be a power of two.
uintptr_t round_down(const uintptr_t n, const uintptr_t multiple)
{
	debug_assert(is_pow2((long)multiple));
	const uintptr_t result = n & ~(multiple-1);
	debug_assert(result <= n && n < result+multiple);
	return result;
}
