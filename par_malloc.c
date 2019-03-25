

#include <stdlib.h>
#include <unistd.h>

#include "xmalloc.h"
#include "omem.h"
#include "pthread.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void*
xmalloc(size_t bytes)
{
    return omalloc(bytes);
}

void
xfree(void* ptr)
{
    ofree(ptr);
}

void*
xrealloc(void* prev, size_t bytes)
{
    return orealloc(prev, bytes);
}

