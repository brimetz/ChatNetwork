# ChatApp — C++ / Qt 6 Chat Application

A local network chat application built from scratch in C++, featuring a **console server** and a **Qt 6 GUI client** with automatic server discovery.

---

## Overview

This project was built step by step to learn socket programming, Win32 GUI, and Qt 6.  
It covers UDP broadcasting, TCP communication, multi-threading, and cross-framework architecture.

```
┌─────────────────────────────────────────────────────┐
│                    Local Network                     │
│                                                      │
│   ┌──────────────┐   UDP Beacon    ┌──────────────┐ │
│   │              │ ─────────────►  │              │ │
│   │   server.exe │                 │  Qt Client   │ │
│   │   (C++ / Win32)  TCP Chat  ◄── │  (Qt 6)      │ │
│   │              │ ◄────────────►  │              │ │
│   └──────────────┘                 └──────────────┘ │
└─────────────────────────────────────────────────────┘
```

---

## Features

- **Automatic server discovery** via UDP broadcast — no manual IP entry needed
- **Real-time chat** over TCP with multi-client support
- **Color-coded usernames** — each client gets a unique color assigned by the server
- **Qt 6 GUI client** with a server list window and a chat window
- **Console server** with ANSI color support
- **Server naming** — pass a custom name as a launch argument

---

## Project Structure

```
ChatApp/
│
├── common/                       # Shared between server and client
│   ├── common_shared.h           # UdpBeacon struct, ports, protocol tags (no dependencies)
│   ├── common_win32.h            # Win32/Winsock colors and ANSI codes (server only)
│   └── common_qt.h               # QColor palette (Qt client only)
│
├── server/                       # Console server (C++ / Winsock2)
│   └── server.cpp
│
└── client_qt/                    # Qt 6 GUI client
    ├── main.cpp
    ├── NetworkManager.h/.cpp     # QUdpSocket + QTcpSocket, emits Qt signals
    ├── ServerListWindow.h/.cpp   # Server discovery list (QListWidget)
    ├── ChatWindow.h/.cpp         # Chat UI (QTextBrowser + QLineEdit)
    └── ChatClientQt.pro          # qmake project file
```

---

## Architecture

### Server (`server.cpp`)

The server is a pure C++ console application using **Winsock2**.

| Thread | Role |
|--------|------|
| Main thread | `accept()` loop — waits for incoming TCP connections |
| UDP beacon thread | Broadcasts a `UdpBeacon` packet every 2 seconds |
| Per-client thread | One thread per connected client — handles `recv_line()` / `broadcast()` |

**Message protocol** — every line exchanged over TCP follows this format:
```
COLOR R G B          ← server assigns a color to the new client
MSG pseudo text      ← chat message from a client
```

### Client (`client_qt/`)

The Qt client is fully **event-driven** — no manual threads needed.

| Class | Role |
|-------|------|
| `NetworkManager` | Owns `QUdpSocket` and `QTcpSocket`. Emits signals on network events. |
| `ServerListWindow` | Listens to `serverDiscovered` / `serverUpdated` / `serverLost` signals. |
| `ChatWindow` | Listens to `messageReceived` / `colorAssigned` / `connected` signals. |

`QUdpSocket` and `QTcpSocket` are asynchronous — they emit `readyRead()` when data is available, without blocking the UI thread.

### UDP Discovery Flow

```
server.exe                          client Qt
    │                                   │
    │── UdpBeacon broadcast ──────────► │  onUdpDataReceived()
    │   (every 2 seconds)               │      └─ emit serverDiscovered()
    │                                   │          └─ ServerListWindow adds row
    │                                   │
    │   (no beacon for 6 seconds)       │  onCleanupTimer()
    │                                   │      └─ emit serverLost()
    │                                   │          └─ ServerListWindow removes row
```

### TCP Chat Flow

```
client Qt                           server.exe
    │                                   │
    │── connect() ──────────────────►   │  accept() → new thread
    │◄─ COLOR R G B ─────────────────   │  send color
    │── pseudo\n ────────────────────►  │  recv_line() → store pseudo
    │◄─ MSG Server Welcome! ──────────  │  send welcome
    │                                   │
    │── Hello\n ─────────────────────►  │  broadcast("MSG pseudo Hello")
    │◄─ MSG Alice Hello ──────────────  │  (to all other clients)
```

---

## Getting Started

### Prerequisites

| Tool | Version |
|------|---------|
| Visual Studio | 2019 or 2022 (with "Desktop development with C++") |
| Qt | 6.x |
| Qt Creator | 10+ (optional, recommended for the client) |

---

### Build — Server

Open the **Developer Command Prompt for Visual Studio**, then:

```cmd
cd server
cl server.cpp /EHsc /std:c++17 /Feserver.exe /I..\common /link ws2_32.lib
```

Or open the `.sln` in Visual Studio and configure:

| Property | Value |
|----------|-------|
| Additional Dependencies | `ws2_32.lib` |
| Additional Include Directories | path to `common/` |
| Subsystem | `/SUBSYSTEM:CONSOLE` |
| C++ Standard | `/std:c++17` |

---

### Build — Qt Client

Open `client_qt/ChatClientQt.pro` in **Qt Creator**, select a kit (MinGW or MSVC), then press **Ctrl+R**.

Or from the command line:

```cmd
cd client_qt
qmake ChatClientQt.pro
mingw32-make        # MinGW
:: or
nmake               # MSVC
```

---

### Run

**Terminal 1 — Start the server:**
```cmd
server.exe
:: or with a custom name:
server.exe "Bob's Server"
```

Expected output:
```
=== Server "Bob's Server" ===
TCP listen on port 8080
UDP beacon on port    9001
[UDP] Beacon started (port 9001)
```

**Launch the Qt client:**
```cmd
ChatClientQt.exe
```

1. The server appears in the list within ~2 seconds
2. Double-click or click **Join**
3. Enter your username
4. Chat window opens — start messaging

---

## Network Ports

| Port | Protocol | Usage |
|------|----------|-------|
| `8080` | TCP | Chat messages |
| `9001` | UDP | Server discovery (broadcast) |

---

## Key Concepts Learned

| Concept | Where |
|---------|-------|
| Winsock2 socket lifecycle (`socket` → `bind` → `listen` → `accept`) | `server.cpp` |
| UDP broadcast for service discovery | `udp_beacon_thread()` / `NetworkManager` |
| TCP framing — reading line by line | `recv_line()` / `m_tcpBuffer` |
| Multi-threading with `std::thread` and `std::mutex` | `server.cpp` |
| Win32 GUI (`WNDCLASS`, `WndProc`, message loop) | Win32 client (legacy) |
| Qt signals/slots — event-driven networking without threads | `NetworkManager` |
| Qt layouts (`QVBoxLayout`, `QHBoxLayout`) | `ServerListWindow`, `ChatWindow` |
| HTML rendering in `QTextBrowser` | `ChatWindow::appendMessage()` |
| Cross-platform header separation | `common/` |


## WIP: DEV Last console command

cl client/src/main.cpp /EHsc /std:c++17 /Feclient.exe /link ws2_32.lib user32.lib gdi32.lib comctl32.lib
cl server/src/main.cpp /EHsc /std:c++17 /Feserver.exe /link ws2_32.lib user32.lib gdi32.lib comctl32.lib
