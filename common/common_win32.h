// common_win32.h
// Couleurs spécifiques Win32 (COLORREF) et ANSI.
// Inclus UNIQUEMENT par le serveur.

#pragma once
#include "common_shared.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>

// ============================================================
// ANSI COLOR (console server)
// ============================================================
const std::string ANSI_COLORS[] =
{
    "\033[31m",  // Red
    "\033[32m",  // Green
    "\033[33m",  // Yellow
    "\033[34m",  // Blue
    "\033[35m",  // Violet
    "\033[36m",  // Cyan
    "\033[91m",  // Light Red
    "\033[92m",  // Light Green
    "\033[93m",  // Light Yellow
    "\033[94m",  // Light Blue
};
const int ANSI_COLOR_COUNT = sizeof(ANSI_COLORS) / sizeof(ANSI_COLORS[0]);
const std::string ANSI_RESET = "\033[0m";

// ============================================================
// Win32 Colors (COLORREF)
// ============================================================
const COLORREF WIN32_COLORS[] =
{
    RGB(220,  50,  50),
    RGB(50, 180,  50),
    RGB(200, 160,  30),
    RGB(50, 100, 220),
    RGB(160,  50, 200),
    RGB(30, 180, 180),
    RGB(255, 100, 100),
    RGB(100, 220, 100),
    RGB(240, 210,  60),
    RGB(100, 149, 255),
};
const int WIN32_COLOR_COUNT = sizeof(WIN32_COLORS) / sizeof(WIN32_COLORS[0]);
