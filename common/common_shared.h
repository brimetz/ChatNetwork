// common_shared.h - structures and variables shared

#pragma once

// ============================================================
// PORTS
// ============================================================
constexpr int TCP_PORT        = 8080;
constexpr int UDP_PORT        = 9001;
constexpr int UDP_BEACON_MS   = 2000;
constexpr int MAX_PSEUDO      = 20;
constexpr int MAX_SERVER_NAME = 32;

// ============================================================
// UDP BEACON PACJET
// Same structure as the C++ server, must remain identical because it is sent RAW over the network

// #pragma pack(1) guarantees that there is no padding,
// therefore the size and arrangement of the corresponding bytes
// exactly between the server (MSCV) and the client (Qt/MinGW)
// ============================================================
#pragma pack(push, 1)
struct UdpBeacon
{
    char server_name[MAX_SERVER_NAME];
    int  client_count;
};
#pragma pack(pop)

// ============================================================
// MESSAGE PROTOCOL (same format as the server)
// Each line received from the server begins with a tag:
//   "COLOR R G B"       → color assigned to this client
//   "MSG pseudo text"  → chat message
//   "DECONNECTION pseudo"  → Deconnection message
// ============================================================
constexpr char TAG_COLOR[] = "COLOR";
constexpr char TAG_MSG[] = "MSG";
constexpr char TAG_DECONNECTION[]   = "DECONNECTION";
