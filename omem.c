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
    char free;
} nu_header;

typedef struct nu_footer
{
    int64_t size;
} nu_footer;

typedef struct nu_free_cell
{
    int64_t size;
    char free;
    struct nu_free_cell *next;
    struct nu_free_cell *prev;
} nu_free_cell;

typedef struct nu_bin
{
    int64_t size;
    struct nu_free_cell *node;
} nu_bin;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//static int bin_length = 8;
static nu_bin bins[8];
static int bin_init = 0;

//static const int64_t CHUNK_SIZE = 2097152;
static const int64_t CHUNK_SIZE = 4096;

static const int64_t CELL_SIZE = (int64_t)sizeof(nu_free_cell);

void * base;

void init_bins()
{
    for (int i = 0; i < 8; i++)
    {
        bins[i].size = 1 << (i + 4); // starting from 32 = 2^5
        bins[i].node = NULL;
    }
    bin_init = 1;
}

static void
nu_free_list_insert(nu_free_cell *cell)
{
    nu_footer *footer = (void *)cell + cell->size - sizeof(nu_footer);
    footer->size = cell->size;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 7; i++)
    {
        if (bins[i].size <= cell->size && bins[i + 1].size > cell->size)
        {
            nu_free_cell *temp = bins[i].node;
            bins[i].node = cell;
            cell->next = temp;
            cell->prev = NULL;
            if (temp != NULL)
            {
                temp->prev = cell;
            }
            pthread_mutex_unlock(&lock);
            return;
        }
    }
    // big ones go to bins[last]
    nu_free_cell *temp = bins[7].node;
    bins[7].node = cell;
    cell->next = temp;
    cell->prev = NULL;
    if (temp != NULL)
    {
        temp->prev = cell;
    }
    pthread_mutex_unlock(&lock);
    return;
}

static nu_free_cell *
free_list_get_cell(int64_t size)
{
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 8; i++)
    {
        if (bins[i].node != NULL && bins[i].size >= size)
        {
            nu_free_cell *temp = bins[i].node;
            bins[i].node = bins[i].node->next;
            pthread_mutex_unlock(&lock);
            return temp;
        }
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

static nu_free_cell *
make_cell()
{
    void *addr = mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(addr, CHUNK_SIZE, MADV_HUGEPAGE);
    nu_free_cell *cell = (nu_free_cell *)addr;
    cell->size = CHUNK_SIZE;
    return cell;
}

void *
omalloc(size_t usize)
{
    char first = 0;
    if (!bin_init)
    {
        first = 1;
        init_bins();
    }
    int64_t size = (int64_t)usize;

    // space for size
    int64_t alloc_size = size + sizeof(nu_header) + sizeof(nu_footer);

    // space for free cell when returned to list
    if (alloc_size < CELL_SIZE)
    {
        alloc_size = CELL_SIZE;
    }

    // TODO: Handle large allocations.
    if (alloc_size > CHUNK_SIZE)
    {
        nu_header *addr = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        madvise(addr, alloc_size, MADV_DONTNEED);
        addr->size = alloc_size;
        addr->free = 0;
        nu_footer *foot = (void *)addr + alloc_size - sizeof(nu_footer);
        return addr + sizeof(int64_t);
    }

    nu_header *cell = (nu_header *)free_list_get_cell(alloc_size);
    if (cell == NULL)
    {
        cell = (nu_header *)make_cell();
    }

    // Return unused portion to free list.
    int64_t rest_size = cell->size - alloc_size;
    if (rest_size >= CELL_SIZE)
    {
        void *addr = (void *)cell;
        nu_free_cell *rest = (nu_free_cell *)(addr + alloc_size);
        rest->size = rest_size;
        rest->free = 1;
        nu_free_list_insert(rest);
    }

    cell->free = 0;
    cell->size = alloc_size;
    nu_footer *footer = (void *)cell + alloc_size - sizeof(nu_footer);
    footer->size = alloc_size;
    if (first) {
        base = cell;
    }
    return ((void *)cell) + sizeof(nu_header);
}

nu_free_cell *
coalesce_list(nu_free_cell *cell)
{
    if (cell == base) {
        return NULL;
    }
    nu_footer *footer_up = (void *)cell - sizeof(nu_footer);
    int64_t size_up = footer_up->size;
    nu_header *header_up = (void *)footer_up + sizeof(nu_footer) - sizeof(size_up);
    nu_header *header_down = (void *)cell + cell->size;
    if (header_up->free)
    {
        header_up->size += cell->size;
        cell = (nu_free_cell *)header_up;
        if (header_down->free)
        {
            nu_free_cell * down = (nu_free_cell *)header_down;
            cell->size += header_down->size;
            if (down->prev) {
                down->prev->next = down->next;
            }
            if (down->next) {
                down->next->prev = down->prev;
            }
        }
        nu_footer *temp = (void *)cell + cell->size;
        temp->size = header_up->size;
        return cell;
    }
    else if (header_down->free) {
        nu_free_cell * down = (nu_free_cell *)header_down;
        cell->size += header_down->size;
        if (down->prev) {
            down->prev->next = cell;
        }
        if (down->next) {
            down->next->prev = cell;
        }
        nu_free_cell * free_cell = (nu_free_cell *)cell;
        free_cell->next = down->next;
        free_cell->prev = down->prev;
        nu_footer *temp = (void *)cell + cell->size;
        temp->size = cell->size;
        return cell;
    }
    return NULL;
}

void ofree(void *addr)
{
    nu_free_cell *cell = addr - sizeof(nu_header);
    cell->free = 1;
    int64_t size = cell->size;

    if (size > CHUNK_SIZE)
    {
        madvise(cell, cell->size, MADV_DONTNEED);
    }
    else
    {
        cell->size = size;
        void * temp = coalesce_list(cell);
        if (temp == NULL) {
            nu_free_list_insert(cell);
        }
    }
}

void *orealloc(void *prev, size_t bytes)
{
    void *newaddr = omalloc(bytes);
    memcpy(newaddr, prev, bytes);
    ofree(prev);
    return newaddr;
}
