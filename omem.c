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
#include <math.h>

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

typedef struct nu_bin
{
    int64_t size;
    struct nu_free_cell* node;
} nu_bin;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define BIN_LENGTH 64
static nu_bin bins[BIN_LENGTH];
static int bin_init = 0;

static const int64_t CHUNK_SIZE = 4096;
static const int64_t CELL_SIZE = (int64_t)sizeof(nu_free_cell);

static nu_free_cell *nu_free_list = 0;

static long nu_malloc_chunks = 0;
static long nu_free_chunks = 0;

void init_bins() {
    int64_t s = 16;
    for (int i = 0; i < BIN_LENGTH / 2; i++) {
        bins[i].size = s;
        bins[i].node = NULL;
        s += 8;
    }
    for (int i = BIN_LENGTH / 2; i < BIN_LENGTH; i++) {
        bins[i].size = 1 << (i+9); // starting from 512 = 2^9
        bins[i].node = NULL;
    }
    bin_init = 1;
}

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
    // printf("%s size: %ld\n", "cell", cell->size);
    nu_footer* footer = (void *)cell + cell->size - sizeof(nu_footer);
    footer->size = cell->size;
    for (int i = 0; i < BIN_LENGTH - 1; i++) {
        if (bins[i].size <= cell->size && bins[i+1].size > cell->size) {
            nu_free_cell *temp = bins[i].node;
            bins[i].node = cell;
            cell->next = temp;
            cell->prev = NULL;
            if (temp != NULL) {
                temp->prev = cell;
            }
            return;
        }
    }
    // big ones go to bins[last]
    nu_free_cell *temp = bins[BIN_LENGTH - 1].node;
    bins[BIN_LENGTH - 1].node = cell;
    cell->next = temp;
    cell->prev = NULL;
    if (temp != NULL) {
        temp->prev = cell;
    }
    return;
}

static nu_free_cell *
free_list_get_cell(int64_t size)
{
    for (int i = 0; i < BIN_LENGTH; i++) {
        if (bins[i].node != NULL && bins[i].size >= size) {
            nu_free_cell *temp = bins[i].node;
            bins[i].node = bins[i].node->next;
            return temp;
        }
    }
    return NULL;
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
    if (!bin_init) {
        init_bins();
    }
    pthread_mutex_lock(&lock);
    int64_t size = (int64_t)usize;

    // space for size
    int64_t alloc_size = size + (sizeof(int64_t));

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
        pthread_mutex_unlock(&lock);
        return addr + sizeof(int64_t);
    }

    nu_free_cell *cell = free_list_get_cell(alloc_size);
    if (cell == NULL)
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
    nu_footer* footer = (void *)cell + alloc_size - sizeof(nu_footer);
    footer->size = alloc_size;
    pthread_mutex_unlock(&lock);
    return ((void *)cell) + sizeof(nu_header);
}

void ofree(void *addr)
{
    pthread_mutex_lock(&lock);
    nu_free_cell *cell = (nu_free_cell *)(addr - sizeof(int64_t));
    int64_t size = *((int64_t *)cell);
    cell->size = size;
    cell->prev = NULL;
    cell->next = NULL;

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
    nu_header* nodepart = (void*)prev - sizeof(nu_header);
    size_t s = nodepart->size;
    memcpy(newaddr, prev, s);
    ofree(prev);
    return newaddr;
}
