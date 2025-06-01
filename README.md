# Enhanced Memory Allocator

This is a custom memory allocator written in C. It acts like `malloc`, `free`, and `realloc`, but I built it from scratch to learn how memory management works behind the scenes.

## 🚀 Features

- Block splitting (reduces wasted space)
- Block merging (reduces fragmentation)
- Memory alignment (8 byte)
- Sentinel values (detects memory corruption)
- Tracks allocation stats (used/free memory, failed allocations)
- Debugging tools like heap map printer

## 📦 How to Use

### Compile and run test code:

```bash
gcc -D MEMORY_ALLOCATOR_TEST enhanced-memory-allocator.c -o allocator
./allocator


void initialize(void);
void* my_malloc(size_t size);
void my_free(void* ptr);
void* my_realloc(void* ptr, size_t size);
void* my_calloc(size_t count, size_t size);

------ Example Ouput ---
===== HEAP MAP =====
[0x1234abcd: 80 bytes, USED]
[0x1234acff: 1024 bytes, FREE]
====================

-🧠 Why I Built This

I built this project to understand how dynamic memory allocation works at a low level. Instead of just using malloc, I wanted to create my own allocator to learn about memory alignment, fragmentation, heap corruption, and performance tradeoffs.





