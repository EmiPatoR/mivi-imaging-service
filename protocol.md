# Shared Memory Protocol for Cross-Language Access

This document defines the shared memory protocol used for communicating ultrasound frame data between different services in the medical imaging system.

## Memory Layout

The shared memory region is organized as follows:

```
┌───────────────────────────────────────────────────────────────────┐
│                         Control Block                             │
├───────────────────────────────────────────────────────────────────┤
│                         Metadata Area                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│                          Frame 0 Header                           │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│                          Frame 0 Data                             │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│                          Frame 1 Header                           │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│                          Frame 1 Data                             │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                              ...                                  │
└───────────────────────────────────────────────────────────────────┘
```

## Control Block (64 bytes)

The control block is located at the beginning of the shared memory and contains:

```c
struct ControlBlock {
    std::atomic<uint64_t> writeIndex;      // Current write position
    std::atomic<uint64_t> readIndex;       // Current read position
    std::atomic<uint64_t> frameCount;      // Number of frames in the buffer
    std::atomic<uint64_t> totalFramesWritten; // Total frames written
    std::atomic<uint64_t> totalFramesRead; // Total frames read
    std::atomic<uint64_t> droppedFrames;   // Frames dropped due to buffer full
    std::atomic<bool> active;              // Whether the shared memory is active
    std::atomic<uint64_t> lastWriteTime;   // Timestamp of last write (ns since epoch)
    std::atomic<uint64_t> lastReadTime;    // Timestamp of last read (ns since epoch)
    uint32_t metadataOffset;               // Offset to metadata area
    uint32_t metadataSize;                 // Size of metadata area
    uint32_t flags;                        // Additional flags
};
```

## Metadata Area (4KB)

The metadata area contains a JSON document with information about:
- Format version
- Creation timestamp
- Frame format details
- Maximum number of frames
- Buffer size
- Other system-wide metadata

Example:
```json
{
  "format_version": "1.0",
  "created_at": 1679012345678,
  "type": "medical_imaging_frames",
  "frame_format": "YUV422",
  "max_frames": 120,
  "buffer_size": 134217728,
  "data_offset": 4160
}
```

## Frame Header (32 bytes)

Each frame in the ring buffer has a header:

```c
struct FrameHeader {
    uint64_t frameId;          // Unique frame identifier
    uint64_t timestamp;        // Frame timestamp (nanoseconds since epoch)
    uint32_t width;            // Frame width in pixels
    uint32_t height;           // Frame height in pixels
    uint32_t bytesPerPixel;    // Bytes per pixel
    uint32_t dataSize;         // Size of frame data in bytes
    uint32_t formatCode;       // Format identifier code
    uint32_t flags;            // Additional flags
    uint64_t sequenceNumber;   // Sequence number for ordering
    uint32_t metadataOffset;   // Offset to JSON metadata (if present)
    uint32_t metadataSize;     // Size of metadata in bytes
};
```

## Format Codes

- `0x01`: YUV (YUV422) 8-bit
- `0x02`: BGRA (32-bit)
- `0x03`: YUV10 (10-bit)
- `0x04`: RGB10 (10-bit)
- `0xFF`: Unknown format

## Flags

- `0x01`: Zero-copy frame (data is located elsewhere)
- `0x02`: Frame has segmentation data
- `0x04`: Frame has calibration data
- `0x08`: Frame has been processed

## Synchronization Protocol

1. **Writing Process:**
   - Get current `writeIndex`
   - Check if buffer is full by comparing with `readIndex`
   - If not full, write frame header and data at `writeIndex`
   - Atomically increment `writeIndex` and `frameCount`
   - Update `lastWriteTime` and `totalFramesWritten`

2. **Reading Process:**
   - Get current `readIndex`
   - Check if buffer is empty by comparing with `writeIndex`
   - If not empty, read frame header and data at `readIndex`
   - Atomically increment `readIndex` and decrement `frameCount`
   - Update `lastReadTime` and `totalFramesRead`

## Rust Implementation Guidance

When implementing the shared memory access in Rust:

```rust
use std::sync::atomic::{AtomicU64, AtomicBool, Ordering};
use memmap2::{MmapMut, MmapOptions};
use std::fs::OpenOptions;

// Control block access
struct ControlBlock {
    write_index: *const AtomicU64,
    read_index: *const AtomicU64,
    frame_count: *const AtomicU64,
    total_frames_written: *const AtomicU64,
    total_frames_read: *const AtomicU64,
    dropped_frames: *const AtomicU64,
    active: *const AtomicBool,
    last_write_time: *const AtomicU64,
    last_read_time: *const AtomicU64,
    // ... other fields
}

impl ControlBlock {
    // Safely access atomic fields
    fn get_write_index(&self) -> u64 {
        unsafe { (*self.write_index).load(Ordering::Acquire) }
    }
    
    fn get_read_index(&self) -> u64 {
        unsafe { (*self.read_index).load(Ordering::Acquire) }
    }
    
    fn increment_read_index(&self) {
        let current = self.get_read_index();
        unsafe { 
            (*self.read_index).store(current + 1, Ordering::Release);
            // Also decrement frame count atomically
            let count = (*self.frame_count).load(Ordering::Acquire);
            if count > 0 {
                (*self.frame_count).store(count - 1, Ordering::Release);
            }
        }
    }
    
    // ... other methods
}

// SharedMemory implementation
struct SharedMemory {
    mmap: MmapMut,
    control_block: ControlBlock,
    data_offset: usize,
}

impl SharedMemory {
    fn open(name: &str) -> std::io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(format!("/dev/shm/{}", name))?;
            
        let mmap = unsafe { MmapOptions::new().map_mut(&file)? };
        
        // Initialize control block pointers
        let control_block = unsafe {
            ControlBlock {
                write_index: mmap.as_ptr() as *const AtomicU64,
                read_index: (mmap.as_ptr() as *const AtomicU64).add(1),
                // ... other fields
            }
        };
        
        // Get data offset from metadata
        let metadata_offset = u32::from_ne_bytes([
            mmap[40], mmap[41], mmap[42], mmap[43]
        ]) as usize;
        
        let metadata_size = u32::from_ne_bytes([
            mmap[44], mmap[45], mmap[46], mmap[47]
        ]) as usize;
        
        let data_offset = metadata_offset + metadata_size;
        
        Ok(SharedMemory {
            mmap,
            control_block,
            data_offset,
        })
    }
    
    fn read_latest_frame(&mut self) -> Option<Frame> {
        // Implementation details...
    }
    
    // ... other methods
}
```

## Performance Considerations

1. **Alignment**: All structures are aligned to avoid performance penalties on different architectures.
2. **Cache Line Padding**: Critical atomic variables are padded to avoid false sharing.
3. **Memory Ordering**: Appropriate memory ordering is used for atomic operations.
4. **Huge Pages**: Support for huge pages is available for better performance.
```
┌───────────────────────────────────────────────────────────────────┐
│                         Control Block                             │
├───────────────────────────────────────────────────────────────────┤
│                         Metadata Area                             │
├───────────────────────────────────────────────────────────────────┤
│                          Frame Ring Buffer                        │
│                                                                   │
│  ┌─────────────────┬───────────────────────────────────────────┐  │
│  │ Frame 0 Header  │ Frame 0 Data                              │  │
│  ├─────────────────┼───────────────────────────────────────────┤  │
│  │ Frame 1 Header  │ Frame 1 Data                              │  │
│  ├─────────────────┼───────────────────────────────────────────┤  │
│  │       ...       │ ...                                       │  │
│  └─────────────────┴───────────────────────────────────────────┘  │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```
