// NetworkManager.h
// Manages the entire client network layer :
//   - UDP listener to discover servers (QUdpSocket)
//   - TCP connection for chat (QTcpSocket)
//
// KEY CONCEPT : Qt signals
// ========================
//
// Instead of threads + PostMessage (Win32), Qt uses the
// signals/slots system. When a network event occurs (data
// received, connection established, error), NetworkManager emits a signal.
//
// Windows (ServerListWindow, ChatWindow) connect their slots
// to these signals — Qt automatically calls the slot in the correct thread.
//
// QUdpSocket and QTcpSocket are asynchronous: they do not need
// a dedicated thread. The readyRead() signal is emitted by Qt when
// data is available, without blocking the UI thread.

#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QColor>

// ============================================================
// STRUCTURE: Information from a discovered server
// ============================================================
struct ServerInfo
{
    QString name;
    QString ip;
    int     clientCount = 0;
    qint64  lastSeen    = 0; // timestamp QDateTime::currentMSecsSinceEpoch()
};

// ============================================================
// NetworkManager
//
// Inherits from QObject — required to use signals/slots.
// Q_OBJECT in the .h file activates Qt's MOC (Meta-Object Compiler)
// which generates the dispatch code for signals/slots.
// ============================================================
class NetworkManager : public QObject
{
    Q_OBJECT // Macro required for signals/slots

public:
    explicit NetworkManager(QObject* parent = nullptr);
    ~NetworkManager();

    // Starts UDP listening (server discovery)
    void startDiscovery();

    // Stop listening to UDP
    void stopDiscovery();

    // Connects to a TCP server and sends the nickname
    // Returns false if the connection fails immediately
    void connectToServer(const QString& ip, const QString& pseudo);

    // Sends a chat message (automatically adds '\n')
    void sendMessage(const QString& text);

    // Disconnect the TCP socket properly
    void disconnectFromServer();

    // True if the TCP socket is connected
    bool isConnected() const;

    // Called when User want to refresh the list
    void refreshServerlist() {m_servers.clear();}

    auto& getServersKnown() {return m_servers;}

// ============================================================
// SIGNALS
// A signal is a function declared but never implemented
// by us — Qt generates the implementation via the MOC.
// We "emit" a signal with `emit`, Qt calls all connected slots.
// ============================================================
signals:
    // Issued when a new UDP server is discovered
    void serverDiscovered(const ServerInfo& server);

    // Issued when the information on a known server changes (number of clients, etc.)
    void serverUpdated(const ServerInfo& server);

    // Issued when a server disappears (no beacon for 6 seconds)
    void serverLost(const QString& ip);

    // Issued when the TCP connection is established
    void connected();

    // Issued when the TCP connection is lost
    void disconnected();

    // Emitted when the server sends our assigned color
    void colorAssigned(QString pseudo, const QColor& color);

    // Emit when the server sends a disconnection message
    void deconnectionReceived(QString pseudo);

    // Issued for each chat message received
    void messageReceived(const QString& pseudo, const QString& text);

    // Issued in case of network error
    void networkError(const QString& message);

// ============================================================
// PRIVATE SLOTS
// A slot is a normal method that can be connected
// to a signal. Qt will automatically call it when the associated signal
// is emitted.
// ============================================================
private slots:
    // Called by QUdpSocket::readyRead() when a beacon arrives
    void onUdpDataReceived();

    // QTimer calls every 2 seconds to clean up missing servers
    void onCleanupTimer();

    // Called by QTcpSocket::connected()
    void onTcpConnected();

    // Called by QTcpSocket::readyRead() when TCP data arrives
    void onTcpDataReceived();

    // Called by QTcpSocket::disconnected()
    void onTcpDisconnected();

    // Called by QTcpSocket::errorOccurred()
    void onTcpError(QAbstractSocket::SocketError error);

private:
    // Parses a line received from the server ("COLOR ...", "MSG ...")
    void parseLine(const QString& line);

    QUdpSocket* m_udpSocket  = nullptr;
    QTcpSocket* m_tcpSocket  = nullptr;
    QTimer*     m_cleanupTimer = nullptr;

    // TCP receive buffer — TCP data may arrive
    // in several pieces, we accumulate until we find '\n'
    QString m_tcpBuffer;

    // Username pending sending (sent after TCP connection established)
    QString m_pendingPseudo;

    // List of discovered servers (key = IP)
    QMap<QString, ServerInfo> m_servers;
};
