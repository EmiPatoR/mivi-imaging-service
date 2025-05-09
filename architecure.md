# Medical Imaging System Architecture

## Overview

This document outlines the architecture for the Medical VR/AR Ultrasound Imaging system, featuring a microservice approach with shared memory communication.

## Core Design Principles

1. **Single Responsibility**: Each service focuses on exactly one task
2. **Zero-Copy Communication**: Minimize data copying for real-time performance
3. **Language Flexibility**: Services can be implemented in the most appropriate language
4. **Low Latency**: Prioritize low-latency acquisition and processing
5. **Loose Coupling**: Services communicate via well-defined protocols

## Service Architecture

```
┌─────────────────────┐         ┌──────────────────┐         ┌───────────────────┐
│                     │         │                  │         │                   │
│  Ultrasound Device  │───►│ C++ Acquisition │─────►│ Shared Memory Ring │
│  (Blackmagic SDK)   │         │     Service      │         │      Buffer       │
│                     │         │                  │         │                   │
└─────────────────────┘         └──────────────────┘         └─────────┬─────────┘
                                                                       │
                                                                       ▼
         ┌─────────────────────────────────────────────────────────────────────────┐
         │                                                                         │
         │                             Consumers                                   │
         │                                                                         │
         ├────────────────────┬────────────────────┬───────────────────────────────┤
         │                    │                    │                               │
         │  Rust gRPC Bridge  │  C++ Segmentation  │  Rust 3D Reconstruction      │
         │      Service       │      Service       │         Service              │
         │                    │                    │                               │
         └────────────────────┴────────────────────┴───────────────────────────────┘
```

## Service Responsibilities

### C++ Acquisition Service
- Manages ultrasound hardware devices via Blackmagic SDK
- Acquires frames at the highest possible frame rate
- Writes frames to shared memory with minimal processing
- Provides a simple control interface (start/stop/configure)

### Shared Memory Ring Buffer
- Zero-copy communication mechanism between services
- Uses memory-mapped files for cross-process and cross-language access
- Implements atomic synchronization primitives
- Provides a well-defined memory layout

### Rust gRPC Bridge Service
- Reads frames from shared memory
- Provides gRPC API for external clients
- Handles network communication and serialization
- Isolates network overhead from the acquisition pipeline

### C++ Segmentation Service
- Reads frames from shared memory
- Performs AI-based thyroid segmentation
- Writes segmentation results back to shared memory
- Optionally communicates with calibration services

### Rust 3D Reconstruction Service
- Consumes frames and segmentation data from shared memory
- Tracks probe position using calibration data
- Reconstructs 3D volume from 2D ultrasound slices
- Updates 3D model for visualization

## Communication Protocols

### Shared Memory Protocol
- Ring buffer layout with atomic read/write pointers
- Frame header followed by raw image data
- Metadata section for additional information
- Versioned protocol for compatibility

### Service Control
- Simple HTTP or local socket-based control API
- JSON configuration and status messages
- Start/stop/configure operations

### External API (via gRPC Bridge)
- gRPC services for remote control and data access
- Streaming endpoints for real-time data
- Authentication and authorization
- Compression options for remote clients
