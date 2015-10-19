/*
 * Ebizzy - replicate a large ebusiness type of workload.
 *
 * Written by Valerie Henson <val@nmt.edu>
 *
 * Copyright 2006 - 2007 Intel Corporation
 * Copyright 2007 Valerie Henson <val@nmt.edu>
 *
 * Rodrigo Rubira Branco <rrbranco@br.ibm.com> - HP/BSD/Solaris port and some
 *  						 new features
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

/*
 * This program is designed to replicate a common web search app
 * workload.  A lot of search applications have the basic pattern: Get
 * a request to find a certain record, index into the chunk of memory
 * that contains it, copy it into another chunk, then look it up via
 * binary search.  The interesting parts of this workload are:
 *
 * Large working set
 * Data alloc/copy/free cycle
 * Unpredictable data access patterns
 *
 * Fiddle with the command line options until you get something
 * resembling the kind of workload you want to investigate.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <malloc.h>

/*
 * Command line options
 */

static unsigned int chunks;
static unsigned int use_permissions;
static unsigned int use_holes;
static unsigned int random_size;
static unsigned int chunk_size;
static unsigned int linear;
static unsigned int touch_pages;

/*
 * Other global variables
 */

typedef size_t record_t;
static unsigned int record_size = sizeof (record_t);
static record_t **mem;
static char **hole_mem;
static unsigned int page_size;

/*
 * Read options, check them, and set some defaults.
 */

static void
read_options(void)
{
	page_size = getpagesize();

	/*
	 * Set some defaults.  These are currently tuned to run in a
	 * reasonable amount of time on my laptop.
	 *
	 * We could set the static defaults in the declarations, but
	 * then the defaults would be split between here and the top
	 * of the file, which is annoying.
	 */
	chunks = 10;
	chunk_size = record_size * 64 * 1024;

	if (chunk_size < record_size) {
		fprintf(stderr, "Chunk size %u smaller than record size %u\n",
			chunk_size, record_size);
		exit(1);
	}
}

static void
touch_mem(char *dest, size_t size)
{
       int i;
       if (touch_pages) {
               for (i = 0; i < size; i += page_size)
                       *(dest + i) = 0xff;
       }
}

static void *
alloc_mem(size_t size)
{
	char *p;
	int err = 0;

	p = malloc(size);
	if (p == NULL)
		err = 1;

	if (err) {
		fprintf(stderr, "Couldn't allocate %zu bytes, try smaller "
			"chunks or size options\n"
			"Using -n %u chunks and -s %u size\n",
			size, chunks, chunk_size);
		exit(1);
	}

	return (p);
}

static void
allocate(void)
{
	int i;

	mem = alloc_mem(chunks * sizeof (record_t *));

	if (use_holes)
		hole_mem = alloc_mem(chunks * sizeof (record_t *));


	for (i = 0; i < chunks; i++) {
		mem[i] = (record_t *) alloc_mem(chunk_size);
		/* Prevent coalescing using holes */
		if (use_holes)
			hole_mem[i] = alloc_mem(page_size);
	}

	/* Free hole memory */
	if (use_holes)
		for (i = 0; i < chunks; i++)
			free(hole_mem[i]);
}

static void
write_pattern(void)
{
	int i, j;

	for (i = 0; i < chunks; i++) {
		for(j = 0; j < chunk_size / record_size; j++)
			mem[i][j] = (record_t) j;
		/* Prevent coalescing by alternating permissions */
		if (use_permissions && (i % 2) == 0)
			mprotect((void *) mem[i], chunk_size, PROT_READ);
	}
}

static void *
linear_search(record_t key, record_t *base, size_t size)
{
	record_t *p;
	record_t *end = base + (size / record_size);

	for(p = base; p < end; p++)
		if (*p == key)
			return p;
	return NULL;
}

static int
compare(const void *p1, const void *p2)
{
	return (* (record_t *) p1 - * (record_t *) p2);
}

/*
 * Stupid ranged random number function.  We don't care about quality.
 *
 * Inline because it's starting to be a scaling issue.
 */

static inline unsigned int
rand_num(unsigned int max, unsigned int state)
{
	state = state * 1103515245 + 12345;
	return ((state/65536) % max);
}

/*
 * This function is the meat of the program; the rest is just support.
 *
 * In this function, we randomly select a memory chunk, copy it into a
 * newly allocated buffer, randomly select a search key, look it up,
 * then free the memory.  An option tells us to allocate and copy a
 * randomly sized chunk of the memory instead of the whole thing.
 *
 * Linear search provided for sanity checking.
 *
 */

void search_mem(volatile long *idx, long *p, int n)
{
	record_t key, *found;
	record_t *src, *copy;
	unsigned int chunk;
	size_t copy_size = chunk_size;
	unsigned int state = 0;

	while (1) {
		chunk = rand_num(chunks, state);
		src = mem[chunk];
		/*
		 * If we're doing random sizes, we need a non-zero
		 * multiple of record size.
		 */
		if (random_size)
			copy_size = (rand_num(chunk_size / record_size, state)
				     + 1) * record_size;
		copy = alloc_mem(copy_size);

		if ( touch_pages ) {
			touch_mem((char *) copy, copy_size);
		} else {
		
			memcpy(copy, src, copy_size);

			key = rand_num(copy_size / record_size, state);

			if (linear)
				found = linear_search(key, copy, copy_size);
			else
				found = bsearch(&key, copy, copy_size / record_size,
					record_size, compare);
	
				/* Below check is mainly for memory corruption or other bug */
			if (found == NULL) {
				fprintf(stderr, "Couldn't find key %zd\n", key);
				exit(1);
			}
		} /* end if ! touch_pages */

		free(copy);

		p[*idx * n]++;
	}
}

void ebizzy(volatile long *idx, long *p, int n)
{
	read_options();

	allocate();

	write_pattern();

	search_mem(idx, p, n);
}
