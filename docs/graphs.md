# 📘 **1. Arquitetura Geral (Master + Workers + Threads)**

```mermaid
flowchart LR
    A[Cliente HTTP] -->|TCP Conexão| B(Master Process)

    subgraph MASTER[Master Process]
        B --> C["Shared Connection Queue<br>(Shared Memory)"]
    end

    subgraph WORKERS[Worker Processes]
        direction LR
        W1[Worker 1] --> T1a[Thread 1]
        W1 --> T1b[Thread 2]
        W1 --> T1c[Thread N]

        W2[Worker 2] --> T2a[Thread 1]
        W2 --> T2b[Thread 2]
        W2 --> T2c[Thread N]

        WN[Worker N] --> TNa[Thread 1]
        WN --> TNb[Thread 2]
        WN --> TNc[Thread N]
    end

    C --> W1
    C --> W2
    C --> WN
```

---

# 📗 **2. Producer-Consumer com Semáforos (Fila de Ligações)**

```mermaid
sequenceDiagram
    participant Master as Master Process
    participant Queue as Shared Connection Queue
    participant Worker as Worker Process

    Master->>Queue: sem_wait(empty_slots)
    Master->>Queue: sem_wait(mutex)
    Master->>Queue: Enqueue(socket_fd)
    Master->>Queue: sem_post(mutex)
    Master->>Queue: sem_post(filled_slots)

    Worker->>Queue: sem_wait(filled_slots)
    Worker->>Queue: sem_wait(mutex)
    Worker->>Queue: Dequeue(socket_fd)
    Worker->>Queue: sem_post(mutex)
    Worker->>Queue: sem_post(empty_slots)

    Worker->>Worker: Envia FD para thread pool
```

---

# 📙 **3. Estrutura Interna do Worker (Thread Pool)**

```mermaid
flowchart TD
    A[Worker Process] --> B[Thread Pool]

    B --> T1[Thread 1]
    B --> T2[Thread 2]
    B --> T3[Thread 3]
    B --> TN[Thread N]

    subgraph Queue[Local Task Queue]
        Q1[(mutex)]
        Q2[(cond_full/cond_empty)]
    end

    A --> Queue
    Queue --> T1
    Queue --> T2
    Queue --> T3
    Queue --> TN
```

---

# 📕 **4. Fluxo Completo do Pedido HTTP**

```mermaid
sequenceDiagram
    participant Client
    participant Master as Master Process
    participant Queue as Connection Queue
    participant Worker as Worker
    participant Thread as Worker Thread
    participant Cache as File Cache

    Client->>Master: TCP Connection
    Master->>Queue: Enqueue fd
    Worker->>Queue: Dequeue fd
    Worker->>Thread: Assign fd to thread

    Thread->>Thread: Parse HTTP Request
    Thread->>Cache: Lookup file in LRU cache
    alt Cache Hit
        Cache-->>Thread: Return cached content
    else Cache Miss
        Thread->>Disk: Read file
        Thread->>Cache: Insert in cache (writer lock)
    end

    Thread->>Client: Send HTTP Response
    Thread->>Worker: Update Shared Statistics
```

---

# 📓 **5. LRU File Cache (Estrutura e Sincronização)**

```mermaid
flowchart TD
    subgraph Cache[LRU Cache]
        direction LR
        A1[Hash Table] --> A2["Doubly Linked List<br>(LRU Order)"]
    end

    subgraph Locks[Cache Locks]
        RWLock[Reader-Writer Lock]
    end

    T1[Thread] -->|rdlock| Cache
    T2[Thread] -->|rdlock| Cache
    T3[Thread] -->|wrlock| Cache
```

---

# 📒 **6. Estrutura das Estatísticas Partilhadas**

```mermaid
flowchart TB
    Master[Master Process] --> Stats[(Shared Statistics)]
    Worker1[Worker 1] --> Stats
    Worker2[Worker 2] --> Stats
    WorkerN[Worker N] --> Stats

    Stats -->|"Protected by<br>POSIX Semaphore"| Lock
```

---

# 📘 **7. Diagrama de Estados do Thread Pool**

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Waiting: cond_wait
    Waiting --> Working: New FD arrives
    Working --> Idle: Task finished
    Working --> Shutdown: Server closing
    Idle --> Shutdown: Server closing
```
