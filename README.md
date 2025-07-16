# 🚀 High-Performance Multi-threaded Web Server with LRU Cache

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/anugrahk21/webserver-cache)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C](https://img.shields.io/badge/language-C-orange)](https://en.wikipedia.org/wiki/C_(programming_language))

> A blazing-fast, multi-threaded HTTP web server implementation in C featuring an intelligent LRU cache system for optimal performance! 🔥

## 📋 Table of Contents

- [✨ Features](#-features)
- [🏗️ Architecture](#️-architecture)
- [🚀 Quick Start](#-quick-start)
- [🔧 Usage](#-usage)
- [📊 Performance Testing](#-performance-testing)
- [📈 Benchmarking](#-benchmarking)
- [🛠️ Components](#️-components)
- [📦 Build System](#-build-system)

## ✨ Features

- 🧵 **Multi-threaded Architecture** - Configurable thread pool for concurrent request handling
- 🗄️ **LRU Cache System** - Intelligent caching with customizable memory limits
- ⚡ **High Performance** - Optimized for speed and efficiency
- 📊 **Built-in Benchmarking** - Comprehensive performance testing tools
- 🔧 **Configurable Parameters** - Flexible server configuration
- 📈 **Performance Visualization** - Gnuplot integration for data visualization
- 🛡️ **Robust Error Handling** - Comprehensive error management and logging

> ⚠️ **Known Issue**: There's a current bug in the hashtable eviction logic that can cause linked list corruption when removing the first node in a hash chain during cache eviction.

## 🏗️ Architecture

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   HTTP Client   │────│   Web Server    │────│   File System   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                              │
                       ┌─────────────────┐
                       │   LRU Cache     │
                       │   (Hash Table)  │
                       └─────────────────┘
```

The server implements:
- **Producer-Consumer Pattern** with thread pool
- **Hash Table + Doubly Linked List** for O(1) LRU cache operations
- **Non-blocking I/O** with poll() for connection management

## 🚀 Quick Start

### Prerequisites
- GCC compiler with pthread support
- Make build system
- POSIX-compliant system (Linux/Unix)

### Build & Run

```bash
# Build the project
make

# Start the server
./server <port> <nr_threads> <max_requests> <max_cache_size>

# Example: Start server on port 8080 with 8 threads, 100 max requests, 1MB cache
./server 8080 8 100 1048576
```

## 🔧 Usage

### Server Parameters

| Parameter | Description | Example |
|-----------|-------------|---------|
| `port` | Port number (≥1024) | `8080` |
| `nr_threads` | Number of worker threads | `8` |
| `max_requests` | Maximum concurrent requests | `100` |
| `max_cache_size` | Cache size in bytes | `1048576` |

### Client Testing

```bash
# Simple client test
./client_simple localhost 8080 /index.html

# Multi-threaded client benchmark
./client [-t] <host> <port> <nr_times> <nr_threads> <fileset>
```

## 📊 Performance Testing

### Automated Experiments

```bash
# Test different thread counts
./run-experiment 8080

# Test various cache sizes
./run-cache-experiment 8080

# Single experiment run
./run-one-experiment <port> <threads> <requests> <cache_size> <fileset>
```

### Generate Test Files

```bash
# Create file set for testing
./fileset -d fileset_dir
```

## 📈 Benchmarking

The project includes comprehensive benchmarking tools:

- **🧵 Thread Scaling**: Tests performance with 0-32 threads
- **📊 Request Load**: Varies concurrent request counts
- **🗄️ Cache Impact**: Analyzes cache size effects (0-8MB)
- **📉 Visualization**: Automatic Gnuplot chart generation

### Output Files
- `plot-threads.out` - Thread performance data
- `plot-requests.out` - Request load data  
- `plot-cachesize.out` - Cache performance data
- `*.pdf` - Generated performance charts

## 🛠️ Components

### Core Files

| File | Purpose | Description |
|------|---------|-------------|
| `server.c` | 🖥️ Main server | HTTP server entry point |
| `server_thread.c` | 🧵 Thread management | Worker threads & cache logic |
| `request.c` | 📝 Request handling | HTTP request processing |
| `client.c` | 👤 Test client | Multi-threaded client for testing |
| `common.c` | 🔧 Utilities | Shared utility functions |

### Cache Implementation
- **Hash Table**: O(1) lookup with chaining
- **LRU List**: Doubly-linked list for cache eviction
- **Thread-Safe**: Mutex protection for concurrent access

## 📦 Build System

```bash
# Available targets
make all        # Build all executables
make clean      # Remove build artifacts  
make realclean  # Deep clean including logs
make depend     # Generate dependencies
make tags       # Generate TAGS file
```

### Dependencies
- `pthread` - POSIX threads
- `popt` - Command line parsing
- `math` - Mathematical functions

---

**Happy Coding!** 🎉 Feel free to contribute, report issues, or suggest improvements!
