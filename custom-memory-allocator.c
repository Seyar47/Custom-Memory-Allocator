/*
  Enhanced Memory Allocator
  
  A custom memory allocator implementation with the following features:
  - Block splitting to reduce memory fragmentation
  - Block coalescing to combine adjacent free blocks
  - Memory alignment support
  - Metadata protection using sentinels and stuff
  - Debugging and statistics features
  - Error handling and validation
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define HEAP_SIZE (4 * 1024)  // 4KB heap
#define ALIGNMENT 8           // Memory alignment (8 bytes)
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define MIN_BLOCK_SIZE ALIGN(sizeof(Block) + 8)  // Minimum block size that can be allocated
#define SENTINEL_VALUE 0xCAFEBABE  // some value to detect heap corruption

// Debug options
#define DEBUG_LEVEL 1  // 0=off, 1=basic, 2=verbose
#define ENABLE_STATS 1  // Track allocation statistics

// Type definitions 
typedef struct Block {
    uint32_t sentinel_start;   // Detect corruption at start of block
    size_t size;               // size of the usable memory (not including the header)
    bool free;                 // whether the block is free or not
    struct Block *prev;        // previous block in list (doubly linked)
    struct Block *next;        // Next block in list
    uint32_t sentinel_end;     // detect corruption at end of block
    // Actual data follows this header
} Block;

// Global state 
static char heap[HEAP_SIZE];
static Block *free_list = NULL;  // head of the free list
static Block *used_list = NULL;  // head of the used list
static bool initialized = false;

#if ENABLE_STATS
// statistics tracking 
typedef struct {
    size_t allocated_bytes;    // currently allocated bytes
    size_t free_bytes;         // currently free bytes
    size_t allocated_blocks;   // number of allocated blocks
    size_t free_blocks;        // number of free blocks
    size_t total_allocations;  // total or lifetime allocations
    size_t total_frees;        // total or lifetime frees
    size_t failed_allocations; // failed allocation requests
} AllocStats;

static AllocStats stats = {0};

void print_allocation_stats(void) {
    printf("\n=== Memory Allocator Statistics ===\n");
    printf("Allocated: %zu bytes in %zu blocks\n", stats.allocated_bytes, stats.allocated_blocks);
    printf("Free: %zu bytes in %zu blocks\n", stats.free_bytes, stats.free_blocks);
    printf("Total allocations: %zu (failed: %zu)\n", stats.total_allocations, stats.failed_allocations);
    printf("Total frees: %zu\n", stats.total_frees);
    printf("Fragmentation: %.2f%%\n", 
        stats.free_blocks > 0 ? 
        (100.0 - ((float)stats.free_bytes / (float)stats.free_blocks) / ((float)(stats.allocated_bytes + stats.free_bytes) / (float)(stats.allocated_blocks + stats.free_blocks)) * 100.0) : 
        0.0);
    printf("================================\n");
}
#endif

// declarations
static void validate_block(Block *block, const char *location);
static void check_heap_integrity(void);
static void add_to_free_list(Block *block);
static void remove_from_free_list(Block *block);
static void add_to_used_list(Block *block);
static void remove_from_used_list(Block *block);
static Block *find_best_fit_block(size_t size);
static void split_block(Block *block, size_t size);
static bool try_merge_with_neighbors(Block *block);


// initialize the memory allocator.
// have to be called before any allocation operations.

void initialize(void) {
    if (initialized) return;
    
    memset(heap, 0, HEAP_SIZE);
    
    Block *first_block = (Block*)heap;
    first_block->sentinel_start = SENTINEL_VALUE;
    first_block->size = HEAP_SIZE - sizeof(Block);
    first_block->free = true;
    first_block->prev = NULL;
    first_block->next = NULL;
    first_block->sentinel_end = SENTINEL_VALUE;
    
    // Add to free list
    free_list = first_block;
    
    #if ENABLE_STATS
    stats.free_bytes = first_block->size;
    stats.free_blocks = 1;
    #endif
    
    initialized = true;
    
    #if DEBUG_LEVEL > 0
    printf("Memory allocator initialized with %zu bytes\n", HEAP_SIZE);
    #endif
}

/**
Allocate memory of specified size.  
 @param size Size of memory to allocate in bytes
 @return Pointer to allocated memory or NULL if allocation fails
 */
