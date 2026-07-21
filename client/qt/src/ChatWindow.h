// ChatWindow.h
// Display messages + Input + TCP connection

#pragma once

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QColor>
#include "NetworkManager.h"

class ChatWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWindow(const ServerInfo& server,
                        const QString&    pseudo,
                        NetworkManager*   network,
                        QWidget*          parent = nullptr);
    ~ChatWindow();

protected:
    // Intercepts the window closing (X button)
    // to properly disconnect the TCP socket
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QString& pseudo, const QString& text);
    void onColorAssigned(const QString& pseudo, const QColor& color);
    void onDeconnectionReceived(const QString& pseudo);
    void onNetworkError(const QString& msg);
    void onSendClicked();

private:
    void setupUi(const QString& serverName);
    void setupConnections();

    // Adds a message to the QTextBrowser with HTML formatting
    void appendMessage(const QString& pseudo,
                       const QString& text,
                       const QColor&  color);

    // Adds a system message (login, logout, etc.)
    void appendSystemMessage(const QString& text);

    NetworkManager* m_network   = nullptr;
    QTextBrowser*   m_display   = nullptr; // messages history
    QLineEdit*      m_input     = nullptr; // input area
    QPushButton*    m_sendBtn   = nullptr;
    QLabel*         m_statusLbl = nullptr;

    // I don't manage pseudo with more than one word
    QString  m_pseudo;    // our pseudo
    std::map<QString, QColor> m_colormap;
    bool     m_connected = false;
};
