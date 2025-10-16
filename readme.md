# 🔁 Synchronize File System
---

## 📘 Overview

This part of the project builds a **server system** that shares and synchronizes file data between a **cache** and multiple **clients**.  
It uses **shared memory** and **message queues** to move data quickly and keep everything in sync without relying on slow file I/O or network transfers.

The goal is to make data exchange **fast, reliable, and fully synchronized** — even when several clients request files at once.

---

## ⚙️ How It Works

1. **Server Setup:**  
   The system starts by creating a central server that manages shared memory channels and message queues.  
   Each client connects to the server through a proxy.

2. **Shared Memory Channels:**  
   - These are special memory regions both the cache and proxy can access.  
   - They act like high-speed “data pipes” for file content.  
   - Each channel is paired with control flags to coordinate reading and writing.

3. **Message Queues:**  
   - The proxy sends messages to the cache saying which file is needed and which memory slot to use.  
   - The cache reads the file from disk, places it into shared memory, and signals when it’s ready.

4. **Data Synchronization:**  
   - The proxy reads the file data from shared memory and sends it to the client.  
   - The server ensures that clients always receive the most up-to-date version of each file.  
   - When a file changes, updates propagate automatically through shared memory.

5. **Memory Recycling:**  
   - Once a transfer completes, the shared memory segment is cleared and reused for new requests.  
   - This keeps memory usage low while maintaining high performance.

---

## 🧪 Testing

- Verified that multiple clients can connect and receive synchronized data.  
- Ensured memory channels reset correctly after transfers.  
- Confirmed no data corruption or memory leaks under concurrent load.

---

## 🧰 Tools & Technologies

- **C / POSIX Threads**  
- **POSIX Shared Memory** – for high-speed data sharing  
- **POSIX Message Queues** – for coordination and signaling  
- **Mutexes & Condition Variables** – to synchronize reads and writes  
- **GFServer / GFClient APIs** – for client-server communication

---

## 💡 Key Takeaways

- The system acts as a **synchronized server**, keeping clients updated with the latest file data.  
- Shared memory enables **direct, fast data exchange** without extra copying.  
- Message queues make communication **organized and reliable**.  
- This design demonstrates how **servers, caches, and clients** can stay synchronized in real time using low-level OS mechanisms.

---
