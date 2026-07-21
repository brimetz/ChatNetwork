// NetworkManager.cpp

#include "NetworkManager.h"
#include <QDateTime>
#include <QHostAddress>
#include <QNetworkDatagram>

#include "common_shared.h"

// ============================================================
// CONSTRUCTOR
// ============================================================
NetworkManager::NetworkManager(QObject* parent)
    : QObject(parent)
{
    // --- UDP Socket ---
    // QUdpSocket: Connectionless UDP socket
    // We create it here but don't bind it yet (done in startDiscovery)
    m_udpSocket = new QUdpSocket(this);
    // "this" = Qt parent → Qt will destroy m_udpSocket when NetworkManager is destroyed
    // This is Qt's ownership system — no need for manual deletion

    // --- Cleaning timer ---
    // QTimer emits timeout() at regular intervals
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(2000); // every 2 seconds

    // --- TCP Socket ---
    m_tcpSocket = new QTcpSocket(this);

    // ============================================================
    // SIGNAL/SLOT CONNECTIONS
    // connect(sender, signal, receiver, slot)
    // When the sender emits the signal, Qt calls the receiver's slot.
    // The member-pointer syntax (Qt5+) is checked at compile time:
    // connect(obj, &Class::signal, this, &MyClass::slot)
    // The old SIGNAL()/SLOT() syntax is only checked at runtime.
    // ============================================================

    // UDP → reading slot
    connect(m_udpSocket, &QUdpSocket::readyRead,
            this, &NetworkManager::onUdpDataReceived);

    // Timer → cleaning slot
    connect(m_cleanupTimer, &QTimer::timeout,
            this, &NetworkManager::onCleanupTimer);

    // TCP → events slot
    connect(m_tcpSocket, &QTcpSocket::connected,
            this, &NetworkManager::onTcpConnected);

    connect(m_tcpSocket, &QTcpSocket::readyRead,
            this, &NetworkManager::onTcpDataReceived);

    connect(m_tcpSocket, &QTcpSocket::disconnected,
            this, &NetworkManager::onTcpDisconnected);

    // errorOccurred has a parameter — we specify the type to remove the ambiguity
    connect(m_tcpSocket, &QTcpSocket::errorOccurred,
            this, &NetworkManager::onTcpError);
}

NetworkManager::~NetworkManager()
{
    // Qt automatically destroys child objects (sockets, timers)
    // because they were created with "this" as their parent.
    // No need for explicit deletion here.
    stopDiscovery();
}

// ============================================================
// UDP DISCOVERY
// ============================================================
void NetworkManager::startDiscovery()
{
    // bind() on INADDR_ANY:UDP_PORT to receive broadcasts
    // QHostAddress::AnyIPv4 = 0.0.0.0 = all interfaces
    if (!m_udpSocket->bind(QHostAddress::AnyIPv4,
                           UDP_PORT,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        // ShareAddress = SO_REUSEADDR : Multiple apps can listen on the same port.
        emit networkError("UDP bind failed : " + m_udpSocket->errorString());
        return;
    }

    m_cleanupTimer->start();
}

void NetworkManager::stopDiscovery()
{
    m_cleanupTimer->stop();
    m_udpSocket->close();
}

// ============================================================
// SLOT: UDP data received
// Automatically called by Qt when readyRead() is issued.
// ============================================================
void NetworkManager::onUdpDataReceived()
{
    // hasPendingDatagrams(): true if there is at least one datagram pending
    // We loop because several datagrams can arrive between two calls
    while (m_udpSocket->hasPendingDatagrams())
    {
        // receiveDatagram() = recvfrom() in Qt
        // QNetworkDatagram contains the data + the source address
        QNetworkDatagram datagram = m_udpSocket->receiveDatagram();

        // Check the package size
        if (datagram.data().size() != sizeof(UdpBeacon))
            continue;

        // Extract the UdpBeacon structure from the raw bytes
        UdpBeacon beacon;
        memcpy(&beacon, datagram.data().constData(), sizeof(UdpBeacon));
        beacon.server_name[MAX_SERVER_NAME - 1] = '\0'; // sécurité

        QString ip   = datagram.senderAddress().toString();
        // Qt sometimes prefixes IPv4 addresses with "::ffff:" (IPv4-mapped IPv6)
        // We remove it to get a clean IP address like "192.168.1.10"
        if (ip.startsWith("::ffff:"))
            ip = ip.mid(7);

        QString name = QString::fromLocal8Bit(beacon.server_name);
        qint64  now  = QDateTime::currentMSecsSinceEpoch();

        if (m_servers.contains(ip))
        {
            // Server already known → update
            ServerInfo& s = m_servers[ip];
            bool changed = (s.name != name || s.clientCount != beacon.client_count);
            s.name        = name;
            s.clientCount = beacon.client_count;
            s.lastSeen    = now;

            if (changed)
                emit serverUpdated(s); // signal → ServerListWindow will update the line
        }
        else
        {
            // New server
            ServerInfo s;
            s.ip          = ip;
            s.name        = name;
            s.clientCount = beacon.client_count;
            s.lastSeen    = now;
            m_servers[ip] = s;

            emit serverDiscovered(s); // signal → ServerListWindow will add the line
        }
    }
}

// ============================================================
// SLOT: Cleanup of missing servers
// Called every 2 seconds by the QTimer
// ============================================================
void NetworkManager::onCleanupTimer()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // QMap::iterator — We iterate and delete servers that are too old.
    auto it = m_servers.begin();
    while (it != m_servers.end())
    {
        if ((now - it->lastSeen) > 6000) // 6 seconds = 3 missed beacons
        {
            it = m_servers.erase(it);
            emit serverLost(it->ip); // signal → ServerListWindow will remove the line
        }
        else
        {
            ++it;
        }
    }
}

