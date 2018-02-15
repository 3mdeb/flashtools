/** \file
 * Print a hexdump of values
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include "util.h"

static char
printable(
 	uint8_t c
)
{
	if (isprint(c))
		return (char) c;
	return '.';
}


void
hexdump(
	const uintptr_t base_offset,
	const uint8_t * const buf,
	const size_t len
)
{
	const size_t width = 16;

	for (size_t offset = 0 ; offset < len ; offset += width)
	{
		printf("%08"PRIxPTR":", base_offset + offset);
		for (size_t i = 0 ; i < width ; i++)
		{
			if (i + offset < len)
				printf(" %02x", buf[offset+i]);
			else
				printf("   ");
		}

		printf("  ");

		for (size_t i = 0 ; i < width ; i++)
		{
			if (i + offset < len)
				printf("%c", printable(buf[offset+i]));
			else
				printf(" ");
		}

		printf("\n");
	}
}


#define MEMCPY_N(WIDTH) \
static void \
memcpy_##WIDTH( \
	volatile uint##WIDTH##_t * const dest, \
	const volatile uint##WIDTH##_t * const src, \
	size_t len, \
	mem_op_t op \
) \
{ \
	for (size_t i = 0 ; i < len/sizeof(*dest) ; i ++) \
	{ \
		if (op == MEM_AND) \
			dest[i] &= src[i]; \
		else \
		if (op == MEM_OR) \
			dest[i] |= src[i]; \
		else \
			dest[i] = src[i]; \
		__asm__ __volatile__("mfence"); \
	} \
}

MEMCPY_N(8)
MEMCPY_N(16)
MEMCPY_N(32)
MEMCPY_N(64)

void
memcpy_width(
	volatile void * dest,
	const volatile void * src,
	size_t len, // in bytes
	size_t width, // in bytes, 1,2,4 or 8
	mem_op_t op
)
{
	if (width == 1)
		memcpy_8(dest, src, len, op);
	else
	if (width == 2)
		memcpy_16(dest, src, len, op);
	else
	if (width == 4)
		memcpy_32(dest, src, len, op);
	else
	if (width == 8)
		memcpy_64(dest, src, len, op);
	else
	{
		fprintf(stderr, "width %zu not supported\n", width);
		exit(EXIT_FAILURE);
	}
}