void *my_malloc(size_t requested_size) {
    if (!initialized) initialize();
    
    // Validate input
    if (requested_size == 0) return NULL;
    
    #if ENABLE_STATS
    stats.total_allocations++;
    #endif
    
    // Align the requested size
    size_t aligned_size = ALIGN(requested_size);
    
    #if DEBUG_LEVEL > 1
    printf("Allocation request: %zu bytes (aligned to %zu)\n", requested_size, aligned_size);
    #endif
    
    check_heap_integrity();
    
    // find a suitable block
    Block *block = find_best_fit_block(aligned_size);
    if (!block) {
        #if ENABLE_STATS
        stats.failed_allocations++;
        #endif
        
        #if DEBUG_LEVEL > 0
        printf("Allocation failed: no suitable block found for %zu bytes\n", aligned_size);
        #endif
        
        return NULL;
    }
    
    // split the block if it's way way larger than needed
    if (block->size >= aligned_size + MIN_BLOCK_SIZE) {
        split_block(block, aligned_size);
    }
    
    // mark the block as used
    block->free = false;
    remove_from_free_list(block);
    add_to_used_list(block);
    
    #if ENABLE_STATS
    stats.allocated_bytes += block->size;
    stats.allocated_blocks++;
    stats.free_bytes -= block->size;
    stats.free_blocks--;
    #endif
    
    // return pointer to the usable memory area (after the header)
    void *data_ptr = (void*)((char*)block + sizeof(Block));
    
    #if DEBUG_LEVEL > 1
    printf("Allocated %zu bytes at %p (block %p)\n", block->size, data_ptr, block);
    #endif

    // clear the alloceted memeroy
    memset(data_ptr, 0, block->size);
    
    return data_ptr;
}

/**
  Free previously allocated memory.
  
  @param ptr Pointer to memory to free
 */
void my_free(void *ptr) {
    if (!initialized) initialize();
    
    // validate input
    if (!ptr) return;
    
    check_heap_integrity();
    
    // get the block header by going back from the data pointer
    Block *block = (Block*)((char*)ptr - sizeof(Block));
    
    validate_block(block, "my_free");
    
    if (block->free) {
        fprintf(stderr, "Double free detected at %p\n", ptr);
        return;
    }
    
    #if DEBUG_LEVEL > 1
    printf("Freeing %zu bytes at %p (block %p)\n", block->size, ptr, block);
    #endif
    
    // update allocation tracking
    #if ENABLE_STATS
    stats.allocated_bytes -= block->size;
    stats.allocated_blocks--;
    stats.free_bytes += block->size;
    stats.free_blocks++;
    stats.total_frees++;
    #endif
    
    // again mark block as free
    block->free = true;
    remove_from_used_list(block);
    add_to_free_list(block);
    
    // try to merge with the free blocks that are adjacent
    try_merge_with_neighbors(block);
}

/**
  Get the size of an allocated block.
 
  @param ptr Pointer to allocated memory
  @return Size of the allocated block or 0 if invalid
 */
size_t my_malloc_size(void *ptr) {
    if (!ptr || !initialized) return 0;
    
    Block *block = (Block*)((char*)ptr - sizeof(Block));
    
    if (block->sentinel_start != SENTINEL_VALUE || 
        block->sentinel_end != SENTINEL_VALUE) {
        fprintf(stderr, "Invalid block or corrupted memory at %p\n", ptr);
        return 0;
    }
    
    return block->size;
}

/**
  Reallocate memory block to a new size.
  
  @param ptr Pointer to previously allocated memory
  @param size New size for the memory block
  @return Pointer to new memory block or NULL on failure
 */
void *my_realloc(void *ptr, size_t size) {
    // handle edge cases and stuff
    if (!ptr) return my_malloc(size);
    if (size == 0) {
        my_free(ptr);
        return NULL;
    }
    
    // get the current block size
    size_t current_size = my_malloc_size(ptr);
    if (current_size == 0) return NULL;  // pointer is invalid
    
    // If new size is smaller, then we can just resize the current block
    if (ALIGN(size) <= current_size) {
        Block *block = (Block*)((char*)ptr - sizeof(Block));
        
        // Only split if we save alot of  space
        if (current_size >= ALIGN(size) + MIN_BLOCK_SIZE) {
            split_block(block, ALIGN(size));
        }
        
        return ptr;
    }
    
    // otherwise we can just allocate new block and copy the data
    void *new_ptr = my_malloc(size);
    if (!new_ptr) return NULL;
    
    // copy the old stuff to new block
    memcpy(new_ptr, ptr, current_size);
    
    // free old block
    my_free(ptr);
    
    return new_ptr;
}

