# ChatNetwork — C++ / Qt 6 Chat Application

A local network chat application built from scratch in C++, featuring a **console server** and a **Qt 6 GUI client** with automatic server discovery.

---

## Overview

```
┌─────────────────────────────────────────────────────┐
│                    Local Network                     │
│                                                      │
│   ┌──────────────┐   UDP Beacon    ┌──────────────┐ │
│   │              │ ─────────────►  │              │ │
│   │   server     │                 │  Qt Client   │ │
│   │   (C++/Win32)│  ◄── TCP ────►  │  (Qt 6)      │ │
│   │              │                 │              │ │
│   └──────────────┘                 └──────────────┘ │
└─────────────────────────────────────────────────────┘
```

The server broadcasts its presence every 2 seconds over UDP. The Qt client listens, displays available servers, and connects via TCP to chat.

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
ChatNetwork/
│
├── README.md
├── .gitignore
│
├── common/                         # Shared headers (no external dependencies)
│   ├── common_shared.h             # UdpBeacon struct, ports, protocol tags
│   ├── common_win32.h              # COLORREF, Winsock colors (server only)
│   └── common_qt.h                 # QColor palette (Qt client only)
│
├── server/                         # Console server — Visual Studio project
│   ├── ChatServer.sln
│   ├── ChatServer/
│   │   ├── ChatServer.vcxproj
│   │   └── ChatServer.vcxproj.filters
│   └── src/
│       └── server.cpp
│
└── client/
    ├── qt/                         # Qt 6 GUI client — Qt Creator project
    │   ├── ChatClientQt.pro
    │   └── src/
    │       ├── main.cpp
    │       ├── NetworkManager.h/.cpp
    │       ├── ServerListWindow.h/.cpp
    │       └── ChatWindow.h/.cpp
    └── win32/                      # Legacy Win32 client (reference only)
        ├── client.sln
        └── ChatClient/
            └── src/
                └── main.cpp
