
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>

#include "hmalloc.h"
#include <pthread.h>
#include <string.h>

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/


// A node on the free list
typedef struct hm_free_node_t
{
    struct hm_free_node_t *next;
    size_t size;
} hm_free_node_t;

// A header for an allocated block
typedef struct hm_header_t
{
    size_t size;
} hm_header_t;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

hm_free_node_t *free_list = NULL;

// Insert node into free list (sorted by address)
static
void insert_node(hm_free_node_t *node)
{
    if (free_list == NULL)
    {
        free_list = node;
        node->next = NULL;
        return;
    }

    hm_free_node_t *curr_node = free_list;
    hm_free_node_t *prev_node = NULL;

    while (curr_node != NULL && curr_node < node)
    {
        prev_node = curr_node;
        curr_node = curr_node->next;
    }

    if (prev_node == NULL)
    {
        node->next = curr_node;
        free_list = node;
        return;
    }

    prev_node->next = node;
    node->next = curr_node;
}

// Remove node from free list
static
void remove_node(hm_free_node_t *node)
{
    if (free_list == NULL || node == NULL)
    {
        return;
    }

    hm_free_node_t *curr_node = free_list;
    hm_free_node_t *prev_node = NULL;

    while (curr_node != NULL && curr_node != node)
    {
        prev_node = curr_node;
        curr_node = curr_node->next;
    }

    if (prev_node == NULL)
    {
        free_list = curr_node->next;
        return;
    }

    if (curr_node == NULL)
    {
        prev_node->next = NULL;
        return;
    }

    prev_node->next = curr_node->next;
}

// Length of free list
long free_list_length()
{
    hm_free_node_t *curr_node = free_list;
    int i = 0;
    while (curr_node != NULL)
    {
        curr_node = curr_node->next;
        i++;
    }
    return i;
}

// Get allocator stats
hm_stats *
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

// Print allocator stats
void hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

// Ceiling of xx / yy
static size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx)
    {
        return zz;
    }
    else
    {
        return zz + 1;
    }
}

// Map a new page for allocations less than 4K
void *
map_page(size_t size)
{
    size_t remaining = PAGE_SIZE > (size + sizeof(hm_header_t) + sizeof(hm_free_node_t));

    if (remaining == 0)
    {
        size = PAGE_SIZE - sizeof(hm_header_t);
    }

    hm_header_t *header = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    stats.pages_mapped++;

    header->size = size;
    void *alloc_add = (void *)header + sizeof(hm_header_t);
    stats.chunks_allocated++;

    if (remaining != 0)
    {
        hm_free_node_t *free = alloc_add + size;
        free->size = PAGE_SIZE - (size + sizeof(hm_header_t));
        insert_node(free);
    }

    return alloc_add;
}

// Allocate from free list
void *
alloc_free(hm_free_node_t *free_block, size_t size)
{
    remove_node(free_block);

    size_t prev_size = free_block->size;

    size_t remaining = prev_size > (size + sizeof(hm_header_t) + sizeof(hm_free_node_t));

    if (remaining == 0)
    {
        size = prev_size - sizeof(hm_header_t);
    }

    hm_header_t *header = (hm_header_t *)free_block;
    header->size = size;
    void *alloc_add = (void *)header + sizeof(hm_header_t);
    stats.chunks_allocated++;

    if (remaining != 0)
    {
        hm_free_node_t *free = alloc_add + size;
        free->size = prev_size - (size + sizeof(size_t));
        insert_node(free);
    }

    return alloc_add;
}

// Map large allocations over 4K
static
void *
map_large(size_t size)
{
    size_t total = size + sizeof(hm_header_t);
    size_t num_pages = div_up(total, PAGE_SIZE);
    size_t pages_size = num_pages * PAGE_SIZE;

    hm_header_t *header = mmap(0, pages_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    stats.pages_mapped += num_pages;
    void *alloc_add = (void *)header + sizeof(hm_header_t);
    stats.chunks_allocated++;
    header->size = pages_size;

    return alloc_add;
}

// Allocate dynamic memory with mmap
void *
hmalloc(size_t size)
{
    pthread_mutex_lock(&lock);
    if (free_list != NULL)
    {
    }
    if (size <= 0)
    {
        return (void *)0xDEADBEEF;
    }

    if (size > PAGE_SIZE - sizeof(hm_header_t))
    {
        return map_large(size);
    }

    if (free_list == NULL)
    {
        return map_page(size);
    }

    hm_free_node_t *curr_free = free_list;

    while (curr_free != NULL)
    {
        if (curr_free->size > (size + sizeof(size_t)))
        {
            return alloc_free(curr_free, size);
        }

        curr_free = curr_free->next;
    }
    pthread_mutex_unlock(&lock);
    return map_page(size);
}

// Free large allocation over 4K
void free_large(void *item, size_t size)
{
    munmap(item - sizeof(hm_header_t), size);
    size_t num_pages = div_up(size, PAGE_SIZE);
    stats.pages_unmapped += num_pages;
    stats.chunks_freed++;
}

// Coalesce free list nodes
static
void coalesce_free()
{

    hm_free_node_t *curr_node = free_list;
    hm_free_node_t *prev_node = NULL;

    while (curr_node)
    {
        if (prev_node != NULL && (void *)prev_node + prev_node->size == curr_node)
        {
            prev_node->size += curr_node->size;
            prev_node->next = curr_node->next;
        }
        else
        {
            prev_node = curr_node;
        }
        curr_node = curr_node->next;
    }
}

// Free allocated memory
void hfree(void *item)
{
    pthread_mutex_lock(&lock);
    hm_header_t *header = item - sizeof(hm_header_t);

    size_t size = header->size;

    if (size > PAGE_SIZE - sizeof(hm_header_t))
    {
        free_large(item, size);
        return;
    }

    hm_free_node_t *node = (hm_free_node_t *)header;
    node->size = header->size + sizeof(hm_header_t);
    insert_node(node);
    stats.chunks_freed++;
    coalesce_free();
    pthread_mutex_unlock(&lock);
}

void* hrealloc(void* prev, size_t bytes)
{
    void* newaddr = hmalloc(bytes);
    // I haven't fully followed through your code yet but we might need to do:
    //  void* nodepart = (void*)(prev - sizeof(header));
    //  size_t s = *((size_t*)nodepart);
    //  memcpy(newaddr, prev, s);
    memcpy(newaddr, prev, bytes);
    hfree(prev);
    return newaddr;
}
