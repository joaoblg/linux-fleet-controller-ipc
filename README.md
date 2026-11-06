# Linux Fleet Mobility Controller & Multi-Process IPC System

[![C](https://img.shields.io/badge/Language-C99-00599C?style=for-the-badge&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![POSIX](https://img.shields.io/badge/Standard-POSIX%20%2F%20Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)
[![IPC](https://img.shields.io/badge/Architecture-Named_Pipes_(FIFOs)%20%26%20Signals-E95420?style=for-the-badge)](#)
[![License](https://img.shields.io/badge/Academic-ISEC_LEI-green?style=for-the-badge)](#)

A concurrent, distributed multi-process fleet management and ride dispatch system built in **C99** on native **Linux / POSIX APIs** for the **Operating Systems I (SO)** course in Computer Engineering at **ISEC (Instituto Superior de Engenharia de Coimbra)**.

The project models an on-demand urban transport service (similar to a ride-hailing / taxi platform) coordinating independent client requests, autonomous vehicle processes, and a central controller engine using asynchronous UNIX IPC primitives.

---

## 🏛️ System Architecture & IPC Primitives

```
   ┌────────────────────────────────┐
   │            CLIENTS             │
   │      (cliente 1, 2, ... N)     │
   └───────────────▲────────────────┘
                   │
  Private FIFOs    │  Well-known Request FIFO
  (/tmp/cli_*_fifo)│  (/tmp/taxi_fifo)
                   │
                   ▼
       ┌────────────────────────┐
       │       CONTROLADOR      │
       │ (Central Fleet Manager)│
       └───────────▲────────────┘
                   │
  Private FIFOs    │  Registration & Status FIFO
  (/tmp/vei_*_fifo)│  (/tmp/taxi_fifo)
                   │
                   ▼
   ┌────────────────────────────────┐
   │            VEHICLES            │
   │      (veiculo 1, 2, ... M)     │
   └────────────────────────────────┘
```

### Technical Highlights:
* **Asynchronous Communication via FIFOs:** 
  * Main intake via dedicated public FIFO (`/tmp/taxi_fifo`).
  * Dedicated, private return FIFOs generated per process PID (`/tmp/cli_<PID>_fifo` and `/tmp/vei_<PID>_fifo`) to eliminate race conditions and interleaving.
* **POSIX Signal Handling:** Graceful system shutdown and dynamic notification using POSIX signals (`SIGINT`, `SIGUSR1`).
* **Concurrency & Coordination:** Synchronous and asynchronous dispatching routines ensuring vehicle availability, ride assignments, and trip completion tracking.

Detailed architecture and state diagrams are provided in the [`RelatorioTrabalhoPraticoSO.pdf`](RelatorioTrabalhoPraticoSO.pdf).

---

## 🚀 Building & Running

### Requirements
* Linux environment (Ubuntu, Debian, Fedora, Arch, or WSL2)
* GCC compiler with `pthread` support
* Make utility

### Compiling
```bash
# Clone the repository
git clone https://github.com/joaoblg/linux-fleet-controller-ipc.git
cd linux-fleet-controller-ipc

# Compile all modules via Makefile
make
```

### Running the System

1. **Launch the Central Controller:**
   ```bash
   ./controlador
   ```

2. **Register one or more Vehicles (in separate terminals):**
   ```bash
   ./veiculo
   ```

3. **Issue ride requests from Clients:**
   ```bash
   ./cliente
   ```

4. **Clean up binary and temporary FIFO files:**
   ```bash
   make clean
   ```

---

## 👨‍💻 Author & Academic Context

* **João Bernardo Graça**

Developed as part of the **Degree in Computer Engineering (LEI)** - *Application Development (DA) branch* at **ISEC (Instituto Superior de Engenharia de Coimbra)**.