// ============================================================
// TCP CONNECTION
// ============================================================
void NetworkManager::connectToServer(const QString& ip, const QString& pseudo)
{
    m_pendingPseudo = pseudo; // will be send in onTcpConnected()
    m_tcpBuffer.clear();

    // connectToHost() is non-blocking in Qt.
    // It initiates the connection and returns immediately.
    // The connected() signal will be emitted when the connection is established.
    m_tcpSocket->connectToHost(ip, TCP_PORT);
}

// ============================================================
// SLOT: TCP connection established
// ============================================================
void NetworkManager::onTcpConnected()
{
    // The server first waits for the username (without a PSEUDO_REQUEST prompt
    // in this version — it's sent directly)
    sendMessage(m_pendingPseudo);
    emit connected();
}

// ============================================================
// SLOT: TCP data received
// IMPORTANT: Qt can call this slot multiple times for a single
// message, or once for multiple messages.
// → We accumulate in m_tcpBuffer and split on '\n'.
// ============================================================
void NetworkManager::onTcpDataReceived()
{
    // readAll() reads everything available in the TCP buffer
    m_tcpBuffer += QString::fromUtf8(m_tcpSocket->readAll());

    // Split on '\n' to process complete lines
    // As long as there is a '\n' in the buffer...
    while (true)
    {
        int idx = m_tcpBuffer.indexOf('\n');
        if (idx < 0) break; // not yet a complete line

        QString line = m_tcpBuffer.left(idx).trimmed();
        m_tcpBuffer  = m_tcpBuffer.mid(idx + 1); // remainder of the buffer

        if (!line.isEmpty())
            parseLine(line);
    }
}

// ============================================================
// PARSING A SERVER LINE
// Format: "TAG param1 param2 ..."
// ============================================================
void NetworkManager::parseLine(const QString& line)
{
    // split(' ', Qt::SkipEmptyParts) = split on spaces
    // ignoring multiple consecutive spaces
    QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;

    const QString& tag = parts[0];

    // --- "COLOR R G B" ---
    if (tag == TAG_COLOR && parts.size() >= 5)
    {
        int r = parts[2].toInt();
        int g = parts[3].toInt();
        int b = parts[4].toInt();
        emit colorAssigned(parts[1], QColor(r, g, b));
        return;
    }

    // --- "DECONNECTION pseudo" ---
    if (tag == TAG_DECONNECTION && parts.size() >= 2)
    {
        emit deconnectionReceived(parts[1]);
        return;
    }

    // --- "MSG pseudo text" ---
    if (tag == TAG_MSG && parts.size() >= 3)
    {
        QString pseudo = parts[1];

        // The text = everything following the username (may contain spaces)
        // It is reconstructed from index 2
        // "MSG Alice Hello everyone" → username="Alice", text="Hello everyone"
        QString text = parts.mid(2).join(' ');

        emit messageReceived(pseudo, text);
        return;
    }
}

// ============================================================
// SLOTS TCP DIVERS
// ============================================================
void NetworkManager::onTcpDisconnected()
{
    emit disconnected();
}

void NetworkManager::onTcpError(QAbstractSocket::SocketError error)
{
    // We ignore the intentional disconnection error (RemoteHostClosedError)
    // to avoid duplication with onTcpDisconnected()
    if (error == QAbstractSocket::RemoteHostClosedError) return;
    emit networkError(m_tcpSocket->errorString());
}

void NetworkManager::sendMessage(const QString& text)
{
    if (!m_tcpSocket || m_tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    // toUtf8(): converts QString (internal Qt UTF-16) to UTF-8 for the network
    QByteArray data = (text + "\n").toUtf8();
    m_tcpSocket->write(data);
    // In Qt, `write()` is non-blocking: it queues the data
    // and sends it as soon as the socket is ready
}

void NetworkManager::disconnectFromServer()
{
    if (m_tcpSocket)
        m_tcpSocket->disconnectFromHost();
}

bool NetworkManager::isConnected() const
{
    return m_tcpSocket &&
           m_tcpSocket->state() == QAbstractSocket::ConnectedState;
}
