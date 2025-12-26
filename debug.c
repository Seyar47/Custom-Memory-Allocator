#include "allocator.h"

#if ENABLE_STATS
void print_allocation_stats(void) {
    LOCK();
    printf("\n=== Memory Allocator Statistics ===\n");
    printf("Allocated: %zu bytes in %zu blocks (avg: %.2f)\n", 
           stats.allocated_bytes, stats.allocated_blocks,
           stats.allocated_blocks > 0 ? (float)stats.allocated_bytes / stats.allocated_blocks : 0);
    printf("Free: %zu bytes in %zu blocks (avg: %.2f)\n", 
           stats.free_bytes, stats.free_blocks,
           stats.free_blocks > 0 ? (float)stats.free_bytes / stats.free_blocks : 0);
    printf("Memory overhead: %zu bytes (%.2f%%)\n",
           stats.overhead_bytes,
           ((float)stats.overhead_bytes / (stats.allocated_bytes + stats.free_bytes + 1)) * 100);
    printf("Total allocations: %zu (failed: %zu)\n", stats.total_allocations, stats.failed_allocations);
    printf("Total frees: %zu\n", stats.total_frees);
    
    float fragmentation_index = 0.0;
    if (stats.free_blocks > 1 && stats.free_bytes > 0) {
        fragmentation_index = 1.0 - ((float)stats.largest_free_block / stats.free_bytes);
    }
    printf("Fragmentation index: %.4f\n", fragmentation_index);
    
    printf("\nSize class distribution:\n");
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        printf("Class %d: %zu bytes\n", i, stats.class_usage[i]);
    }
    printf("================================\n");
    UNLOCK();
}
#else
void print_allocation_stats(void) {}
#endif

void print_heap_map(void) {
    LOCK();
    printf("\n===== HEAP MAP =====\n");
    if (!initialized) { printf("Heap not initialized\n"); UNLOCK(); return; }
    
    char *heap_end = heap + HEAP_SIZE;
    char *current = heap;
    int block_count = 0;
    
    while (current < heap_end) {
        Block *block = (Block*)current;
        if (block->sentinel_start != SENTINEL_VALUE) { printf("[CORRUPTED at %p]\n", current); break; }
        
        printf("Block %d [%p]: %zu bytes, %s, ID: %u\n", 
               ++block_count, block, block->size, block->free ? "FREE" : "USED", block->alloc_id);
        
        current = (char*)(block) + sizeof(Block) + block->size + (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0);
        if (current >= heap_end) break;
    }
    printf("====================\n");
    UNLOCK();
}

void visualize_memory(void) {
    LOCK();
    if (!initialized) { printf("Heap not initialized\n"); UNLOCK(); return; }
    
    printf("\n===== MEMORY VISUALIZATION =====\n");
    const int width = 60;
    const char used_char = '#';
    const char free_char = '.';
    const char overhead_char = 'o';
    
    #if ENABLE_STATS
    size_t total_bytes = stats.allocated_bytes + stats.free_bytes + stats.overhead_bytes;
    #else
    size_t total_bytes = HEAP_SIZE; 
    #endif
    double bytes_per_char = (double)total_bytes / width;
    
    char map[width + 1];
    map[width] = '\0';
    for (int i = 0; i < width; i++) map[i] = free_char;
    
    char *current = heap;
    while (current < heap + HEAP_SIZE) {
        Block *block = (Block*)current;
        if (block->sentinel_start != SENTINEL_VALUE) break;
        
        int start_pos = ((current - heap) / bytes_per_char);
        int header_end = start_pos + (sizeof(Block) / bytes_per_char);
        int data_end = header_end + (block->size / bytes_per_char);
        int footer_end = data_end + (BOUNDARY_TAGS ? (sizeof(BlockFooter) / bytes_per_char) : 0);
        
        if (header_end >= width) header_end = width - 1;
        if (data_end >= width) data_end = width - 1;
        if (footer_end >= width) footer_end = width - 1;
        
        for (int i = start_pos; i < header_end && i < width; i++) map[i] = overhead_char;
        for (int i = header_end; i < data_end && i < width; i++) map[i] = block->free ? free_char : used_char;
        for (int i = data_end; i < footer_end && i < width; i++) map[i] = overhead_char;
        
        current = (char*)(block) + sizeof(Block) + block->size + (BOUNDARY_TAGS ? sizeof(BlockFooter) : 0);
    }
    
    printf("%s\n", map);
    printf("Legend: %c=Used, %c=Free, %c=Overhead\n", used_char, free_char, overhead_char);
    UNLOCK();
}

void check_for_leaks(void) {
    #if LEAK_DETECTION
    LOCK();
    printf("\n=== Memory Leak Check ===\n");
    AllocationRecord *record = allocation_records;
    size_t leak_count = 0;
    size_t leak_bytes = 0;
    
    while (record) {
        if (record->ptr) {
            printf("Potential leak: %p, %zu bytes, ID %u, allocated at %s:%d\n", 
                   record->ptr, record->size, record->alloc_id, 
                   record->file ? record->file : "unknown", record->line);
            leak_count++;
            leak_bytes += record->size;
        }
        record = record->next;
    }
    
    if (leak_count == 0) printf("No memory leaks detected.\n");
    else printf("Total: %zu leaks, %zu bytes\n", leak_count, leak_bytes);
    printf("========================\n");
    UNLOCK();
    #endif
}

void get_memory_stats(float *used_percent, float *free_percent, float *overhead_percent, float *fragmentation) {
    #if ENABLE_STATS
    LOCK();
    size_t total_bytes = stats.allocated_bytes + stats.free_bytes + stats.overhead_bytes;
    *used_percent = ((float)stats.allocated_bytes / total_bytes) * 100.0;
    *free_percent = ((float)stats.free_bytes / total_bytes) * 100.0;
    *overhead_percent = ((float)stats.overhead_bytes / total_bytes) * 100.0;
    
    if (stats.free_blocks > 1 && stats.free_bytes > 0) {
        *fragmentation = (1.0 - ((float)stats.largest_free_block / stats.free_bytes)) * 100.0;
    } else {
        *fragmentation = 0.0;
    }
    UNLOCK();
    #else
    *used_percent = 0; *free_percent = 0; *overhead_percent = 0; *fragmentation = 0;
    #endif
}