/**
 Allocate memory and initialize it to zero.
  
  @param count Number of elements
  @param size Size of each element
  @return Pointer to allocated memory or NULL on failure
 */
void *my_calloc(size_t count, size_t size) {
    size_t total_size;
    
    // check for multiplication overflow
    if (count > 0 && size > SIZE_MAX / count) {
        return NULL;
    }
    
    total_size = count * size;
    void *ptr = my_malloc(total_size);
    
    // my_malloc already zeros the memory
    return ptr;
}


 // find a block with the best fit strategy
 
static Block *find_best_fit_block(size_t size) {
    Block *current = free_list;
    Block *best_fit = NULL;
    size_t smallest_diff = SIZE_MAX;
    
    while (current) {
        validate_block(current, "find_best_fit");
        
        if (current->free && current->size >= size) {
            size_t diff = current->size - size;
            
            if (diff == 0) {
                return current;  // Perfect fit, we can definelty use that
            } else if (diff < smallest_diff) {
                smallest_diff = diff;
                best_fit = current;
            }
        }
        
        current = current->next;
    }
    
    return best_fit;
}

// Split a block into two blocks
 
static void split_block(Block *block, size_t size) {
    validate_block(block, "split_block_before");
    
    size_t original_size = block->size;
    size_t remaining_size = original_size - size - sizeof(Block);
    
    // only split if the rest of the size is worth creating a new block 
    if (remaining_size < MIN_BLOCK_SIZE) return;
    
    // fix and adjust size of current block
    block->size = size;
    
    Block *new_block = (Block*)((char*)block + sizeof(Block) + size);
    new_block->sentinel_start = SENTINEL_VALUE;
    new_block->size = remaining_size;
    new_block->free = true;
    new_block->prev = NULL;
    new_block->next = NULL;
    new_block->sentinel_end = SENTINEL_VALUE;
    
    // add new block to free list
    add_to_free_list(new_block);
    
    #if ENABLE_STATS
    stats.free_blocks++;
    #endif
    
    #if DEBUG_LEVEL > 1
    printf("Split block %p (size %zu) -> new block %p (size %zu)\n", 
           block, size, new_block, remaining_size);
    #endif
    
    validate_block(block, "split_block_after1");
    validate_block(new_block, "split_block_after2");
}

/**
  we can try to merge a block with its adjacent blocks if they're free
  @return true if any merge occurred
 */
static bool try_merge_with_neighbors(Block *block) {
    bool merged = false;
    
    // Check if the block is inside our heap bounds
    if ((char*)block < heap || (char*)block >= heap + HEAP_SIZE) {
        return false;
    }
    
    Block *next_physical = (Block*)((char*)block + sizeof(Block) + block->size);
    if ((char*)next_physical < heap + HEAP_SIZE - sizeof(Block)) {
        validate_block(next_physical, "merge_check_next");
        
        // If next block is free, merge with it
        if (next_physical->free) {
            #if DEBUG_LEVEL > 1
            printf("Merging block %p with next %p\n", block, next_physical);
            #endif
            
            // update the size to include next block
            block->size += sizeof(Block) + next_physical->size;
            
            remove_from_free_list(next_physical);
            
            #if ENABLE_STATS
            stats.free_blocks--;
            #endif
            
            merged = true;
        }
    }
    
    return merged;
}


//  add a block to the free list, preserving address order
static void add_to_free_list(Block *block) {
    if (!block) return;
    
    block->free = true;
    
    if (!free_list) {
        free_list = block;
        block->prev = NULL;
        block->next = NULL;
        return;
    }
    
    // Insert in address order for better coalescing
    Block *current = free_list;
    Block *prev = NULL;
    
    while (current && current < block) {
        prev = current;
        current = current->next;
    }
    
    // Insert block
    if (!prev) {
        // put it in at beginning
        block->prev = NULL;
        block->next = free_list;
        free_list->prev = block;
        free_list = block;
    } else {
        // Insert in middle or end
        block->prev = prev;
        block->next = current;
        prev->next = block;
        if (current) current->prev = block;
    }
}

static void remove_from_free_list(Block *block) {
    if (!block) return;
    
    // Update neighbors
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    
    // update the head if needed
    if (free_list == block) free_list = block->next;
    
    block->prev = NULL;
    block->next = NULL;
}

static void add_to_used_list(Block *block) {
    if (!block) return;
    
    block->free = false;
    block->prev = NULL;
    block->next = used_list;
    
    if (used_list) used_list->prev = block;
    used_list = block;
}


