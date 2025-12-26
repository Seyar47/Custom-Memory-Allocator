# Advanced Memory Allocator

A high-performance, thread-safe memory allocator implemented in C. This project replaces the standard `malloc`, `free`, and `realloc` functions with a custom implementation designed for efficiency and robustness in multi-threaded environments.

## 🚀 Key Features

* **Thread Safety:** Implements `pthread` mutex locking to ensure safe concurrent access in multi-threaded applications.
* **O(1) Allocation:** Uses **Segregated Free Lists** (8 size classes) to reduce block search time from linear $O(N)$ to constant $O(1)$.
* **Memory Coalescing:** Automatically merges adjacent free blocks (using boundary tags) to minimize external fragmentation.
* **Debug & Protection:**
    * **Guard Bytes:** Detects buffer overflows (0xFE padding).
    * **Leak Detection:** Tracks allocations and reports memory leaks upon exit.
    * **Heap Visualization:** Prints a visual map of memory usage and fragmentation.

## 🛠 Project Structure

The project is modularized for maintainability:

* `allocator.h` - Header file containing structs, constants, and function prototypes.
* `allocator.c` - Core logic for memory management (`malloc`, `free`, `realloc`).
* `debug.c` - Debugging tools, heap visualization, and statistics tracking.
* `main.c` - Test driver demonstrating allocation patterns and edge cases.
* `Makefile` - automated build script.

## 📦 How to Build and Run

**Prerequisites:** GCC Compiler (Linux/WSL/MinGW)

1.  **Compile the project:**
    ```bash
    make
    ```

2.  **Run the test driver:**
    ```bash
    make run
    ```

3.  **Clean up build files:**
    ```bash
    make clean
    ```