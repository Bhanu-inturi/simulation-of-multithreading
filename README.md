# 🧵 ThreadLab – Multithreading & Scheduling Simulator

**An Interactive Operating Systems Project | C | Raylib | Native Multithreading**

ThreadLab is a cross-platform graphical simulation application designed to demonstrate the fundamental concepts of **multithreading, CPU scheduling, process synchronization, race conditions, and deadlocks** in Operating Systems.

Built using C11 and Raylib, the application provides an interactive visual environment where users can explore how multiple threads execute, share resources, coordinate tasks, and handle synchronization challenges.

The project combines theoretical Operating Systems concepts with practical multithreading implementation using native operating system thread libraries.

---

## ✨ Features

### 1. Round Robin Scheduling
- Simulates Round Robin CPU scheduling.
- Configurable time quantum.
- Visualizes thread execution and progress.
- Supports Start, Run, Pause, Step, and Reset controls.

### 2. Priority Scheduling
- Implements non-preemptive priority scheduling.
- Executes threads according to their priority levels.
- Displays thread priorities and execution progress.
- Visualizes scheduling decisions.

### 3. Race Condition Demonstration
- Demonstrates race conditions using two native worker threads.
- Compares unprotected logical updates with mutex-protected updates.
- Visualizes lost updates when multiple threads access shared data.
- Shows how mutex locks ensure safe shared-resource access.

### 4. Deadlock Demonstration
- Visualizes circular wait between two threads and two resources.
- Demonstrates how deadlocks occur.
- Includes a deadlock prevention simulation using resource ordering.

### 5. Execution Timeline
- Provides a visual Gantt-style CPU execution timeline.
- Displays the order in which threads receive CPU time.
- Helps understand scheduling behavior.

### 6. Producer-Consumer Problem
- Implements the classic Producer-Consumer synchronization problem.
- Uses a bounded buffer with a capacity of five items.
- Demonstrates synchronization using:
  - Empty semaphore
  - Full semaphore
  - Mutex lock
- Displays produced and consumed items through a live event log.

### 7. Individual Thread Execution Time
- Launches five native worker threads.
- Assigns different computational workloads to each thread.
- Measures individual execution time.
- Displays execution results and checksums.

---

## 🛠️ Technologies Used

| Technology | Purpose |
|---|---|
| C (C11) | Core programming language |
| Raylib 5.x | Graphical user interface |
| Pthreads | Native multithreading on macOS/Linux |
| Win32 API | Native thread management on Windows |
| Mutex Locks | Thread synchronization |
| Semaphores | Resource coordination |
| POSIX / Win32 APIs | Cross-platform system-level operations |

---

## 💻 Platform Compatibility

ThreadLab is designed to work across:

- macOS
- Windows
- Linux (with compatible POSIX semaphore support)

Platform-specific threading implementations are handled using conditional compilation.

---

## ⚙️ Prerequisites

Before running ThreadLab, install:

1. A C11-compatible compiler (GCC or Clang).
2. Raylib 5.x.
3. Required native threading libraries (included with the respective operating systems).

---

## 🚀 Installation & Execution

### 1. Clone the Repository

```bash
git clone https://github.com/YOUR-USERNAME/ThreadLab.git
cd ThreadLab
```

### 2. Compile on macOS

```bash
clang ThreadLab_Updated.c -o ThreadLab -std=c11 -lraylib -pthread -lm
```

### 3. Compile on Linux

```bash
gcc ThreadLab_Updated.c -o ThreadLab -std=c11 -lraylib -pthread -lm
```

### 4. Compile on Windows

Using MinGW with Raylib configured:

```bash
gcc ThreadLab_Updated.c -o ThreadLab.exe -std=c11 -lraylib -lopengl32 -lgdi32 -lwinmm
```

### 5. Run the Application

**macOS / Linux:**

```bash
./ThreadLab
```

**Windows:**

```bash
ThreadLab.exe
```

---

## 🎮 How to Use

1. Launch the ThreadLab application.
2. Explore the available modules from the navigation menu.
3. Select a scheduling or synchronization simulation.
4. Interact with the controls to start, pause, step through, or reset simulations.
5. Observe thread execution, synchronization, and resource management visually.

---

## 📚 Operating Systems Concepts Covered

- Multithreading and concurrency
- Thread creation and execution
- CPU scheduling algorithms
- Round Robin scheduling
- Non-preemptive Priority scheduling
- Race conditions
- Critical sections
- Mutex locks
- Semaphores
- Producer-Consumer synchronization
- Deadlocks and circular wait
- Deadlock prevention
- Thread execution-time measurement

---

## 🎯 Project Objective

The primary objective of ThreadLab is to make complex Operating Systems concepts easier to understand through interactive visualization and practical implementation.

Instead of relying only on theoretical explanations, ThreadLab demonstrates how threads execute concurrently, how shared resources are managed, and how synchronization mechanisms help maintain consistency in multithreaded applications.

---

<img width="2560" height="1660" alt="image" src="https://github.com/user-attachments/assets/b2f46a65-7f12-4dfd-9ea7-8102acfbc5a2" />