//   Remove a block from the used list.
 
static void remove_from_used_list(Block *block) {
    if (!block) return;
    
    // Update neighbors
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    
    if (used_list == block) used_list = block->next;
    
    block->prev = NULL;
    block->next = NULL;
}

//   validate a block's structure and detect corruptions

static void validate_block(Block *block, const char *location) {
    if (!block) return;
    
    if ((char*)block < heap || (char*)block >= heap + HEAP_SIZE) {
        fprintf(stderr, "MEMORY ERROR at %s: Block %p is outside heap bounds\n", 
                location, block);
        return;
    }
    
    if (block->sentinel_start != SENTINEL_VALUE) {
        fprintf(stderr, "MEMORY CORRUPTION at %s: Block %p start sentinel corrupted\n", 
                location, block);
    }
    
    if (block->sentinel_end != SENTINEL_VALUE) {
        fprintf(stderr, "MEMORY CORRUPTION at %s: Block %p end sentinel corrupted\n", 
                location, block);
    }
    
    if (block->size > HEAP_SIZE) {
        fprintf(stderr, "MEMORY ERROR at %s: Block %p has invalid size %zu\n", 
                location, block, block->size);
    }
}


static void check_heap_integrity(void) {
    #if DEBUG_LEVEL < 2
    return;  // Skip checks that are not in debug mode
    #endif
    
    Block *current;
    size_t free_count = 0;
    size_t used_count = 0;
    size_t free_bytes = 0;
    size_t used_bytes = 0;
    
    // check free list
    current = free_list;
    while (current) {
        validate_block(current, "heap_check_free");
        if (!current->free) {
            fprintf(stderr, "HEAP ERROR: Block in free list is marked as used\n");
        }
        free_count++;
        free_bytes += current->size;
        current = current->next;
    }
    
    // check used list
    current = used_list;
    while (current) {
        validate_block(current, "heap_check_used");
        if (current->free) {
            fprintf(stderr, "HEAP ERROR: Block in used list is marked as free\n");
        }
        used_count++;
        used_bytes += current->size;
        current = current->next;
    }
    
    #if ENABLE_STATS
    if (stats.free_blocks != free_count || stats.allocated_blocks != used_count ||
        stats.free_bytes != free_bytes || stats.allocated_bytes != used_bytes) {
        fprintf(stderr, "HEAP ERROR: Stats mismatch\n");
        fprintf(stderr, "Expected: %zu free blocks (%zu bytes), %zu used blocks (%zu bytes)\n",
                stats.free_blocks, stats.free_bytes, stats.allocated_blocks, stats.allocated_bytes);
        fprintf(stderr, "Actual: %zu free blocks (%zu bytes), %zu used blocks (%zu bytes)\n",
                free_count, free_bytes, used_count, used_bytes);
    }
    #endif
}

// print a visual representation of heap debugging
void print_heap_map(void) {
    printf("\n===== HEAP MAP =====\n");
    
    char *heap_end = heap + HEAP_SIZE;
    char *current = heap;
    
    while (current < heap_end) {
        Block *block = (Block*)current;
        
        if (block->sentinel_start != SENTINEL_VALUE) {
            printf("[CORRUPTED at %p]", current);
            break;
        }
        
        printf("[%p: %zu bytes, %s]\n", 
               block, 
               block->size, 
               block->free ? "FREE" : "USED");
        
        current = (char*)(block + 1) + block->size;
        
        if (current >= heap_end) break;
    }
    
    printf("====================\n");
}

// Example usage:
int main() {
    initialize();
    
    // Test basic allocation
    int *a = (int*)my_malloc(sizeof(int) * 10);
    if (a) {
        for (int i = 0; i < 10; i++) {
            a[i] = i;
        }
        printf("Allocated array of 10 ints\n");
    }
    
    // Test realloc
    a = (int*)my_realloc(a, sizeof(int) * 20);
    if (a) {
        for (int i = 10; i < 20; i++) {
            a[i] = i;
        }
        printf("Reallocated to array of 20 ints\n");
    }
    
    // Test calloc
    double *b = (double*)my_calloc(5, sizeof(double));
    if (b) {
        printf("Allocated array of 5 doubles (calloc)\n");
    }
    
    // Print heap state
    print_heap_map();
    
    // Free memory
    my_free(a);
    my_free(b);
    
    printf("After freeing:\n");
    print_heap_map();
    
    #if ENABLE_STATS
    print_allocation_stats();
    #endif
    
    return 0;
}

