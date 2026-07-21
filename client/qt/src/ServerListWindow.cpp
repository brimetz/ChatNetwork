// ServerListWindow.cpp

#include "ServerListWindow.h"
#include "ChatWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidgetItem>

#include "common_shared.h"

// ============================================================
// CONSTRUCTOR
// ============================================================
ServerListWindow::ServerListWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Chat — Server Search");
    setMinimumSize(500, 380);

    m_network = new NetworkManager(this);

    setupUi();
    setupConnections();

    m_network->startDiscovery();
}

ServerListWindow::~ServerListWindow()
{
    m_network->stopDiscovery();
}

// ============================================================
// UI CONSTRUCTION
// ============================================================
void ServerListWindow::setupUi()
{
    // --------------------------------------------------------
    // LAYOUTS
    // In Qt, widgets aren't positioned using pixels (x, y).
    // We use layouts that handle positioning and
    // resizing automatically.
    // QVBoxLayout: stacks widgets vertically
    // QHBoxLayout: aligns widgets horizontally
    // --------------------------------------------------------

    // Main vertical layout
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // --- Title ---
    auto* titleLbl = new QLabel("Servers available on the network :", this);
    QFont titleFont = titleLbl->font();
    titleFont.setBold(true);
    titleLbl->setFont(titleFont);
    mainLayout->addWidget(titleLbl);

    // --- QListWidget : displays the servers ---
    // QListWidget = simple list with QListWidgetItem
    // Each item has a displayed text + hidden data (UserRole).
    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);  // alternating light/dark lines
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSortingEnabled(false);

    // Inline CSS style — Qt supports a subset of CSS called QSS
    m_list->setStyleSheet(
        "QListWidget { font-size: 13px; }"
        "QListWidget::item { padding: 6px; }"
        "QListWidget::item:selected { background: #2979FF; color: white; }"
    );
    mainLayout->addWidget(m_list);

    // --- Button row ---
    auto* btnLayout = new QHBoxLayout();

    m_refreshBtn = new QPushButton("↺  Refresh", this);
    m_refreshBtn->setFixedHeight(32);

    // addStretch() = flexible space that pushes the following widgets to the right
    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addStretch();

    m_joinBtn = new QPushButton("Join  →", this);
    m_joinBtn->setFixedHeight(32);
    m_joinBtn->setEnabled(false); // disabled until selected
    m_joinBtn->setStyleSheet(
        "QPushButton:enabled  { background: #2979FF; color: white; font-weight: bold; }"
        "QPushButton:disabled { background: #cccccc; color: #888888; }"
    );
    btnLayout->addWidget(m_joinBtn);

    mainLayout->addLayout(btnLayout);

    // --- Status label ---
    m_statusLbl = new QLabel("Server search...", this);
    m_statusLbl->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(m_statusLbl);
}

// ============================================================
// SIGNAL CONNECTIONS/SLOTS
// ============================================================
void ServerListWindow::setupConnections()
{
    // NetworkManager → this
    connect(m_network, &NetworkManager::serverDiscovered,
            this, &ServerListWindow::onServerDiscovered);

    connect(m_network, &NetworkManager::serverUpdated,
            this, &ServerListWindow::onServerUpdated);

    connect(m_network, &NetworkManager::serverLost,
            this, &ServerListWindow::onServerLost);

    connect(m_network, &NetworkManager::networkError,
            this, [this](const QString& msg) {
        m_statusLbl->setText("Erreur : " + msg);
    });

    // Button → this
    connect(m_joinBtn,    &QPushButton::clicked,
            this, &ServerListWindow::onJoinClicked);

    connect(m_refreshBtn, &QPushButton::clicked,
            this, [this]() {
        // Clear the list and restart the discovery
        m_list->clear();
        m_network->refreshServerlist();
        m_network->stopDiscovery();
        m_network->startDiscovery();
        m_statusLbl->setText("Update...");
        m_joinBtn->setEnabled(false);
    });

    // Select from the list → toggle the button on/off
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &ServerListWindow::onSelectionChanged);

    // Double-click → join directly
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onJoinClicked(); });
}

