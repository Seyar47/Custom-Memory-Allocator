#include "allocator.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

char *heap = NULL;
Block *free_lists[NUM_SIZE_CLASSES] = {NULL};
Block *used_list = NULL;
bool initialized = false;
static uint32_t next_alloc_id = 1;

#if LEAK_DETECTION
AllocationRecord *allocation_records = NULL;
#endif

#if ENABLE_STATS
AllocStats stats = {0};
#endif

#if THREAD_SAFE
pthread_mutex_t heap_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/* Forward Declarations for Internal Helpers */
static void validate_block(Block *block, const char *location);
static void check_heap_integrity(void);
static void add_to_free_list(Block *block);
static void remove_from_free_list(Block *block);
static void add_to_used_list(Block *block);
static void remove_from_used_list(Block *block);
static Block *find_best_fit_block(size_t size);
static void split_block(Block *block, size_t size);
static bool try_merge_with_neighbors(Block *block);
static int get_size_class(size_t size);
static BlockFooter *get_footer(Block *block);
static Block *get_prev_physical_block(Block *block);
static void set_block_footer(Block *block);
static void *get_block_data(Block *block);
static void add_guard_bytes(void *ptr, size_t size);
static bool check_guard_bytes(void *ptr, size_t size);
#if ENABLE_STATS
static void update_fragmentation_stats(void);
#endif

/* --- CORE IMPLEMENTATION --- */

void initialize(void) {
    LOCK();
    if (initialized) { UNLOCK(); return; }
    
    heap = (char*)malloc(HEAP_SIZE);
    if (!heap) {
        fprintf(stderr, "Failed to initialize heap of size %d\n", HEAP_SIZE);
        UNLOCK();
        return;
    }
    
    memset(heap, 0, HEAP_SIZE);
    Block *first_block = (Block*)heap;
    first_block->sentinel_start = SENTINEL_VALUE;
    first_block->size = HEAP_SIZE - sizeof(Block) - (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0);
    first_block->free = true;
    first_block->prev = NULL;
    first_block->next = NULL;
    first_block->request_size = 0;
    first_block->address_tag = NULL;
    first_block->alloc_id = 0;
    first_block->sentinel_end = SENTINEL_VALUE;
    
    #if BOUNDARY_TAGS
    set_block_footer(first_block);
    #endif
    
    add_to_free_list(first_block);
    #if ENABLE_STATS
    stats.free_bytes = first_block->size;
    stats.free_blocks = 1;
    stats.largest_free_block = first_block->size;
    stats.smallest_free_block = first_block->size;
    #endif
    
    initialized = true;
    #if DEBUG_LEVEL > 0
    printf("Memory allocator initialized with %d bytes at %p\n", HEAP_SIZE, heap);
    #endif
    UNLOCK();
}

void cleanup(void) {
    if (!initialized) return;
    LOCK();
    
    #if LEAK_DETECTION
    while (allocation_records) {
        AllocationRecord *next = allocation_records->next;
        free(allocation_records);
        allocation_records = next;
    }
    #endif
    
    free(heap);
    heap = NULL;
    
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        free_lists[i] = NULL;
    }
    used_list = NULL;
    initialized = false;
    UNLOCK();
    #if DEBUG_LEVEL > 0
    printf("Memory allocator cleaned up\n");
    #endif
}

static int get_size_class(size_t size) {
    if (size <= SIZE_CLASS_1) return 0;
    if (size <= SIZE_CLASS_2) return 1;
    if (size <= SIZE_CLASS_3) return 2;
    if (size <= SIZE_CLASS_4) return 3;
    if (size <= SIZE_CLASS_5) return 4;
    if (size <= SIZE_CLASS_6) return 5;
    if (size <= SIZE_CLASS_7) return 6;
    return 7;
}

static BlockFooter *get_footer(Block *block) {
    #if BOUNDARY_TAGS
    return (BlockFooter*)((char*)block + sizeof(Block) + block->size);
    #else
    return NULL;
    #endif
}

