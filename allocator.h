#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define HEAP_SIZE (1024 * 1024) 
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define MIN_BLOCK_SIZE ALIGN(sizeof(Block) + 16)
#define SENTINEL_VALUE 0xCAFEBABE
#define FOOTER_SENTINEL 0xDEADBEEF

#define NUM_SIZE_CLASSES 8
#define SIZE_CLASS_1 32
#define SIZE_CLASS_2 64
#define SIZE_CLASS_3 128
#define SIZE_CLASS_4 256
#define SIZE_CLASS_5 512
#define SIZE_CLASS_6 1024
#define SIZE_CLASS_7 2048

#define THREAD_SAFE 1
#define DEBUG_LEVEL 1
#define ENABLE_STATS 1
#define MEMORY_GUARDS 1
#define GUARD_VALUE 0xFE
#define BOUNDARY_TAGS 1
#define CACHE_LOCALITY 1
#define LEAK_DETECTION 1

typedef struct BlockFooter {
    size_t size;
    bool free;
    uint32_t sentinel;
} BlockFooter;

typedef struct Block {
    uint32_t sentinel_start;
    size_t size;
    bool free;
    struct Block *prev;
    struct Block *next;
    uint32_t request_size;
    void *address_tag;
    uint32_t alloc_id;
    uint32_t sentinel_end;
} Block;

#if LEAK_DETECTION
typedef struct AllocationRecord {
    void *ptr;
    size_t size;
    uint32_t alloc_id;
    const char *file;
    int line;
    struct AllocationRecord *next;
} AllocationRecord;
#endif

#if ENABLE_STATS
typedef struct {
    size_t allocated_bytes;
    size_t free_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t total_allocations;
    size_t total_frees;
    size_t failed_allocations;
    size_t fragmentation_count;
    size_t largest_free_block;
    size_t smallest_free_block;
    clock_t total_alloc_time;
    clock_t total_free_time;
    size_t requested_bytes;
    size_t overhead_bytes;
    size_t class_usage[NUM_SIZE_CLASSES];
} AllocStats;
#endif

extern char *heap;
extern Block *free_lists[NUM_SIZE_CLASSES];
extern Block *used_list;
extern bool initialized;
#if LEAK_DETECTION
extern AllocationRecord *allocation_records;
#endif
#if ENABLE_STATS
extern AllocStats stats;
#endif
#if THREAD_SAFE
extern pthread_mutex_t heap_mutex;
#define LOCK() pthread_mutex_lock(&heap_mutex)
#define UNLOCK() pthread_mutex_unlock(&heap_mutex)
#else
#define LOCK()
#define UNLOCK()
#endif

void initialize(void);
void cleanup(void);
void *my_malloc_internal(size_t size, const char *file, int line);
void my_free(void *ptr);
void *my_realloc_internal(void *ptr, size_t size, const char *file, int line);
void *my_calloc_internal(size_t count, size_t size, const char *file, int line);
size_t my_malloc_size(void *ptr);

void print_allocation_stats(void);
void print_heap_map(void);
void visualize_memory(void);
void check_for_leaks(void);
void get_memory_stats(float *used_percent, float *free_percent, float *overhead_percent, float *fragmentation);

#define my_malloc(size) my_malloc_internal(size, __FILE__, __LINE__)
#define my_realloc(ptr, size) my_realloc_internal(ptr, size, __FILE__, __LINE__)
#define my_calloc(count, size) my_calloc_internal(count, size, __FILE__, __LINE__)

#endif