// ============================================================
// SLOTS : list update
// ============================================================

// Formats the text of an item: "ServerName — 192.168.x.x (3 clients)"
static QString formatItem(const ServerInfo& s)
{
    return QString("%1     %2     %3 client(s)")
           .arg(s.name, -24)   // -24 = left-aligned, 24 characters
           .arg(s.ip,   -16)
           .arg(s.clientCount);
}

void ServerListWindow::onServerDiscovered(const ServerInfo& server)
{
    // Create a new item in the list
    auto* item = new QListWidgetItem(formatItem(server), m_list);

    // Store the IP address in Qt::UserRole — hidden data associated with the item
    // Allows retrieval of the ServerInfo corresponding to the click
    item->setData(Qt::UserRole, server.ip);

    // Colored icon: a small square in the server's color
    // (We use the index in the list as the color)
    QPixmap px(14, 14);
    px.fill(QColor(41, 121, 255)); // bleu Qt
    item->setIcon(QIcon(px));

    // Update status
    m_statusLbl->setText(QString("%1 Server(s) found").arg(m_network->getServersKnown().size()));
}

void ServerListWindow::onServerUpdated(const ServerInfo& server)
{
    // Find the item corresponding to this IP address
    for (int i = 0; i < m_list->count(); ++i)
    {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(Qt::UserRole).toString() == server.ip)
        {
            item->setText(formatItem(server));
            break;
        }
    }
}

void ServerListWindow::onServerLost(const QString& ip)
{
    // Remove the item from the list
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toString() == ip)
        {
            // takeItem() removes the item from the widget and returns a pointer
            // → it must be deleted manually (Qt doesn't do this here)
            delete m_list->takeItem(i);
            break;
        }
    }

    m_statusLbl->setText(QString("%1 Server(s) found").arg(m_network->getServersKnown().size()));
    onSelectionChanged(); // recalculate the button state
}

// ============================================================
// SELECTION
// ============================================================
void ServerListWindow::onSelectionChanged()
{
    m_joinBtn->setEnabled(!m_list->selectedItems().isEmpty());
}

ServerInfo* ServerListWindow::selectedServer()
{
    auto items = m_list->selectedItems();
    if (items.isEmpty()) return nullptr;

    QString ip = items.first()->data(Qt::UserRole).toString();
    auto it = m_network->getServersKnown().find(ip);
    return (it != m_network->getServersKnown().end()) ? &it.value() : nullptr;
}

// ============================================================
// JOIN A SERVER
// ============================================================
void ServerListWindow::onJoinClicked()
{
    ServerInfo* server = selectedServer();
    if (!server)
        return;

    // --------------------------------------------------------
    // QInputDialog::getText — Qt built-in input dialog
    // Much simpler than a Win32 dialog template!
    // Parameters: parent, title, label, echo mode, initial value, OK
    // --------------------------------------------------------
    bool ok = false;
    QString pseudo = QInputDialog::getText(
        this,
        "Choose a nickname",
        "Enter your username :",
        QLineEdit::Normal,
        "",
        &ok
    );

    if (!ok || pseudo.trimmed().isEmpty()) return;

    // Limit the length
    pseudo = pseudo.trimmed().left(MAX_PSEUDO);

    // --------------------------------------------------------
    // Open the chat window
    // Pass the NetworkManager AND the server information
    // ChatWindow will handle the TCP connection
    // --------------------------------------------------------
    auto* chatWin = new ChatWindow(*server, pseudo, m_network);

    // WA_DeleteOnClose : Qt will destroy the window when it is closed
    chatWin->setAttribute(Qt::WA_DeleteOnClose);
    chatWin->show();

    // Hide the main window during the chat
    hide();

    // When ChatWindow is closed → redisplay the list
    connect(chatWin, &QWidget::destroyed,
            this, [this]() { show(); });
}
