# Reliable Serial Port Protocol (Data Link & Application Layer)

A robust, layered communication protocol written in C for reliable file transmission over RS-232 serial interfaces. The system abstracts physical serial communication through a custom Data Link Layer implementing Stop-and-Wait ARQ with deterministic state machine parsing, byte stuffing, error detection, and POSIX-compliant signal handling.

---

## Architecture Overview

The project is structured in two distinct layers following standard networking abstractions:


```

+-------------------------------------------------------+
|                   Application Layer                   |
|  - File segmentation & reassembly                     |
|  - Control packets (START, DATA, END) with TLV format |
|  - Throughput & transmission efficiency benchmarking  |
+-------------------------------------------------------+
|
+-------------------------------------------------------+
|                    Data Link Layer                    |
|  - Connection establishment & teardown (SET, UA, DISC)|
|  - Stop-and-Wait ARQ flow control                     |
|  - Frame framing & byte stuffing (FLAG: 0x7E)         |
|  - Error detection (BCC1 header check, BCC2 payload)  |
|  - POSIX signal alarms for timeout & retransmissions  |
+-------------------------------------------------------+
|
+-------------------------------------------------------+
|                 Serial Hardware / TTY                 |
|  - Non-canonical termios configuration                |
|  - Virtual cable testing via socat loopback           |
+-------------------------------------------------------+

```

---

## Key Features

* **Deterministic State-Machine Parsing:** Robust byte-by-byte frame evaluation resistant to malformed bytes, garbage data, and framing errors.
* **Stop-and-Wait ARQ:** Reliable data transfer supporting sequence numbers (`Ns`), acknowledgments (`RR`), negative acknowledgments (`REJ`), and configurable retransmission thresholds.
* **Error Detection & Framing:** 
  * Header validation via XOR parity checks (`BCC1 = A ^ C`).
  * Payload data integrity check (`BCC2 = XOR(payload)`).
  * Octet-stuffing escape mechanism (`0x7E` and `0x7D` escaped with `0x20` XOR masks).
* **POSIX Timer Handling:** Non-blocking asynchronous alarm handling (`SIGALRM` / `sigaction`) to govern retransmissions on dropped or delayed frames.
* **TLV Application Control Packets:** Metadata handling (file size and file name) encoded via Type-Length-Value structures before and after file stream transfer.
* **Performance Telemetry:** Real-time computation of effective line throughput ($R$) and channel efficiency ($S$).

---

## Frame Specifications

### Supervision & Unnumbered Frames (SU-Frames)
Control frames consist of a fixed 5-byte header/trailer structure:

```

+------+------+------+------+------+
| FLAG |  A   |  C   | BCC1 | FLAG |
| 0x7E | 0x03 | Code | A^C  | 0x7E |
+------+------+------+------+------+

```
* **SET (`0x03`):** Connection initiation
* **UA (`0x07`):** Unnumbered acknowledgment
* **RR0 / RR1 (`0xAA` / `0xAB`):** Receiver ready (acknowledges receipt and requests next frame)
* **REJ0 / REJ1 (`0x54` / `0x55`):** Negative acknowledgment (requests immediate retransmission)
* **DISC (`0x0B`):** Connection disconnect

### Information Frames (I-Frames)
Carries variable-length application payloads protected by dual parity checks and octet stuffing:

```

+------+------+------+------+--------------------------+------+------+
| FLAG |  A   | C(s) | BCC1 | Data Payload (Stuffed)   | BCC2 | FLAG |
| 0x7E | 0x03 | 0x00 | A^C  | Variable (up to max size)| XOR  | 0x7E |
|      |      | 0x80 |      |                          | Data |      |
+------+------+------+------+--------------------------+------+------+

```

---

## Project Structure


```

├── bin/                 # Compiled binaries
├── cable/               # Virtual cable simulator (virtual serial port pair)
├── src/                 # Protocol implementation source files
│   ├── application_layer.c # Packet assembly/disassembly, TLV parser, metrics
│   ├── link_layer.c        # Framing, state machine, Stop-and-Wait ARQ logic
│   ├── main.c              # Application CLI entrypoint and argument parsing
│   └── serial_port.c       # Low-level POSIX termios serial configuration
├── Makefile             # Build automation and execution targets
└── penguin.gif          # Sample binary asset for transmission validation

```

---

## Getting Started

### Prerequisites

* Linux-based operating system
* `gcc` / `clang`
* `make`
* `socat` (required for virtual serial port emulation)

Install `socat` on Debian/Ubuntu:
```bash
sudo apt update && sudo apt install -y socat

```

### Compilation

Build the main executable and virtual cable binary:

```bash
make

```

---

## Running the Protocol

### 1. Initialize the Virtual Cable

Open a dedicated terminal and start the virtual link:

```bash
sudo make run_cable
# Alternatively: sudo ./bin/cable_app

```

This initializes a paired pseudo-terminal loopback interface (typically `/dev/ttyS10` and `/dev/ttyS11`).

### 2. Start the Receiver (Rx)

In a second terminal, launch the receiving application:

```bash
make run_rx
# Alternatively: ./bin/main /dev/ttyS11 9600 rx penguin-received.gif

```

### 3. Start the Transmitter (Tx)

In a third terminal, transmit the target file:

```bash
make run_tx
# Alternatively: ./bin/main /dev/ttyS10 9600 tx penguin.gif

```

### 4. Verify File Integrity

Confirm byte-level integrity between the sent and received binary files:

```bash
make check_files
# Alternatively: diff -s penguin.gif penguin-received.gif

```

---

## Fault Tolerance & Noise Testing

While transmission is active, switch to the virtual cable simulator console to inject real-world line faults:

* Press `0` to simulate a physical cable disconnect.
* Press `2` to inject channel noise and frame corruption.
* Press `1` to return the channel to normal state.

The protocol automatically absorbs dropped frames and corrupted payloads via timeout alarms and `REJ`/retransmission triggers, guaranteeing byte-level transfer completion once the connection recovers.

---

## Performance Metrics

Upon transfer completion, the application outputs telemetry assessing link efficiency:

```text
                  File Size (bits)
Throughput (R) = -------------------  (bps)
                  Runtime (seconds)

Efficiency (S) =  R / Baud Rate