```

---

## Architecture

### Server (`server/src/server.cpp`)

Pure C++ console application using **Winsock2**.

| Thread | Role |
|--------|------|
| Main thread | `accept()` loop — waits for incoming TCP connections |
| UDP beacon thread | Broadcasts a `UdpBeacon` packet every 2 seconds on port 9001 |
| Per-client thread | One thread per connected client — handles recv/broadcast |

### Qt Client (`client/qt/`)

Fully **event-driven** — no manual threads needed.

| Class | Role |
|-------|------|
| `NetworkManager` | Owns `QUdpSocket` and `QTcpSocket`. Emits signals on network events. |
| `ServerListWindow` | Listens to `serverDiscovered` / `serverUpdated` / `serverLost` signals. |
| `ChatWindow` | Listens to `messageReceived` / `colorAssigned` / `connected` signals. |

### Message Protocol

Every line exchanged over TCP follows this format:
```
COLOR R G B          ← server assigns a RGB color to the new client
MSG pseudo text      ← chat message (pseudo = sender's username)
```

### Network Ports

| Port | Protocol | Usage |
|------|----------|-------|
| `8080` | TCP | Chat messages |
| `9001` | UDP | Server discovery (broadcast) |

---

## Prerequisites

| Tool | Version | Used for |
|------|---------|----------|
| Visual Studio | 2019 or 2022 | Server compilation |
| "Desktop development with C++" workload | — | Required by Visual Studio |
| Qt | 6.x | Client compilation |
| Qt Creator | 10+ | Client IDE (recommended) |

---

## Build & Run

### Server

**Option A — Visual Studio (recommended for debugging)**

1. Open `server/ChatServer.sln` in Visual Studio
2. Set configuration to **Debug** or **Release**, platform **x64**
3. Press **Ctrl+Shift+B** to build
4. Press **F5** to run with debugger, or **Ctrl+F5** without

The executable is output to:
```
server/x64/Debug/ChatServer.exe
```

**Option B — Developer Command Prompt**

Open the *Developer Command Prompt for Visual Studio*, then:

```cmd
cd server\src
cl server.cpp /EHsc /std:c++17 /FeChatServer.exe /I..\..\common /link ws2_32.lib
```

**Run with a custom server name:**
```cmd
ChatServer.exe "Bob's Server"
```

Expected output:
```
=== Server "Bob's Server" ===
TCP listen on port 8080
UDP beacon on port    9001
[UDP] Beacon started (port 9001)
```

---

### Qt Client

**Option A — Qt Creator (recommended)**

1. Open `client/qt/ChatClientQt.pro` in Qt Creator
2. Select a kit (MinGW 64-bit or MSVC)
3. Press **Ctrl+R** to build and run

The executable is output to:
```
client/qt/build/Desktop_Qt_6_x_x_MinGW_64bit_Debug/debug/ChatClientQt.exe
```

**Option B — Command line (MinGW)**

```cmd
cd client\qt
qmake ChatClientQt.pro
mingw32-make
```

**Option B — Command line (MSVC)**

```cmd
cd client\qt
qmake ChatClientQt.pro
nmake
```

---

### Running the Application

1. **Start the server first** (in any terminal):
```cmd
server\x64\Debug\ChatServer.exe
```

2. **Launch one or more Qt clients:**
```cmd
client\qt\build\...\ChatClientQt.exe
```

3. In the client window:
   - Wait ~2 seconds — the server appears in the list automatically
   - Double-click the server or click **Join**
   - Enter a username
   - Start chatting

---

## Firewall

If the connection is blocked by Windows Firewall, run these commands as administrator:

```cmd
netsh advfirewall firewall add rule name="ChatNetwork TCP" dir=in action=allow protocol=TCP localport=8080
netsh advfirewall firewall add rule name="ChatNetwork UDP" dir=in action=allow protocol=UDP localport=9001
```

---

## Visual Studio Project Configuration (server)

If you recreate the Visual Studio project from scratch, apply these settings:

> Right-click project → **Properties** → set *Configuration* to **All Configurations**

| Category | Property | Value |
|----------|----------|-------|
| General | C++ Standard | `/std:c++17` |
| C/C++ → General | Additional Include Directories | `..\..\common` |
| Linker → Input | Additional Dependencies | `ws2_32.lib` |
| Linker → System | Subsystem | `Console (/SUBSYSTEM:CONSOLE)` |

---

## Qt Creator Project Configuration (client)

Everything is configured in `client/qt/ChatClientQt.pro`:

```pro
QT += core gui widgets network
CONFIG += c++17
INCLUDEPATH += ../../common

SOURCES += \
    src/main.cpp \
    src/NetworkManager.cpp \
    src/ServerListWindow.cpp \
    src/ChatWindow.cpp

HEADERS += \
    src/NetworkManager.h \
    src/ServerListWindow.h \
    src/ChatWindow.h \
    ../../common/common_shared.h \
    ../../common/common_qt.h
```

After editing the `.pro`, click **"Yes"** on the reload banner in Qt Creator, or right-click the project → **Run qmake**.

---

## Key Concepts

| Concept | Where |
|---------|-------|
| Winsock2 socket lifecycle (`socket` → `bind` → `listen` → `accept`) | `server/src/server.cpp` |
| UDP broadcast for service discovery | `udp_beacon_thread()` / `NetworkManager` |
| TCP framing — accumulating data until `\n` | `recv_line()` / `m_tcpBuffer` |
| Multi-threading with `std::thread` and `std::mutex` | `server/src/server.cpp` |
| Win32 GUI (`WNDCLASS`, `WndProc`, message loop) | `client/win32/` (legacy) |
| Qt signals/slots — event-driven networking without threads | `NetworkManager` |
| Qt layouts (`QVBoxLayout`, `QHBoxLayout`) | `ServerListWindow`, `ChatWindow` |
| HTML rendering in `QTextBrowser` | `ChatWindow::appendMessage()` |
| Cross-platform header separation | `common/` |
