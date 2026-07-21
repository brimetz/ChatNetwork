// ChatWindow.cpp

#include "ChatWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QScrollBar>
#include <QDateTime>

// ============================================================
// CONSTRUCTOR
// ============================================================
ChatWindow::ChatWindow(const ServerInfo& server,
                       const QString&    pseudo,
                       NetworkManager*   network,
                       QWidget*          parent)
    : QWidget(parent)
    , m_network(network)
    , m_pseudo(pseudo)
{
    setWindowTitle("Chat — " + server.name);
    setMinimumSize(580, 460);

    setupUi(server.name);
    setupConnections();

    // Initiate the TCP connection — non-blocking, onConnected() will be called
    // automatically when the connection is established
    m_statusLbl->setText("Connection to " + server.ip + "...");
    m_network->connectToServer(server.ip, pseudo);
}

ChatWindow::~ChatWindow()
{
    // NetworkManager is a Qt child of ServerListWindow,
    // no ChatWindow — we're not destroying it here.
    // We're only disconnecting signals to prevent calls
    // to a destroyed widget.
    m_network->disconnect(this);
}

// ============================================================
// UI
// ============================================================
void ChatWindow::setupUi(const QString& serverName)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(6);

    // --- Header: Server name ---
    auto* headerLbl = new QLabel("Connected to : <b>" + serverName + "</b>", this);
    mainLayout->addWidget(headerLbl);

    // --------------------------------------------------------
    // QTextBrowser: Message display area
    // We use QTextBrowser instead of QTextEdit because:
    // - It is read-only by default
    // - It supports rich HTML (colors, bold, links)
    // - setOpenExternalLinks(true) opens URLs in the browser
    // We inject HTML to colorize the usernames:
    // <span style="color:#ff3232;font-weight:bold;">[Alice]</span> Hello!
    // --------------------------------------------------------
    m_display = new QTextBrowser(this);
    m_display->setOpenExternalLinks(true);
    m_display->setStyleSheet(
        "QTextBrowser {"
        "  background-color: #1e1e1e;"  // fond sombre
        "  color: #dcdcdc;"             // texte clair
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 13px;"
        "  border: 1px solid #444;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "}"
    );

    // Stretch factor 1: the QTextBrowser takes up all available vertical space
    mainLayout->addWidget(m_display, 1);

    // --- Input line ---
    auto* inputLayout = new QHBoxLayout();

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Type your message...");
    m_input->setFixedHeight(34);
    m_input->setEnabled(false); // disabled until TCP connection established
    m_input->setStyleSheet(
        "QLineEdit {"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "  padding: 0 8px;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus { border-color: #2979FF; }"
    );
    inputLayout->addWidget(m_input);

    m_sendBtn = new QPushButton("Envoyer", this);
    m_sendBtn->setFixedSize(90, 34);
    m_sendBtn->setEnabled(false);
    m_sendBtn->setStyleSheet(
        "QPushButton:enabled {"
        "  background: #2979FF;"
        "  color: white;"
        "  font-weight: bold;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:disabled { background: #555; color: #999; border-radius: 4px; }"
        "QPushButton:pressed  { background: #1565C0; }"
    );
    inputLayout->addWidget(m_sendBtn);

    mainLayout->addLayout(inputLayout);

    // --- Statut ---
    m_statusLbl = new QLabel("Connection...", this);
    m_statusLbl->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(m_statusLbl);
}

// ============================================================
// CONNEXIONS SIGNAUX/SLOTS
// ============================================================
void ChatWindow::setupConnections()
{
    // NetworkManager → ChatWindow
    connect(m_network, &NetworkManager::connected,
            this, &ChatWindow::onConnected);

    connect(m_network, &NetworkManager::disconnected,
            this, &ChatWindow::onDisconnected);

    connect(m_network, &NetworkManager::messageReceived,
            this, &ChatWindow::onMessageReceived);

    connect(m_network, &NetworkManager::colorAssigned,
            this, &ChatWindow::onColorAssigned);

    connect(m_network, &NetworkManager::deconnectionReceived,
            this, &ChatWindow::onDeconnectionReceived);

    connect(m_network, &NetworkManager::networkError,
            this, &ChatWindow::onNetworkError);

    // Bouton Envoyer
    connect(m_sendBtn, &QPushButton::clicked,
            this, &ChatWindow::onSendClicked);

    // Press Enter in the input field → send
    // returnPressed() is issued when the user presses Enter
    connect(m_input, &QLineEdit::returnPressed,
            this, &ChatWindow::onSendClicked);
}

