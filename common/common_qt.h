// common.h - Structures et constantes partagées
// Version Qt : on retire toutes les dépendances Win32 (COLORREF, winsock, etc.)
// On utilise QColor à la place de COLORREF

#pragma once

#include <QString>
#include <QColor>
#include <QList>

// ============================================================
// COLORS
// We use QColor
// The order corresponds to the ANSI_COLORS of the server.
// The client at index 2 will have the same color on both sides
// ============================================================
inline QList<QColor> chatColors()
{
    return {
        QColor(220,  50,  50),  // Red
        QColor( 50, 180,  50),  // Green
        QColor(200, 160,  30),  // Yellow
        QColor( 50, 100, 220),  // Blue
        QColor(160,  50, 200),  // Violet
        QColor( 30, 180, 180),  // Cyan
        QColor(255, 100, 100),  // Light Red
        QColor(100, 220, 100),  // Light Green
        QColor(240, 210,  60),  // Light Yellow
        QColor(100, 149, 255),  // Light Blue
    };
}
