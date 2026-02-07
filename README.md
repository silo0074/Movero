# Movero

**Movero** is a high-performance, asynchronous file copying and moving utility for Linux, built with Qt. It is designed to bridge the gap between simple file managers and low-level system performance, offering features like `O_DIRECT` support, grouped disk flushing, and hardware-verified checksums.

<img src="icon.png" alt="Movero Icon" width="30%">


## 🚀 Key Features

- **Hybrid Sync Strategy:** Balances data integrity and speed by batch-flushing data to disk (64MB default) to minimize I/O wait times.
- **Hardware Verification:** Optionally bypasses the Linux Page Cache using `posix_fadvise` and `O_DIRECT` to ensure files are read directly from physical storage during checksum verification.

## 🛠 Advanced Technical Implementation

It implements several low-level Linux kernel optimizations:

* **Memory Alignment:** Uses `std::aligned_alloc` with 4096-byte boundaries to support `O_DIRECT` I/O.
* **Zero-Cache Verification:** Implements the `fdatasync` + `POSIX_FADV_DONTNEED` sequence to guarantee that verification hashes are calculated from the platter/NAND, not RAM.
* **D-Bus Integration:** Communicates with `org.freedesktop.FileManager1` to highlight completed transfers in your native file manager.



## 📥 Installation

### Prerequisites
- Qt 5.15+ or Qt 6
- xxHash library
- Linux Kernel 5.x+ (for `sync_file_range` support)

### Build from Source
```bash
git clone [https://github.com/yourusername/movero.git](https://github.com/yourusername/movero.git)
cd movero
mkdir build && cd build
qmake ..
make -j$(nproc)
```

## ❤️ Donations

<a href="https://www.buymeacoffee.com/liviuistrate" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="60px" width="217px">
</a>