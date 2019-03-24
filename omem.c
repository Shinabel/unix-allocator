// CS3650 CH02 starter code
// Spring 2019
//
// Author: Nat Tuck
//
// Once you've read this, you're done with the simple allocator homework.

#include <stdint.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include "omem.h"

typedef struct nu_header
{
    int64_t size;
} nu_header;

typedef struct nu_footer
{
    int64_t size;
} nu_footer;

typedef struct nu_free_cell
{
    int64_t size;
    struct nu_free_cell *next;
    struct nu_free_cell *prev;
} nu_free_cell;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static const int64_t CHUNK_SIZE = 65536;
static const int64_t CELL_SIZE = (int64_t)sizeof(nu_free_cell);

static nu_free_cell *nu_free_list = 0;

static long nu_malloc_chunks = 0;
static long nu_free_chunks = 0;

int64_t
nu_free_list_length()
{
    int len = 0;

    for (nu_free_cell *pp = nu_free_list; pp != 0; pp = pp->next)
    {
        len++;
    }

    return len;
}

void nu_print_free_list()
{
    nu_free_cell *pp = nu_free_list;
    printf("= Free list: =\n");

    for (; pp != 0; pp = pp->next)
    {
        printf("%lx: (cell %ld %lx)\n", (int64_t)pp, pp->size, (int64_t)pp->next);
    }
}

static void
nu_free_list_coalesce()
{
    nu_free_cell *pp = nu_free_list;
    int free_chunk = 0;

    while (pp != 0 && pp->next != 0)
    {
        if (((int64_t)pp) + pp->size == ((int64_t)pp->next))
        {
            pp->size += pp->next->size;
            pp->next = pp->next->next;
        }

        pp = pp->next;
    }
}

static void
nu_free_list_insert(nu_free_cell *cell)
{
    if (nu_free_list == 0 || ((uint64_t)nu_free_list) > ((uint64_t)cell))
    {
        cell->next = nu_free_list;
        nu_free_list = cell;
        return;
    }

    nu_free_cell *pp = nu_free_list;

    while (pp->next != 0 && ((uint64_t)pp->next) < ((uint64_t)cell))
    {
        pp = pp->next;
    }

    cell->next = pp->next;
    pp->next = cell;

    nu_free_list_coalesce();
}

static nu_free_cell *
free_list_get_cell(int64_t size)
{
    nu_free_cell **prev = &nu_free_list;

    for (nu_free_cell *pp = nu_free_list; pp != 0; pp = pp->next)
    {
        if (pp->size >= size)
        {
            *prev = pp->next;
            return pp;
        }
        prev = &(pp->next);
    }
    return 0;
}

static nu_free_cell *
make_cell()
{
    void *addr = mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    nu_free_cell *cell = (nu_free_cell *)addr;
    cell->size = CHUNK_SIZE;
    return cell;
}

void *
omalloc(size_t usize)
{
    pthread_mutex_lock(&lock);
    int64_t size = (int64_t)usize;

    // space for size
    int64_t alloc_size = size + sizeof(int64_t);

    // space for free cell when returned to list
    if (alloc_size < CELL_SIZE)
    {
        alloc_size = CELL_SIZE;
    }

    // TODO: Handle large allocations.
    if (alloc_size > CHUNK_SIZE)
    {
        void *addr = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        *((int64_t *)addr) = alloc_size;
        nu_malloc_chunks += 1;
        return addr + sizeof(int64_t);
    }

    nu_free_cell *cell = free_list_get_cell(alloc_size);
    if (!cell)
    {
        cell = make_cell();
    }

    // Return unused portion to free list.
    int64_t rest_size = cell->size - alloc_size;
    if (rest_size >= CELL_SIZE)
    {
        void *addr = (void *)cell;
        nu_free_cell *rest = (nu_free_cell *)(addr + alloc_size);
        rest->size = rest_size;
        nu_free_list_insert(rest);
    }

    *((int64_t *)cell) = alloc_size;
    pthread_mutex_unlock(&lock);
    return ((void *)cell) + sizeof(int64_t);
}

void ofree(void *addr)
{
    pthread_mutex_lock(&lock);
    nu_free_cell *cell = (nu_free_cell *)(addr - sizeof(int64_t));
    int64_t size = *((int64_t *)cell);

    if (size > CHUNK_SIZE)
    {
        nu_free_chunks += 1;
        munmap((void *)cell, size);
    }
    else
    {
        cell->size = size;
        nu_free_list_insert(cell);
    }
    pthread_mutex_unlock(&lock);
}

void *orealloc(void *prev, size_t bytes)
{
    void *newaddr = omalloc(bytes);
    memcpy(newaddr, prev, bytes);
    ofree(prev);
    return newaddr;
}