static void set_block_footer(Block *block) {
    #if BOUNDARY_TAGS
    BlockFooter *footer = get_footer(block);
    if (footer) {
        footer->size = block->size;
        footer->free = block->free;
        footer->sentinel = FOOTER_SENTINEL;
    }
    #endif
}

static Block *get_prev_physical_block(Block *block) {
    #if BOUNDARY_TAGS
    if ((char*)block <= heap) return NULL;
    BlockFooter *prev_footer = (BlockFooter*)((char*)block - sizeof(BlockFooter));
    if (prev_footer->sentinel != FOOTER_SENTINEL) return NULL;
    Block *prev_block = (Block*)((char*)prev_footer - prev_footer->size - sizeof(Block));
    if (prev_block->sentinel_start != SENTINEL_VALUE || prev_block->sentinel_end != SENTINEL_VALUE) return NULL;
    return prev_block;
    #else
    return NULL;
    #endif
}

static void *get_block_data(Block *block) {
    return (void*)((char*)block + sizeof(Block));
}

static void add_guard_bytes(void *ptr, size_t size) {
    #if MEMORY_GUARDS
    unsigned char *guard_start = (unsigned char*)ptr - ALIGNMENT;
    unsigned char *guard_end = (unsigned char*)ptr + size;
    for (size_t i = 0; i < ALIGNMENT; i++) guard_start[i] = GUARD_VALUE;
    for (size_t i = 0; i < ALIGNMENT; i++) guard_end[i] = GUARD_VALUE;
    #endif
}

static bool check_guard_bytes(void *ptr, size_t size) {
    #if MEMORY_GUARDS
    unsigned char *guard_start = (unsigned char*)ptr - ALIGNMENT;
    unsigned char *guard_end = (unsigned char*)ptr + size;
    for (size_t i = 0; i < ALIGNMENT; i++) {
        if (guard_start[i] != GUARD_VALUE) return false;
    }
    for (size_t i = 0; i < ALIGNMENT; i++) {
        if (guard_end[i] != GUARD_VALUE) return false;
    }
    #endif
    return true;
}

#if ENABLE_STATS
static void update_fragmentation_stats(void) {
    stats.fragmentation_count = 0;
    stats.largest_free_block = 0;
    stats.smallest_free_block = SIZE_MAX;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        Block *current = free_lists[i];
        while (current) {
            stats.fragmentation_count++;
            if (current->size > stats.largest_free_block) stats.largest_free_block = current->size;
            if (current->size < stats.smallest_free_block) stats.smallest_free_block = current->size;
            current = current->next;
        }
    }
    if (stats.fragmentation_count == 0) stats.smallest_free_block = 0;
}
#endif

void *my_malloc_internal(size_t requested_size, const char *file, int line) {
    if (!initialized) initialize();
    if (requested_size == 0) return NULL;
    
    LOCK();
    #if ENABLE_STATS
    stats.total_allocations++;
    stats.requested_bytes += requested_size;
    clock_t start_time = clock();
    #endif
    
    size_t aligned_size = ALIGN(requested_size + (MEMORY_GUARDS ? ALIGNMENT * 2 : 0));
    check_heap_integrity();
    
    Block *block = find_best_fit_block(aligned_size);
    if (!block) {
        #if ENABLE_STATS
        stats.failed_allocations++;
        #endif
        UNLOCK();
        return NULL;
    }
    
    if (block->size >= aligned_size + MIN_BLOCK_SIZE) {
        split_block(block, aligned_size);
    }
    
    block->free = false;
    block->request_size = requested_size;
    block->address_tag = (void*)0xDEADBEEF;
    block->alloc_id = next_alloc_id++;
    
    remove_from_free_list(block);
    add_to_used_list(block);
    
    #if BOUNDARY_TAGS
    set_block_footer(block);
    #endif
    
    #if ENABLE_STATS
    stats.allocated_bytes += block->size;
    stats.allocated_blocks++;
    stats.free_bytes -= block->size;
    stats.free_blocks--;
    stats.overhead_bytes += (sizeof(Block) + (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0) + (aligned_size - requested_size));
    stats.class_usage[get_size_class(aligned_size)] += aligned_size;
    stats.total_alloc_time += (clock() - start_time);
    update_fragmentation_stats();
    #endif
    
    void *data_ptr = get_block_data(block);
    #if MEMORY_GUARDS
    data_ptr = (void*)((char*)data_ptr + ALIGNMENT);
    add_guard_bytes(data_ptr, requested_size);
    #endif
    
    memset(data_ptr, 0, requested_size);
    
    #if LEAK_DETECTION
    AllocationRecord *record = (AllocationRecord*)malloc(sizeof(AllocationRecord));
    if (record) {
        record->ptr = data_ptr;
        record->size = requested_size;
        record->alloc_id = block->alloc_id;
        record->file = file;
        record->line = line;
        record->next = allocation_records;
        allocation_records = record;
    }
    #endif
    
    UNLOCK();
    return data_ptr;
}

