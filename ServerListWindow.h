// ServerListWindow.h
// Main window: List of discovered UDP servers.
// Inherits from QWidget — the base class for all Qt widgets.

#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMap>
#include "NetworkManager.h"

class ServerListWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ServerListWindow(QWidget* parent = nullptr);
    ~ServerListWindow();

private slots:
    // Connected to NetworkManager signals
    void onServerDiscovered(const ServerInfo& server);
    void onServerUpdated(const ServerInfo& server);
    void onServerLost(const QString& ip);

    // Connected to the Join button click + double-click list
    void onJoinClicked();

    // Enables/disables the Join button according to the selection
    void onSelectionChanged();

private:
    void setupUi();
    void setupConnections();

    // Returns the ServerInfo of the selected row
    // (the IP address is stored in Qt::UserRole for each QListWidgetItem)
    ServerInfo* selectedServer();

    NetworkManager*  m_network    = nullptr;
    QListWidget*     m_list       = nullptr;
    QPushButton*     m_joinBtn    = nullptr;
    QPushButton*     m_refreshBtn = nullptr;
    QLabel*          m_statusLbl  = nullptr;
};
