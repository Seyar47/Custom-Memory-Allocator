#include "allocator.h"
#include <string.h>

int main() {
    initialize();
    
    printf("Testing enhanced memory allocator:\n");
    void *ptrs[100];
    int count = 0;
    
    printf("Allocating memory blocks...\n");
    for (int i = 0; i < 10; i++) {
        size_t size = (i + 1) * 32;
        ptrs[count] = my_malloc(size);
        printf("Allocated %zu bytes at %p\n", size, ptrs[count]);
        if (ptrs[count]) {
            memset(ptrs[count], 0xAB, size);
            count++;
        }
    }
    
    print_heap_map();
    
    printf("\nFreeing some blocks to create fragmentation...\n");
    for (int i = 0; i < count; i += 2) {
        printf("Freeing %p\n", ptrs[i]);
        my_free(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    print_heap_map();
    visualize_memory();
    
    printf("\nAllocating after fragmentation...\n");
    void *large_ptr = my_malloc(512);
    printf("Allocated 512 bytes at %p\n", large_ptr);
    
    printf("\nTesting realloc...\n");
    void *realloc_ptr = my_malloc(100);
    realloc_ptr = my_realloc(realloc_ptr, 200);
    realloc_ptr = my_realloc(realloc_ptr, 50);
    
    printf("\nTesting calloc...\n");
    int *int_array = my_calloc(10, sizeof(int));
    
    print_heap_map();
    print_allocation_stats();
    
    printf("\nChecking for leaks before cleanup:\n");
    check_for_leaks();
    
    printf("\nCleaning up all allocations...\n");
    for (int i = 1; i < count; i += 2) {
        if (ptrs[i]) my_free(ptrs[i]);
    }
    my_free(large_ptr);
    my_free(realloc_ptr);
    my_free(int_array);
    
    visualize_memory();
    cleanup();
    
    return 0;
}