// Struct and constantes shared between server and client

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>

// ============================================================
// Ports
// TCP_PORT : Port where the server accept chat clients 
// UDP_PORT : port where the server send its beacon and where clients listen servers
// ============================================================
constexpr int TCP_PORT			= 8080; 
constexpr int UDP_PORT			= 9001;
constexpr int UDP_BEACON_MS		= 2000;		// intervalle d'émission du beacon (ms)
constexpr int MAX_PSEUDO		= 20;		// longueur max d'un pseudo
constexpr int MAX_SERVER_NAME	= 32;		// longueur max du nom du serveur

// ============================================================
// Paquet UDP Beacon
// struct send in broadcast by a server each 2 seconds
// Client will receive it and use it to get server information 
// and display the server in the server available list
// 
// Need a fix size struct so no std::string
// each member has a known size at compilation
// ============================================================
#pragma pack(push, 1) // disable padding, without that, compilator can insert some octets to align members
struct UdpBeacon
{
	char server_name[MAX_SERVER_NAME];
	int client_count;
};
#pragma pack(pop) // enable padding

// ============================================================
// ANSI Colors
// ============================================================
const std::string ANSI_COLORS[] = 
{
    "\033[31m",  // Rouge
    "\033[32m",  // Vert
    "\033[33m",  // Jaune
    "\033[34m",  // Bleu
    "\033[35m",  // Magenta
    "\033[36m",  // Cyan
    "\033[91m",  // Rouge clair
    "\033[92m",  // Vert clair
    "\033[93m",  // Jaune clair
    "\033[94m",  // Bleu clair
};
const int ANSI_COLOR_COUNT = sizeof(ANSI_COLORS) / sizeof(ANSI_COLORS[0]);
const std::string ANSI_RESET = "\033[0m";

// ============================================================
// Win32 Colors
// ============================================================
const COLORREF WIN32_COLORS[] = {
    RGB(220,  50,  50),  // Rouge
    RGB(50, 180,  50),  // Vert
    RGB(200, 160,  30),  // Jaune
    RGB(50, 100, 220),  // Bleu
    RGB(160,  50, 200),  // Magenta
    RGB(30, 180, 180),  // Cyan
    RGB(255, 100, 100),  // Rouge clair
    RGB(100, 220, 100),  // Vert clair
    RGB(240, 210,  60),  // Jaune clair
    RGB(100, 149, 255),  // Bleu clair
};
const int WIN32_COLOR_COUNT = sizeof(WIN32_COLORS) / sizeof(WIN32_COLORS[0]);