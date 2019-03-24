#ifndef OMEM_H
#define OMEM_H

// Optimized Malloc Interface

typedef struct om_stats
{
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} om_stats;

om_stats *ogetstats();
void oprintstats();

void *omalloc(size_t size);
void ofree(void *item);
void *orealloc(void *prev, size_t bytes);

#endif