void my_free(void *ptr) {
    if (!initialized) initialize();
    if (!ptr) return;
    
    LOCK();
    check_heap_integrity();
    #if ENABLE_STATS
    clock_t start_time = clock();
    #endif
    
    #if MEMORY_GUARDS
    ptr = (void*)((char*)ptr - ALIGNMENT);
    #endif
    
    Block *block = (Block*)((char*)ptr - sizeof(Block));
    validate_block(block, "my_free");
    
    if (block->free) {
        fprintf(stderr, "Double free detected at %p (ID %u)\n", ptr, block->alloc_id);
        UNLOCK();
        return;
    }
    
    #if MEMORY_GUARDS
    if (!check_guard_bytes((void*)((char*)ptr + ALIGNMENT), block->request_size)) {
        fprintf(stderr, "Buffer overrun detected at %p (ID %u)\n", (void*)((char*)ptr + ALIGNMENT), block->alloc_id);
    }
    #endif
    
    #if ENABLE_STATS
    stats.allocated_bytes -= block->size;
    stats.allocated_blocks--;
    stats.free_bytes += block->size;
    stats.free_blocks++;
    stats.total_frees++;
    stats.class_usage[get_size_class(block->size)] -= block->size;
    #endif
    
    block->free = true;
    block->address_tag = NULL;
    remove_from_used_list(block);
    add_to_free_list(block);
    
    #if BOUNDARY_TAGS
    set_block_footer(block);
    #endif
    
    try_merge_with_neighbors(block);
    
    #if LEAK_DETECTION
    AllocationRecord **curr = &allocation_records;
    while (*curr) {
        if ((*curr)->ptr == (void*)((char*)ptr + ALIGNMENT)) {
            AllocationRecord *temp = *curr;
            *curr = temp->next;
            free(temp);
            break;
        }
        curr = &(*curr)->next;
    }
    #endif
    
    #if ENABLE_STATS
    stats.total_free_time += (clock() - start_time);
    update_fragmentation_stats();
    #endif
    
    UNLOCK();
}