// ============================================================
// NETWORK SLOTS
// ============================================================
void ChatWindow::onConnected()
{
    m_connected = true;
    m_input->setEnabled(true);
    m_sendBtn->setEnabled(true);
    m_input->setFocus();
    m_statusLbl->setText("Connected  •  pseudo : " + m_pseudo);
    appendSystemMessage("Connection established. Welcome " + m_pseudo + " !");
}

void ChatWindow::onDisconnected()
{
    m_connected = false;
    m_input->setEnabled(false);
    m_sendBtn->setEnabled(false);
    m_statusLbl->setText("Déconnected");
    appendSystemMessage("Disconnected from the server.");
}

void ChatWindow::onColorAssigned(const QString& pseudo, const QColor& color)
{
    // The server assigned us our color
    // We use it to display our own messages
    m_colormap[pseudo] = color;

    if (pseudo == m_pseudo)
    {
        // Update the status label with a colored indicator
        // QSS supports dynamic colors via setStyleSheet()
        m_statusLbl->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;")
            .arg(color.name())  // .name() retourne "#rrggbb"
        );
    }
    m_statusLbl->setText("Connecté  •  " + m_pseudo);
}

void ChatWindow::onDeconnectionReceived(const QString& pseudo)
{
    m_colormap.erase(pseudo);
}

void ChatWindow::onMessageReceived(const QString& pseudo, const QString& text)
{
    // Choose the color:
    // - Server messages: gray
    // - Client messages: color assigned by the server
    QColor color;
    if (pseudo == "Server")
        color = QColor(130, 130, 130); // Server: Grey
    else
        color = m_colormap[pseudo]; //  clients : Mapped color

    appendMessage(pseudo, text, color);
}

void ChatWindow::onNetworkError(const QString& msg)
{
    m_statusLbl->setText("Error : " + msg);
    appendSystemMessage("Network Error : " + msg);
}

// ============================================================
// SEND A MESSAGE
// ============================================================
void ChatWindow::onSendClicked()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_connected) return;

    // Send to the server
    m_network->sendMessage(text);

    // Display our own message locally (the server does not send it back to us)
    appendMessage(m_pseudo, text, m_colormap[m_pseudo]);

    // Clear the input field
    m_input->clear();
}

// ============================================================
// DISPLAYING MESSAGES
//
// We use HTML to colorize usernames.
// QTextBrowser::append() adds an HTML block after the username.
//
// QString::toHtmlEscaped() is ESSENTIAL for user test,
// it converts <, >, & into HTML entities to
// prevent the message text from breaking the HTML rendering.
// Ex : "2 < 3" → "2 &lt; 3"
// ============================================================
void ChatWindow::appendMessage(const QString& pseudo,
                               const QString& text,
                               const QColor&  color)
{
    // Current time for each message
    QString time = QDateTime::currentDateTime().toString("hh:mm");

    // Build the HTML line :
    // [hh:mm] [Username] message text
    QString html = QString(
        "<span style='color:#555555;font-size:11px;'>[%1]</span> "
        "<span style='color:%2;font-weight:bold;'>[%3]</span> "
        "<span style='color:#dcdcdc;'>%4</span>"
    )
    .arg(time)
    .arg(color.name())                    // hex color of the username
    .arg(pseudo.toHtmlEscaped())          // HTML secure pseudonym
    .arg(text.toHtmlEscaped());           // HTML secure text

    // append() adds the HTML and automatically scrolls down
    m_display->append(html);

    // Force scroll to the last message
    // (append() normally does this, but we're making sure)
    QScrollBar* sb = m_display->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ChatWindow::appendSystemMessage(const QString& text)
{
    QString html = QString(
        "<span style='color:#888888;font-style:italic;'>— %1 —</span>"
    ).arg(text.toHtmlEscaped());

    m_display->append(html);
}

// ============================================================
// CLOSING THE WINDOW
// closeEvent() is called when the user clicks the X.
// We properly disconnect TCP before letting Qt close.
// ============================================================
void ChatWindow::closeEvent(QCloseEvent* event)
{
    if (m_connected)
        m_network->disconnectFromServer();

    // Accepting the event = letting Qt close the window normally
    event->accept();
}
