# 🔁 Syncrhonize File System

## 📘 Overview

This project implements a **Distributed File System (DFS)** inspired by the **Andrew File System (AFS)**.  
It enables multiple clients to **access, cache, and update files** from a central server while maintaining **strong consistency** and **real-time synchronization**.

The system ensures that whenever one client updates or deletes a file, all other clients receive the change automatically — without requiring manual refresh or re-download.

---

## ⚙️ Key Features

### 🧠 Client-Side Caching
- Each client keeps a **local cache** of accessed files to reduce server load and improve performance.
- Cached data is validated before each use to ensure it’s consistent with the server copy.

### 🔒 Writer Locks
- Only one client at a time can modify a file.
- Other clients are temporarily restricted to read-only access until updates propagate.
- Prevents data conflicts and race conditions.

### 📡 Real-Time File Synchronization
- Implemented with **gRPC bidirectional streaming** and **callback streams**.
- When a file changes or is deleted on the server, connected clients are **immediately notified**.
- Updates are applied locally, keeping all clients in sync.

### 🔁 Consistency & Validation
- Used **CRC32 checksums** and **modification timestamps (mtime)** to detect differences between client and server versions.
- Automatically triggers re-download or invalidation when inconsistencies are found.

### 🧵 Thread-Safe Design
- Built with **mutex locks** and **condition variables** to ensure safe concurrent access.
- Utilizes **inotify** to monitor file changes in real time on both server and client sides.
- Supports **asynchronous RPCs** to handle multiple client requests efficiently.

---

## 🧩 System Architecture

1. **Server**
   - Stores master copies of all files.
   - Handles client connections and version control.
   - Sends change notifications through gRPC streams.

2. **Client**
   - Maintains a synchronized cache of files.
   - Subscribes to file updates via callback streams.
   - Uses CRC32 and timestamps to ensure cache correctness.

3. **Communication**
   - Built entirely on **gRPC + Protobuf** for reliable and type-safe remote communication.
   - Uses asynchronous RPCs to support concurrent updates and downloads.

---

## 🧪 Testing

- Simulated multiple clients reading and editing files simultaneously.  
- Verified consistent state across all clients after each change.  
- Tested resilience under concurrent edits and disconnections.  
- Confirmed proper cache invalidation and re-sync behavior.

---

## 💡 Key Takeaways

- Demonstrates **distributed consistency**, **client-side caching**, and **real-time synchronization**.  
- Showcases strong understanding of **networked systems**, **RPC design**, and **multithreading**.  
- Inspired by real-world distributed file systems like **AFS** and **NFS**.

---

## 📁 Example Use Cases

- Collaborative editing tools  
- Distributed content delivery systems  
- File synchronization and backup services  

---

## 🚀 Technologies Used

| Category | Tools / Libraries |
|-----------|------------------|
| Language | C++ |
| Networking | gRPC, Protobuf |
| File Monitoring | inotify |
| Concurrency | Mutexes, Condition Variables, Async RPCs |
| Validation | CRC32, mtime comparisons |
| Design | Client-server, cache consistency, real-time updates |

---

## 🧠 Summary

This DFS project highlights how to build a **synchronized file-sharing system** from scratch — combining **network communication**, **thread safety**, and **cache coherence** to deliver fast and consistent file access across multiple clients.

---