size_t my_malloc_size(void *ptr) {
    if (!ptr || !initialized) return 0;
    #if MEMORY_GUARDS
    ptr = (void*)((char*)ptr - ALIGNMENT);
    #endif
    Block *block = (Block*)((char*)ptr - sizeof(Block));
    if (block->sentinel_start != SENTINEL_VALUE || block->sentinel_end != SENTINEL_VALUE || block->free) {
        return 0;
    }
    return block->request_size;
}
void *my_realloc_internal(void *ptr, size_t size, const char *file, int line) {
    //  edge cases
    if (!ptr) return my_malloc_internal(size, file, line);
    if (size == 0) {
        my_free(ptr);
        return NULL;
    }
    
    LOCK();
    
    // current block size using the ORIGINAL pointer
    size_t current_size = my_malloc_size(ptr);
    if (current_size == 0) {
        UNLOCK();
        return NULL; 
    }
    
    // for later use
    void *user_ptr = ptr;
    
    //internal pointer for header calculation
    #if MEMORY_GUARDS
    void *internal_ptr = (void*)((char*)ptr - ALIGNMENT);
    #else
    void *internal_ptr = ptr;
    #endif
    
    Block *block = (Block*)((char*)internal_ptr - sizeof(Block));
    size_t required_total_size = ALIGN(size + (MEMORY_GUARDS ? ALIGNMENT * 2 : 0));

    if (required_total_size <= block->size) {
        if (block->size >= required_total_size + MIN_BLOCK_SIZE) {
            split_block(block, required_total_size);
            #if BOUNDARY_TAGS
            set_block_footer(block);
            #endif
        }
        
        block->request_size = size;
        
        #if MEMORY_GUARDS
        add_guard_bytes(user_ptr, size);
        #endif
        
        UNLOCK();
        return user_ptr;
    }
    
    UNLOCK();
    
    void *new_ptr = my_malloc_internal(size, file, line);
    if (!new_ptr) return NULL;
    
    memcpy(new_ptr, user_ptr, current_size < size ? current_size : size);
    
    my_free(user_ptr);
    
    return new_ptr;
}

void *my_calloc_internal(size_t count, size_t size, const char *file, int line) {
    if (count > 0 && size > SIZE_MAX / count) return NULL;
    size_t total_size = count * size;
    return my_malloc_internal(total_size, file, line);
}

static Block *find_best_fit_block(size_t size) {
    int size_class = get_size_class(size);
    Block *best_fit = NULL;
    size_t smallest_diff = SIZE_MAX;
    
    Block *current = free_lists[size_class];
    while (current) {
        validate_block(current, "find_best_fit");
        if (current->free && current->size >= size) {
            size_t diff = current->size - size;
            if (diff == 0) return current;
            else if (diff < smallest_diff) {
                smallest_diff = diff;
                best_fit = current;
            }
        }
        current = current->next;
    }
    if (best_fit) return best_fit;
    
    for (int c = size_class + 1; c < NUM_SIZE_CLASSES; c++) {
        current = free_lists[c];
        if (current) return current;
    }
    return NULL;
}

static void split_block(Block *block, size_t size) {
    validate_block(block, "split_block_before");
    size_t original_size = block->size;
    size_t remaining_size = original_size - size - sizeof(Block) - (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0);
    
    if (remaining_size < MIN_BLOCK_SIZE + (MEMORY_GUARDS ? ALIGNMENT * 2 : 0)) return;
    
    block->size = size;
    #if BOUNDARY_TAGS
    set_block_footer(block);
    #endif
    
    Block *new_block = (Block*)((char*)block + sizeof(Block) + size + (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0));
    new_block->sentinel_start = SENTINEL_VALUE;
    new_block->size = remaining_size;
    new_block->free = true;
    new_block->prev = NULL;
    new_block->next = NULL;
    new_block->request_size = 0;
    new_block->address_tag = NULL;
    new_block->alloc_id = 0;
    new_block->sentinel_end = SENTINEL_VALUE;
    
    #if BOUNDARY_TAGS
    set_block_footer(new_block);
    #endif
    
    add_to_free_list(new_block);
    #if ENABLE_STATS
    stats.free_blocks++;
    #endif
    validate_block(block, "split_block_after1");
    validate_block(new_block, "split_block_after2");
}

static bool try_merge_with_neighbors(Block *block) {
    bool merged = false;
    if ((char*)block < heap || (char*)block >= heap + HEAP_SIZE) return false;
    
    Block *next_physical = (Block*)((char*)block + sizeof(Block) + block->size + (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0));
    if ((char*)next_physical < heap + HEAP_SIZE - sizeof(Block)) {
        validate_block(next_physical, "merge_check_next");
        if (next_physical->free) {
            remove_from_free_list(next_physical);
            block->size += sizeof(Block) + next_physical->size + (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0);
            #if BOUNDARY_TAGS
            set_block_footer(block);
            #endif
            #if ENABLE_STATS
            stats.free_blocks--;
            #endif
            merged = true;
        }
    }
    
    #if BOUNDARY_TAGS
    Block *prev_physical = get_prev_physical_block(block);
    if (prev_physical && prev_physical->free) {
        remove_from_free_list(block);
        prev_physical->size += sizeof(Block) + block->size + sizeof(BlockFooter);
        set_block_footer(prev_physical);
        #if ENABLE_STATS
        stats.free_blocks--;
        #endif
        merged = true;
    }
    #endif
    return merged;
}

static void add_to_free_list(Block *block) {
    if (!block) return;
    block->free = true;
    int size_class = get_size_class(block->size);
    block->prev = NULL;
    block->next = free_lists[size_class];
    if (free_lists[size_class]) free_lists[size_class]->prev = block;
    free_lists[size_class] = block;
    
    #if CACHE_LOCALITY
    if (size_class < 4) {
        Block *current = free_lists[size_class];
        Block *prev = NULL;
        while (current && current->next && current->next < block) {
            prev = current;
            current = current->next;
        }
        if (prev) {
            free_lists[size_class] = free_lists[size_class]->next;
            if (free_lists[size_class]) free_lists[size_class]->prev = NULL;
            block->next = current->next;
            if (block->next) block->next->prev = block;
            current->next = block;
            block->prev = current;
        }
    }
    #endif
}

static void remove_from_free_list(Block *block) {
    if (!block) return;
    int size_class = get_size_class(block->size);
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    if (free_lists[size_class] == block) free_lists[size_class] = block->next;
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

static void remove_from_used_list(Block *block) {
    if (!block) return;
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    if (used_list == block) used_list = block->next;
    block->prev = NULL;
    block->next = NULL;
}

static void validate_block(Block *block, const char *location) {
    if (!block) return;
    if ((char*)block < heap || (char*)block >= heap + HEAP_SIZE) {
        fprintf(stderr, "MEMORY ERROR at %s: Block %p is outside heap bounds\n", location, block);
        return;
    }
    if (block->sentinel_start != SENTINEL_VALUE) fprintf(stderr, "MEMORY CORRUPTION at %s: Block %p start sentinel corrupted\n", location, block);
    if (block->sentinel_end != SENTINEL_VALUE) fprintf(stderr, "MEMORY CORRUPTION at %s: Block %p end sentinel corrupted\n", location, block);
    #if BOUNDARY_TAGS
    BlockFooter *footer = get_footer(block);
    if (footer && footer->sentinel != FOOTER_SENTINEL) fprintf(stderr, "MEMORY CORRUPTION at %s: Block %p footer sentinel corrupted\n", location, block);
    #endif
    if (block->size > HEAP_SIZE) fprintf(stderr, "MEMORY ERROR at %s: Block %p has invalid size %zu\n", location, block, block->size);
}

static void check_heap_integrity(void) {
    #if DEBUG_LEVEL < 2
    return;
    #endif
    Block *current;
    size_t free_count = 0, used_count = 0;
    size_t free_bytes = 0, used_bytes = 0;
    
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        current = free_lists[i];
        while (current) {
            validate_block(current, "heap_check_free");
            if (!current->free) fprintf(stderr, "HEAP ERROR: Block in free list is marked as used\n");
            free_count++;
            free_bytes += current->size;
            current = current->next;
        }
    }
    
    current = used_list;
    while (current) {
        validate_block(current, "heap_check_used");
        if (current->free) fprintf(stderr, "HEAP ERROR: Block in used list is marked as free\n");
        used_count++;
        used_bytes += current->size;
        current = current->next;
    }
    
    #if ENABLE_STATS
    if (stats.free_blocks != free_count || stats.allocated_blocks != used_count) {
        fprintf(stderr, "HEAP ERROR: Stats mismatch\n");
    }
    #endif
}