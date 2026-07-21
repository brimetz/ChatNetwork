#include "../../common/common_shared.h"
#include "../../common/common_win32.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <commctrl.h>  // ListView, Win32 controls
#include <map>
#include <richedit.h>   // RichEdit : zone de texte avec couleurs par ligne

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

// ============================================================
// IDENTIFIANTS DES CONTRÔLES
// ============================================================
// Fenêtre de liste de serveurs
#define IDC_SERVER_LIST   101
#define IDC_BTN_JOIN      102
#define IDC_BTN_REFRESH   103
#define IDC_STATUS_LABEL  104

// Fenêtre de chat
#define IDC_CHAT_DISPLAY  201  // RichEdit : affichage des messages
#define IDC_CHAT_INPUT    202  // EditBox  : saisie du message
#define IDC_BTN_SEND      203  // Bouton   : envoyer
#define IDC_CHAT_STATUS   204  // Label    : statut connexion

// Messages custom (WM_USER = base des messages utilisateur)
#define WM_ADD_SERVER (WM_USER + 1) // Ajoute un serveur de la liste affiché
#define WM_REMOVE_SERVER (WM_USER + 2) // Supprime un serveur de la liste affiché
#define WM_UPDATE_SERVER (WM_USER + 3) // Update les infos d'un serveur de la liste affiché
#define WM_REFRESH_SERVERS (WM_USER + 4) // reset la liste affiché
#define WM_CHAT_MESSAGE   (WM_USER + 5) // nouveau message reçu
#define WM_CHAT_CONNECTED (WM_USER + 6) // connexion TCP établie
#define WM_CHAT_DISCONNECTED (WM_USER + 7) // déconnexion

// ============================================================
// STRUCTURES
// ============================================================
struct ServerInfo {
    std::string ip;
    std::string name;
    int         client_count;
    DWORD       last_seen;
};

// Un message de chat à afficher dans le RichEdit
struct ChatMessage {
    std::string pseudo;   // expéditeur ("Serveur" pour les messages système)
    std::string text;     // contenu
    COLORREF    color;    // couleur du pseudo
};

// ============================================================
// ÉTAT GLOBAL
// ============================================================
std::vector<ServerInfo> servers;
std::mutex              servers_mutex;

std::vector<ChatMessage> pending_messages; // messages en attente d'affichage
std::mutex               messages_mutex;

std::atomic<bool> udp_running(true);
std::atomic<bool> tcp_running(false);

SOCKET g_tcp_socket = INVALID_SOCKET;

// Couleur assignée à notre propre pseudo (reçue du serveur)
COLORREF g_my_color = RGB(255, 255, 255);

// Handles des fenêtres
HWND g_hwnd_main = NULL;
HWND g_hwnd_list = NULL;
HWND g_hwnd_status = NULL;
HWND g_hwnd_join = NULL;
HWND g_hwnd_chat = NULL;  // fenêtre de chat
HWND g_hwnd_display = NULL;  // RichEdit
HWND g_hwnd_input = NULL;  // zone de saisie
HWND g_hwnd_send = NULL;
HWND g_hwnd_chat_status = NULL;

HINSTANCE g_hinstance = NULL;

// ============================================================
// UTILITAIRES RÉSEAU
// ============================================================
bool send_line(SOCKET fd, const std::string& msg) {
    std::string line = msg + "\n";
    return send(fd, line.c_str(), static_cast<int>(line.size()), 0) != SOCKET_ERROR;
}

bool recv_line(SOCKET fd, std::string& out) {
    out.clear();
    char c;
    while (true) {
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') break;
        if (c != '\r') out += c;
    }
    return true;
}

// ============================================================
// AJOUTER UN MESSAGE DANS LE RICHEDIT AVEC COULEUR
// Le RichEdit permet de coloriser du texte ligne par ligne
// via CHARFORMAT2 — une structure qui décrit le formatage.
//
// Principe :
//   1. Positionner le curseur à la fin du texte
//   2. Définir le format (couleur, gras)
//   3. Insérer le texte
//   4. Répéter pour chaque partie (pseudo / message)
//   5. Scroller vers le bas
// ============================================================
void append_chat_message(const ChatMessage& msg) {
    if (!g_hwnd_display) return;

    // Aller à la fin du texte existant
    // EM_SETSEL(-1, -1) = sélectionner la fin
    SendMessage(g_hwnd_display, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    // --- Écrire le pseudo en couleur ---
    CHARFORMAT2A cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_CHARSET;
    cf.dwEffects = CFE_BOLD;          // gras
    cf.crTextColor = msg.color;         // couleur du pseudo
    cf.bCharSet = ANSI_CHARSET;
    // CFM_EFFECTS doit exclure CFE_AUTOCOLOR pour que crTextColor soit pris en compte
    cf.dwMask |= CFM_EFFECTS;
    cf.dwEffects &= ~CFE_AUTOCOLOR;

    SendMessage(g_hwnd_display, EM_SETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&cf));

    std::string pseudo_part = "[" + msg.pseudo + "] ";
    SendMessageA(g_hwnd_display, EM_REPLACESEL, FALSE,
        reinterpret_cast<LPARAM>(pseudo_part.c_str()));

    // --- Écrire le message en blanc ---
    cf.dwEffects = 0;               // pas de gras
    cf.crTextColor = RGB(220, 220, 220); // texte clair
    SendMessage(g_hwnd_display, EM_SETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&cf));

    std::string text_part = msg.text + "\r\n";
    SendMessageA(g_hwnd_display, EM_REPLACESEL, FALSE,
        reinterpret_cast<LPARAM>(text_part.c_str()));

    // Scroller automatiquement vers le dernier message
    SendMessage(g_hwnd_display, WM_VSCROLL, SB_BOTTOM, 0);
}

// ============================================================
// PARSER UN MESSAGE REÇU DU SERVEUR
// Format : "TAG param1 param2 ...\n"
// Tags :
//   PSEUDO_REQUEST          → afficher dialog de saisie pseudo
//   COLOR R G B             → notre couleur assignée par le serveur
//   MSG pseudo texte...     → message de chat
// ============================================================
void parse_server_message(const std::string& line) {
    if (line.empty()) return;

    std::istringstream ss(line);
    std::string tag;
    ss >> tag;

    if (tag == "COLOR") {
        // Recevoir notre couleur assignée
        int r, g, b;
        ss >> r >> g >> b;
        g_my_color = RGB(r, g, b);
        return;
    }

    if (tag == "MSG") {
        std::string pseudo;
        ss >> pseudo;

        // Le reste de la ligne = texte du message
        std::string text;
        std::getline(ss, text);
        if (!text.empty() && text[0] == ' ') text = text.substr(1);

        // Déterminer la couleur du pseudo
        COLORREF color;
        if (pseudo == "Server") {
            color = RGB(150, 150, 150); // gris pour les messages système
        }
        else {
            // Pour les autres, on utilise g_my_color si c'est nous,
            // sinon une couleur par défaut (le serveur gère les couleurs)
            // À l'étape suivante, on pourrait demander la couleur de chaque pseudo
            color = RGB(100, 180, 255);
        }

        // Poster le message dans la file thread-safe
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            pending_messages.push_back({ pseudo, text, color });
        }
        if (g_hwnd_chat)
            PostMessage(g_hwnd_chat, WM_CHAT_MESSAGE, 0, 0);
    }
}

// ============================================================
// THREAD TCP : réception des messages du serveur
// Tourne tant que la connexion est active.
// ============================================================
void tcp_receive_thread(SOCKET sock) {
    std::string line;
    while (tcp_running.load()) {
        if (!recv_line(sock, line)) break;
        parse_server_message(line);
    }
    tcp_running.store(false);
    if (g_hwnd_chat)
        PostMessage(g_hwnd_chat, WM_CHAT_DISCONNECTED, 0, 0);
}

// ============================================================
// THREAD UDP Clean up Server
// delete each server that has not send a beacon since 5 seconds
// ============================================================
void cleanup_thread()
{
    bool bServerRemoved = false;
    while (udp_running.load())
    {
        Sleep(2000);
        DWORD now = GetTickCount64();
        {
            bServerRemoved = false;
            std::lock_guard<std::mutex> lock(servers_mutex);
            servers.erase(std::remove_if(servers.begin(), servers.end(),
                [&, now](const ServerInfo& s)
                {
                    if ((now - s.last_seen) > 5000)
                    {
                        bServerRemoved = true;
                        return true;
                    }
                    return (now - s.last_seen) > 5000;
                }), servers.end());
        }
        if (g_hwnd_main && bServerRemoved)
            PostMessage(g_hwnd_main, WM_REMOVE_SERVER, 0, 0);
    }
}

// ============================================================
// UDP thread listener
// 
// - extract Ip source 
// - parse the UdpBeacon struct
// - update the server list
// - update the window
// ============================================================
void udp_listener_thread()
{
    // create the socket UDP
    SOCKET udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock == INVALID_SOCKET)
        return;

    // SO_REUSEADDR:: applications can listen on the same port, 
    // usefull with many clients on the same computer
    int reuse = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Bind on INADDR_ANY:UDP_PORT to receive the beacon
    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(UDP_PORT);
    listen_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_sock, reinterpret_cast<sockaddr*>(&listen_addr),
        sizeof(listen_addr)) == SOCKET_ERROR)
    {
        closesocket(udp_sock);
        return;
    }

    while (udp_running.load())
    {
        UdpBeacon beacon{};
        sockaddr_in sender_addr{};
        int sender_len = sizeof(sender_addr);

        // recvfrom() = recv() for UDP
        // return also the source address in sender_addr
        int n = recvfrom(udp_sock,
            reinterpret_cast<char*>(&beacon),
            sizeof(beacon),
            0,
            reinterpret_cast<sockaddr*>(&sender_addr),
            &sender_len);

        if (n != sizeof(UdpBeacon))
            continue; // invalid data or 

        // extract server IP
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr.sin_addr, ip_str, sizeof(ip_str));
        std::string ip(ip_str);

        // Securise the server name (be sure that it is terminated by \0)
        beacon.server_name[MAX_SERVER_NAME - 1] = '\0';

        bool bServerAdded = false;
        bool bServerUpdated = false;
        // Update the server list
        {
            std::lock_guard<std::mutex> lock(servers_mutex);
            bool found = false;
            for (auto& s : servers)
            {
                if (s.ip == ip)
                {
                    // already known server : update its data
                    if (s.name != beacon.server_name ||
                        s.client_count != beacon.client_count)
                    {
                        bServerUpdated = true;
                    }
                    s.name = beacon.server_name;
                    s.client_count = beacon.client_count;
                    s.last_seen = GetTickCount64();
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                bServerAdded = true;
                // New server
                servers.push_back({ ip, beacon.server_name, beacon.client_count, GetTickCount() });
            }
        }

        // Notify main window to refresh the list view
        if (g_hwnd_main)
        {
            if (bServerAdded)
            {
                PostMessage(g_hwnd_main, WM_ADD_SERVER, 0, 0);
            }
            else if (bServerUpdated)
            {
                PostMessage(g_hwnd_main, WM_UPDATE_SERVER, 0, 0);
            }
            
        }
    }

    closesocket(udp_sock);
}

// ============================================================
// SE CONNECTER AU SERVEUR SÉLECTIONNÉ
// Appelé depuis le thread UI après confirmation.
// La connexion TCP et l'échange de pseudo se font ici.
// ============================================================
bool connect_to_server(const std::string& ip, const std::string& pseudo) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    // Le serveur envoie d'abord "PSEUDO_REQUEST\n"
    std::string req;
    if (!recv_line(sock, req) || req != "PSEUDO_REQUEST") {
        closesocket(sock);
        return false;
    }

    // Envoyer notre pseudo
    send_line(sock, pseudo);

    // Recevoir notre couleur "COLOR R G B"
    std::string color_line;
    recv_line(sock, color_line);
    parse_server_message(color_line);

    g_tcp_socket = sock;
    tcp_running.store(true);

    // Lancer le thread de réception
    std::thread(tcp_receive_thread, sock).detach();
    return true;
}

// ============================================================
// WINDOW PROCEDURE — FENÊTRE DE CHAT
// ============================================================
LRESULT CALLBACK ChatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        g_hwnd_chat = hwnd;

        // Charger la DLL RichEdit (nécessaire avant CreateWindow avec "RichEdit20A")
        // MSFTEDIT.DLL = RichEdit v4.1 (recommandé, Windows XP+)
        LoadLibraryA("Msftedit.dll");

        // --- Zone d'affichage des messages (RichEdit) ---
        // ES_MULTILINE  : plusieurs lignes
        // ES_READONLY   : lecture seule (l'utilisateur ne peut pas modifier)
        // WS_VSCROLL    : barre de défilement verticale
        // ES_AUTOVSCROLL: défile automatiquement vers le bas
        g_hwnd_display = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "RICHEDIT50W",  // classe RichEdit 4.1
            "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
            WS_VSCROLL | ES_AUTOVSCROLL,
            10, 10, 560, 350,
            hwnd, (HMENU)IDC_CHAT_DISPLAY,
            g_hinstance, NULL);

        // Fond sombre pour le RichEdit
        SendMessage(g_hwnd_display, EM_SETBKGNDCOLOR, 0,
            (LPARAM)RGB(30, 30, 30));

        // Limite de texte : 2 Mo (par défaut c'est 32 Ko)
        SendMessage(g_hwnd_display, EM_LIMITTEXT, 2 * 1024 * 1024, 0);

        // --- Zone de saisie ---
        // ES_AUTOHSCROLL : défile horizontalement si le texte dépasse
        g_hwnd_input = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 370, 460, 28,
            hwnd, (HMENU)IDC_CHAT_INPUT,
            g_hinstance, NULL);

        // --- Bouton Envoyer ---
        g_hwnd_send = CreateWindowA("BUTTON", "Envoyer",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            // BS_DEFPUSHBUTTON : bouton par défaut (activé par Entrée)
            480, 370, 90, 28,
            hwnd, (HMENU)IDC_BTN_SEND,
            g_hinstance, NULL);

        // --- Label de statut ---
        g_hwnd_chat_status = CreateWindowA("STATIC", "Connecté",
            WS_CHILD | WS_VISIBLE,
            10, 405, 400, 18,
            hwnd, (HMENU)IDC_CHAT_STATUS,
            g_hinstance, NULL);

        // Donner le focus à la zone de saisie
        SetFocus(g_hwnd_input);

        // Message de bienvenue local
        append_chat_message({ "Système", "Connecté au serveur. Bienvenue !",
                               RGB(80, 180, 80) });
        return 0;
    }

                  // Nouveau message reçu depuis le thread TCP
    case WM_CHAT_MESSAGE: {
        std::vector<ChatMessage> to_display;
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            to_display.swap(pending_messages); // vider la liste en une fois
        }
        for (const auto& m : to_display)
            append_chat_message(m);
        return 0;
    }

                        // Déconnexion du serveur
    case WM_CHAT_DISCONNECTED:
        SetWindowTextA(g_hwnd_chat_status, "Déconnecté du serveur");
        EnableWindow(g_hwnd_input, FALSE);
        EnableWindow(g_hwnd_send, FALSE);
        append_chat_message({ "Système", "Déconnecté du serveur.",
                               RGB(220, 80, 80) });
        return 0;

    case WM_COMMAND: {
        int ctrl_id = LOWORD(wParam);

        // Envoyer le message (bouton OU touche Entrée via BS_DEFPUSHBUTTON)
        if (ctrl_id == IDC_BTN_SEND) {
            char buf[512] = {};
            GetWindowTextA(g_hwnd_input, buf, sizeof(buf));
            std::string text(buf);

            if (text.empty() || g_tcp_socket == INVALID_SOCKET) return 0;

            // Envoyer au serveur
            send_line(g_tcp_socket, text);

            // Afficher notre propre message localement (avec notre couleur)
            append_chat_message({ "Moi", text, g_my_color });

            // Vider la zone de saisie
            SetWindowTextA(g_hwnd_input, "");
            SetFocus(g_hwnd_input);
            return 0;
        }

        // Entrée dans la zone de saisie → simuler clic Envoyer
        if (ctrl_id == IDC_CHAT_INPUT && HIWORD(wParam) == EN_UPDATE) {
            // Gérer via BS_DEFPUSHBUTTON (déjà fait)
        }
        return 0;
    }

                   // Touche Entrée dans la zone de saisie
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
            PostMessage(hwnd, WM_COMMAND, IDC_BTN_SEND, 0);
        return 0;

    case WM_CLOSE:
        // Fermer proprement la connexion TCP
        tcp_running.store(false);
        if (g_tcp_socket != INVALID_SOCKET) {
            shutdown(g_tcp_socket, SD_BOTH);
            closesocket(g_tcp_socket);
            g_tcp_socket = INVALID_SOCKET;
        }
        g_hwnd_chat = NULL;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_hwnd_chat = NULL;
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================
// OUVRIR LA FENÊTRE DE CHAT
// Crée et affiche la fenêtre de chat, puis connecte le socket TCP.
// ============================================================
void open_chat_window(const ServerInfo& server, const std::string& pseudo) {
    // Enregistrer la classe de la fenêtre de chat (une seule fois)
    WNDCLASSA wc{};
    wc.lpfnWndProc = ChatWndProc;
    wc.hInstance = g_hinstance;
    wc.lpszClassName = "ChatWindowClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH); // fond gris foncé
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc); // si déjà enregistrée, l'appel est ignoré

    std::string title = "Chat - " + server.name + " (" + server.ip + ")";

    HWND hwnd = CreateWindowA(
        "ChatWindowClass", title.c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        100, 100, 590, 470,
        NULL, NULL, g_hinstance, NULL
    );
    if (!hwnd) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Se connecter au serveur TCP dans un thread pour ne pas bloquer l'UI
    std::thread([hwnd, server, pseudo]() {
        bool ok = connect_to_server(server.ip, pseudo);
        if (!ok) {
            MessageBoxA(hwnd, "Impossible de se connecter au serveur.",
                "Erreur", MB_OK | MB_ICONERROR);
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        }).detach();
}

// ============================================================
// BOÎTE DE DIALOGUE : SAISIE DU PSEUDO
// Utilise DialogBoxParam + une procédure de dialogue.
// ============================================================
// Structure pour passer le pseudo en retour
struct PseudoDialogData {
    char pseudo[MAX_PSEUDO + 1];
};

INT_PTR CALLBACK PseudoDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg) {
    case WM_INITDIALOG: {
        // lParam contient le pointeur vers PseudoDialogData
        SetWindowLongPtr(hdlg, DWLP_USER, lParam);
        // Centrer la fenêtre
        RECT rc; GetWindowRect(hdlg, &rc);
        SetWindowPos(hdlg, NULL,
            (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2,
            0, 0, SWP_NOZORDER | SWP_NOSIZE);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) 
        {
            auto* data = reinterpret_cast<PseudoDialogData*>(
                GetWindowLongPtr(hdlg, DWLP_USER));
            GetDlgItemTextA(hdlg, 101, data->pseudo, MAX_PSEUDO);
            if (strlen(data->pseudo) == 0) return TRUE; // pseudo vide, ignorer
            EndDialog(hdlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hdlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Crée une boîte de dialogue simple pour saisir le pseudo
// (sans fichier .rc de ressource — on la construit en mémoire)
std::string ask_pseudo(HWND parent) {
    // Construire le template de dialogue en mémoire
    // Format : DLGTEMPLATE + DLGITEMTEMPLATE pour chaque contrôle
    struct {
        DLGTEMPLATE tmpl;
        WORD        menu, cls, title[8];
        WORD        pt;
        DLGITEMTEMPLATE label_item;
        WORD label_cls[2], label_text[20], label_extra;
        DLGITEMTEMPLATE edit_item;
        WORD edit_cls[2], edit_text[2], edit_extra;
        DLGITEMTEMPLATE btn_item;
        WORD btn_cls[2], btn_text[5], btn_extra;
    } dlg_tpl = {};

    // DLGTEMPLATE : propriétés de la boîte de dialogue
    dlg_tpl.tmpl.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dlg_tpl.tmpl.cdit = 3;      // 3 contrôles (label + edit + bouton)
    dlg_tpl.tmpl.cx = 180;    // largeur (unités dialogue)
    dlg_tpl.tmpl.cy = 60;     // hauteur

    // Titre de la fenêtre (unicode dans le template)
    dlg_tpl.title[0] = L'C'; dlg_tpl.title[1] = L'h';
    dlg_tpl.title[2] = L'o'; dlg_tpl.title[3] = L'i';
    dlg_tpl.title[4] = L'x'; dlg_tpl.title[5] = L' ';
    dlg_tpl.title[6] = L'd'; dlg_tpl.title[7] = 0;

    // Pour simplifier, on utilise MessageBox + InputBox custom via une autre approche :
    // Fenêtre modale simple créée manuellement
    (void)dlg_tpl; // non utilisé finalement

    // Approche plus simple : fenêtre popup temporaire
    char pseudo[MAX_PSEUDO + 1] = {};

    // Créer une fenêtre de saisie simple
    HWND dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "STATIC", "",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 300, 120,
        parent, NULL, g_hinstance, NULL);

    SetWindowTextA(dlg, "Choisir un pseudo");

    // Label
    CreateWindowA("STATIC", "Entrez votre pseudo :",
        WS_CHILD | WS_VISIBLE,
        10, 15, 170, 20, dlg, NULL, g_hinstance, NULL);

    // Zone de saisie
    HWND edit = CreateWindowExA(WS_EX_CLIENTEDGE,
        "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 38, 170, 24, dlg, (HMENU)101, g_hinstance, NULL);
    SendMessageA(edit, EM_SETLIMITTEXT, MAX_PSEUDO, 0);

    // Bouton OK
    CreateWindowA("BUTTON", "OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        200, 38, 80, 24, dlg, (HMENU)IDOK, g_hinstance, NULL);

    // Centrer
    RECT rc; GetWindowRect(dlg, &rc);
    SetWindowPos(dlg, HWND_TOP,
        (GetSystemMetrics(SM_CXSCREEN) - 300) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - 120) / 2,
        0, 0, SWP_NOSIZE);

    ShowWindow(dlg, SW_SHOW);
    SetFocus(edit);

    // Boucle de messages locale (modale)
    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) 
    {
           
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
            GetWindowTextA(edit, pseudo, MAX_PSEUDO);
            break;
        }

        if (LOWORD(m.wParam) == IDOK)
        {
            int i = 0;
        }

        if (m.message == WM_COMMAND && LOWORD(m.wParam) == IDOK) {
            GetWindowTextA(edit, pseudo, MAX_PSEUDO);
            break;
        }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    DestroyWindow(dlg);
    return std::string(pseudo);
}

// ============================================================
// WINDOW PROCEDURE — FENÊTRE PRINCIPALE (liste des serveurs)
// ============================================================
LRESULT CALLBACK WndServerListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        g_hwnd_main = hwnd;

        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icc);

        CreateWindowA("STATIC", "Serveurs disponibles sur le reseau :",
            WS_CHILD | WS_VISIBLE,
            10, 10, 400, 20, hwnd, NULL, g_hinstance, NULL);

        g_hwnd_list = CreateWindowExA(
            WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            10, 35, 560, 280,
            hwnd, (HMENU)IDC_SERVER_LIST, g_hinstance, NULL);

        ListView_SetExtendedListViewStyle(g_hwnd_list,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (char*)"Nom du serveur"; col.cx = 220;
        ListView_InsertColumn(g_hwnd_list, 0, &col);
        col.pszText = (char*)"Adresse IP";     col.cx = 140;
        ListView_InsertColumn(g_hwnd_list, 1, &col);
        col.pszText = (char*)"Clients";        col.cx = 100;
        ListView_InsertColumn(g_hwnd_list, 2, &col);

        CreateWindowA("BUTTON", "Actualiser",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 325, 120, 30,
            hwnd, (HMENU)IDC_BTN_REFRESH, g_hinstance, NULL);

        g_hwnd_join = CreateWindowA("BUTTON", "Rejoindre ->",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            440, 325, 130, 30,
            hwnd, (HMENU)IDC_BTN_JOIN, g_hinstance, NULL);

        g_hwnd_status = CreateWindowA("STATIC", "Recherche de serveurs...",
            WS_CHILD | WS_VISIBLE,
            10, 365, 400, 20,
            hwnd, (HMENU)IDC_STATUS_LABEL, g_hinstance, NULL);

        // Lancer les threads UDP
        std::thread(udp_listener_thread).detach();

        // Thread de nettoyage des serveurs disparus
        std::thread(cleanup_thread).detach();

        return 0;
    }

    case WM_ADD_SERVER:
    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        ServerInfo& lastserver = servers[servers.size() - 1];
        LVITEMA item{};
        item.mask = LVIF_TEXT; item.iItem = servers.size() - 1; item.iSubItem = 0;
        std::string name = lastserver.name;
        item.pszText = const_cast<char*>(name.c_str());
        ListView_InsertItem(g_hwnd_list, &item);
        ListView_SetItemText(g_hwnd_list, servers.size() - 1, 1,
            const_cast<char*>(lastserver.ip.c_str()));

        std::string cnt = std::to_string(lastserver.client_count) + " client(s)";
        ListView_SetItemText(g_hwnd_list, servers.size() - 1, 2,
            const_cast<char*>(cnt.c_str()));

        std::string st = std::to_string(servers.size()) + " serveur(s) trouvé(s)";
        SetWindowTextA(g_hwnd_status, st.c_str());
        return 0;
    }
    case WM_REMOVE_SERVER:
    {
        ListView_DeleteAllItems(g_hwnd_list);
        std::lock_guard<std::mutex> lock(servers_mutex);
        for (int i = 0; i < (int)servers.size(); i++)
        {
            const auto& s = servers[i];
            LVITEMA item{};
            item.mask = LVIF_TEXT; item.iItem = i; item.iSubItem = 0;
            std::string name = s.name;
            item.pszText = const_cast<char*>(name.c_str());
            ListView_InsertItem(g_hwnd_list, &item);
            ListView_SetItemText(g_hwnd_list, i, 1,
                const_cast<char*>(s.ip.c_str()));

            std::string cnt = std::to_string(s.client_count) + " client(s)";
            ListView_SetItemText(g_hwnd_list, i, 2,
                const_cast<char*>(cnt.c_str()));
        }
        std::string st = std::to_string(servers.size()) + " serveur(s) trouvé(s)";
        SetWindowTextA(g_hwnd_status, st.c_str());
        return 0;
    }
    case WM_UPDATE_SERVER:
    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        for (int i = 0; i < (int)servers.size(); i++)
        {
            const auto& s = servers[i];
            LVITEMA item{};
            item.mask = LVIF_TEXT; item.iItem = i; item.iSubItem = 0;
            std::string name = s.name;
            item.pszText = const_cast<char*>(name.c_str());

            //ListView_SetText
           // ListView_InsertItem(g_hwnd_list, &item);
            ListView_SetItemText(g_hwnd_list, i, 1,
                const_cast<char*>(s.ip.c_str()));

            std::string cnt = std::to_string(s.client_count) + " client(s)";
            ListView_SetItemText(g_hwnd_list, i, 2,
                const_cast<char*>(cnt.c_str()));
        }
        std::string st = std::to_string(servers.size()) + " serveur(s) trouvé(s)";
        SetWindowTextA(g_hwnd_status, st.c_str());
        return 0;
    }
    case WM_REFRESH_SERVERS:
    {
        ListView_DeleteAllItems(g_hwnd_list);
        std::lock_guard<std::mutex> lock(servers_mutex);
        for (int i = 0; i < (int)servers.size(); i++) 
        {
            const auto& s = servers[i];
            LVITEMA item{};
            item.mask = LVIF_TEXT; item.iItem = i; item.iSubItem = 0;
            std::string name = s.name;
            item.pszText = const_cast<char*>(name.c_str());
            ListView_InsertItem(g_hwnd_list, &item);
            ListView_SetItemText(g_hwnd_list, i, 1,
                const_cast<char*>(s.ip.c_str()));
            
            std::string cnt = std::to_string(s.client_count) + " client(s)";
            ListView_SetItemText(g_hwnd_list, i, 2,
                const_cast<char*>(cnt.c_str()));
        }
        std::string st = std::to_string(servers.size()) + " serveur(s) trouvé(s)";
        SetWindowTextA(g_hwnd_status, st.c_str());
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->idFrom == IDC_SERVER_LIST) {
            if (nm->code == LVN_ITEMCHANGED)
                EnableWindow(g_hwnd_join,
                    ListView_GetNextItem(g_hwnd_list, -1, LVNI_SELECTED) >= 0);
            if (nm->code == NM_DBLCLK)
                PostMessage(hwnd, WM_COMMAND, IDC_BTN_JOIN, 0);
        }
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BTN_REFRESH) {
            { std::lock_guard<std::mutex> lock(servers_mutex); servers.clear(); }
            ListView_DeleteAllItems(g_hwnd_list);
            SetWindowTextA(g_hwnd_status, "Actualisation...");
        }

        if (LOWORD(wParam) == IDC_BTN_JOIN) {
            int idx = ListView_GetNextItem(g_hwnd_list, -1, LVNI_SELECTED);
            if (idx < 0) return 0;

            ServerInfo selected;
            {
                std::lock_guard<std::mutex> lock(servers_mutex);
                if (idx >= (int)servers.size()) return 0;
                selected = servers[idx];
            }

            // Demander le pseudo
            std::string pseudo = ask_pseudo(hwnd);
            if (pseudo.empty()) return 0;

            // Ouvrir la fenêtre de chat et connecter
            open_chat_window(selected, pseudo);
        }
        return 0;
    }

    case WM_DESTROY:
        udp_running.store(false);
        tcp_running.store(false);
        if (g_tcp_socket != INVALID_SOCKET) {
            shutdown(g_tcp_socket, SD_BOTH);
            closesocket(g_tcp_socket);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================
// WINMAIN
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hinstance = hInstance;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) 
        return 1;

    // Enregistrer la fenêtre principale
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndServerListProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ChatClientClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassA(&wc)) 
        return 1;

    HWND hwnd = CreateWindowA(
        "ChatClientClass", "Chat - Recherche de serveurs",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 420,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    WSACleanup();
    return static_cast<int>(m.wParam);
}

/*
// ============================================================
// Identify Win32 controls
// ============================================================
#define IDC_SERVER_LIST 101     // ListView of discovered servers
#define IDC_BTN_JOIN 102        // Button join
#define IDC_BTN_REFRESH 103     // Button refresh
#define IDC_STATUS_LABEL 104    // status Label
// Chat window
#define IDC_CHAT_LOG      201  // Messages display zone (LISTBOX)
#define IDC_CHAT_INPUT    202  // Input field (EDIT)
#define IDC_BTN_SEND      203  // Send button
#define IDC_CHAT_MEMBERS  204  // Members connected

// Messages Windows custom (WM_USER = base des messages custom)
#define WM_UPDATE_SERVERS  (WM_USER + 1) // custom window message to launch a refresh of the list view
#define WM_CHAT_MESSAGE    (WM_USER + 2) // New message received → update the log
#define WM_CHAT_CONNECTED  (WM_USER + 3) // establish TCP connection 
#define WM_CHAT_DISCONNECT (WM_USER + 4) // Server deconnexion

// ============================================================
// Struct: Server Discovered and Messages
// ============================================================
struct ServerInfo
{
    std::string ip;     // Beacon source
    std::string name;   // server name
    int client_count;   // client count
    DWORD last_seen;    // last time you receive the UDP Beacon
};

// a message in the chat log
struct ChatMessage 
{
    std::string pseudo;   // sender ("server" for system message)
    std::string text;     // message core
    COLORREF    color;    // pseudo color (RGB)
};

// ============================================================
// Global state of the application
// ============================================================
std::vector<ServerInfo> servers; // Servers list
std::mutex              servers_mutex;
std::atomic<bool>       beacon_running(true);

// Statu of the TCP chat
SOCKET                  chat_socket = INVALID_SOCKET;
std::atomic<bool>       chat_connected(false);
std::string             my_pseudo;
COLORREF                my_color = RGB(200, 200, 200);

std::vector<ChatMessage> chat_log;   // Messages history
std::mutex               chat_mutex;

// Pseudos color → COLORREF (received from server as "COLOR r g b")
std::map<std::string, COLORREF> pseudo_colors;

HWND g_hwnd_main = NULL;  // handle of the main window
HWND g_hwnd_list = NULL;  // ListView handle
HWND g_hwnd_status = NULL;  // status handle
HWND g_hwnd_join = NULL;  // handle du bouton Rejoindre
HWND g_hwnd_chat = NULL;  // Chat window
HWND g_hwnd_log = NULL;  // LISTBOX of the log
HWND g_hwnd_input = NULL;  // Input field
HWND g_hwnd_members = NULL;  // Members list

HINSTANCE g_hInstance = NULL;

// ============================================================
// SUBCLASSIFICATION OF THE INPUT FIELD
// To intercept the entry touch in a simple EDIT
// we "sub-classify" the control: we replace temporary its WndProc with our own
// which transfer to the first WndProc 
// ============================================================
WNDPROC g_orig_edit_proc = NULL;

LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) 
    {
        // Simulate a clic for the send button
        PostMessage(g_hwnd_chat, WM_COMMAND, IDC_BTN_SEND, 0);
        return 0;
    }
    return CallWindowProcA(g_orig_edit_proc, hwnd, msg, wParam, lParam);
}

// ============================================================
// Open the chat window
// Call when a user click join and confirm
// ============================================================
void open_chat_window(const ServerInfo& server) 
{
    // Ask the pseudo with an input box
    // we use a minimalist win32 dialog box
    char pseudo_buf[MAX_PSEUDO + 1] = {};

    // InputBox with a field EDIT with DialogBox is a complexe thing in Win32.
    // We use a tip: create a temporary dialog window
    // with a memory template. More Easy solution: MessageBox with EDIT
    // Here we manually create a little window 

    // --- Create and register the chat window class ---
    WNDCLASSA wc{};
    wc.lpfnWndProc = ChatWndProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "ChatWindowClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc); // can failed if already register, it's OK

    // Clear the previous log
    {
        std::lock_guard<std::mutex> lock(chat_mutex);
        chat_log.clear();
        pseudo_colors.clear();
    }

    // keep the pseudo
    my_pseudo = std::string(pseudo_buf);
    // Ask the pseudo to the user (simple box)
    // we use a common Win32 dialogue using a temporary window
    // for simplicity: we use a static buffer with an InputBox
    // Solution: create a popup window with an EDIT and two buttons
    // Here we choose a minimalist solution - custom popup dialog

    // create a seizure dialog with a temporary window
    struct PseudoDlg 
    {
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) 
        {
            static HWND edit = NULL;
            switch (m)
            {
            case WM_CREATE:
                CreateWindowA("STATIC", "Choisissez votre pseudo :",
                    WS_CHILD | WS_VISIBLE, 10, 10, 220, 20, h, NULL,
                    GetModuleHandle(NULL), NULL);
                edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    10, 35, 220, 24, h, (HMENU)1, GetModuleHandle(NULL), NULL);
                SendMessage(edit, EM_LIMITTEXT, MAX_PSEUDO, 0);
                SetFocus(edit);
                CreateWindowA("BUTTON", "OK",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    60, 70, 100, 28, h, (HMENU)IDOK,
                    GetModuleHandle(NULL), NULL);
                return 0;
            case WM_COMMAND:
                if (LOWORD(w) == IDOK || (LOWORD(w) == 1 && HIWORD(w) == EN_CHANGE))
                {
                    if (LOWORD(w) == IDOK)
                    {
                        char buf[MAX_PSEUDO + 1] = {};
                        GetWindowTextA(edit, buf, MAX_PSEUDO);
                        // Stocker le pseudo dans g
                        if (strlen(buf) > 0)
                        {
                            my_pseudo = std::string(buf);
                            DestroyWindow(h);
                        }
                    }
                }
                return 0;
            case WM_KEYDOWN:
                if (w == VK_RETURN) 
                    PostMessage(h, WM_COMMAND, IDOK, 0);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcA(h, m, w, l);
        }
    };

    WNDCLASSA dlg_wc{};
    dlg_wc.lpfnWndProc = PseudoDlg::Proc;
    dlg_wc.hInstance = g_hInstance;
    dlg_wc.lpszClassName = "PseudoDlgClass";
    dlg_wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    dlg_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&dlg_wc);

    HWND dlg = CreateWindowA("PseudoDlgClass", "Votre pseudo",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 260, 140,
        g_hwnd_main, NULL, g_hInstance, NULL);
    ShowWindow(dlg, SW_SHOW);

    // Local message loop for this dialog
    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) 
    {
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN)
            PostMessage(dlg, WM_COMMAND, IDOK, 0);
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    if (my_pseudo.empty()) 
        return; // Canceled

    // Create the chat window
    HWND chat_hwnd = CreateWindowA(
        "ChatWindowClass",
        ("Chat - " + my_pseudo + " @ " + server.name).c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        610, 460,
        NULL, NULL, g_hInstance, NULL);

    ShowWindow(chat_hwnd, SW_SHOW);
    UpdateWindow(chat_hwnd);

    // Under class the EDIT field to intercept entry
    g_orig_edit_proc = (WNDPROC)SetWindowLongPtrA(
        g_hwnd_input, GWLP_WNDPROC,
        (LONG_PTR)EditSubclassProc);

    // Lancer le thread TCP
    std::thread(tcp_recv_thread, server.ip).detach();
}

// ============================================================
// UDP thread listener
// 
// - extract Ip source 
// - parse the UdpBeacon struct
// - update the server list
// - update the window
// ============================================================
void udp_listener_thread() 
{
    // create the socket UDP
    SOCKET udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock == INVALID_SOCKET) 
        return;

    // SO_REUSEADDR:: applications can listen on the same port, 
    // usefull with many clients on the same computer
    int reuse = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Bind on INADDR_ANY:UDP_PORT to receive the beacon
    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(UDP_PORT);
    listen_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_sock, reinterpret_cast<sockaddr*>(&listen_addr),
        sizeof(listen_addr)) == SOCKET_ERROR) 
    {
        closesocket(udp_sock);
        return;
    }

    while (beacon_running.load()) 
    {
        UdpBeacon beacon{};
        sockaddr_in sender_addr{};
        int sender_len = sizeof(sender_addr);

        // recvfrom() = recv() for UDP
        // return also the source address in sender_addr
        int n = recvfrom(udp_sock,
            reinterpret_cast<char*>(&beacon),
            sizeof(beacon),
            0,
            reinterpret_cast<sockaddr*>(&sender_addr),
            &sender_len);

        if (n != sizeof(UdpBeacon)) 
            continue; // invalid data or 

        // extract server IP
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr.sin_addr, ip_str, sizeof(ip_str));
        std::string ip(ip_str);

        // Securise the server name (be sure that it is terminated by \0)
        beacon.server_name[MAX_SERVER_NAME - 1] = '\0';
        std::string name(beacon.server_name);

        // Update the server list
        {
            std::lock_guard<std::mutex> lock(servers_mutex);
            bool found = false;
            for (auto& s : servers) 
            {
                if (s.ip == ip) 
                {
                    // already known server : update its data
                    s.name = name;
                    s.client_count = beacon.client_count;
                    s.last_seen = GetTickCount();
                    found = true;
                    break;
                }
            }
            if (!found) 
            {
                // New server
                servers.push_back({ ip, name, beacon.client_count, GetTickCount() });
            }
        }

        // Notify main window to refresh the list view
        if (g_hwnd_main)
            PostMessage(g_hwnd_main, WM_UPDATE_SERVERS, 0, 0);
    }

    closesocket(udp_sock);
}

// ============================================================
// Clean up thread
// 
// delete each server that has not send a beacon since 5 seconds
// ============================================================
void cleanup_thread() {
    while (beacon_running.load()) 
    {
        Sleep(2000);
        {
            std::lock_guard<std::mutex> lock(servers_mutex);
            DWORD now = GetTickCount();
            servers.erase(
                std::remove_if(servers.begin(), servers.end(),
                    [now](const ServerInfo& s) {
                        return (now - s.last_seen) > 5000; // 6 secondes
                    }),
                servers.end()
            );
        }
        if (g_hwnd_main)
            PostMessage(g_hwnd_main, WM_UPDATE_SERVERS, 0, 0);
    }
}

// ============================================================
// refresh the list view
// ============================================================
void refresh_listview() 
{
    // Vider tous les items existants
    ListView_DeleteAllItems(g_hwnd_list);

    std::lock_guard<std::mutex> lock(servers_mutex);

    for (int i = 0; i < (int)servers.size(); i++) 
    {
        const ServerInfo& s = servers[i];

        // LVITEM : structure décrivant un item du ListView
        LVITEMA item{};
        item.mask = LVIF_TEXT; // on veut définir le texte
        item.iItem = i;         // index de la ligne
        item.iSubItem = 0;         // colonne 0 = nom du serveur

        std::string name = s.name;
        item.pszText = const_cast<char*>(name.c_str());
        ListView_InsertItem(g_hwnd_list, &item);

        // Colonne 1 : IP
        ListView_SetItemText(g_hwnd_list, i, 1,
            const_cast<char*>(s.ip.c_str()));

        // Colonne 2 : nombre de clients
        std::string count = std::to_string(s.client_count) + " client(s)";
        ListView_SetItemText(g_hwnd_list, i, 2,
            const_cast<char*>(count.c_str()));
    }

    // Mettre à jour le label de statut
    std::string status = std::to_string(servers.size()) + " serveur(s) trouvé(s)";
    SetWindowTextA(g_hwnd_status, status.c_str());
}

// ============================================================
//  Get the selected server
// ============================================================
int get_selected_server() 
{
    return ListView_GetNextItem(g_hwnd_list, -1, LVNI_SELECTED);
}

// ============================================================
// Window principal window system
// the function receive all the windows messages
// for this window : creation, clic, size etc...
//
// LRESULT CALLBACK = call Win32 for the callbacks
// HWND    = window handle
// UINT    = Message identifier (WM_CREATE, WM_COMMAND, etc.)
// WPARAM / LPARAM = message param
// ============================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg) 
    {

    // --------------------------------------------------------
    // WM_CREATE : Window creation
    // create all the child controls
    // --------------------------------------------------------
    case WM_CREATE: 
    {
        g_hwnd_main = hwnd;

        // Initialise commons controls
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icc);

        // --- Title ---
        CreateWindowA("STATIC", "Serveurs disponibles sur le reseau :",
            WS_CHILD | WS_VISIBLE,
            10, 10, 400, 20,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        // --- Server ListView ---
        // LVS_REPORT     : vue "détails" avec colonnes
        // LVS_SINGLESEL  : une seule sélection à la fois
        // LVS_SHOWSELALWAYS : garde la sélection visible même sans focus
        // WS_EX_CLIENTEDGE : bordure enfoncée (style moderne)
        g_hwnd_list = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWA, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            10, 35, 560, 280,
            hwnd, (HMENU)IDC_SERVER_LIST,
            GetModuleHandle(NULL), NULL);

        // Activer le style "grille" et la sélection pleine ligne
        ListView_SetExtendedListViewStyle(g_hwnd_list,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // Ajouter les colonnes du ListView
        // LVCOLUMN : structure décrivant une colonne
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH; // on définit texte + largeur

        col.pszText = (char*)"Nom du serveur"; col.cx = 220;
        ListView_InsertColumn(g_hwnd_list, 0, &col);

        col.pszText = (char*)"Adresse IP";     col.cx = 140;
        ListView_InsertColumn(g_hwnd_list, 1, &col);

        col.pszText = (char*)"Clients";        col.cx = 100;
        ListView_InsertColumn(g_hwnd_list, 2, &col);

        // --- Refresh button ---
        CreateWindowA("BUTTON", "Actualiser",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 325, 120, 30,
            hwnd, (HMENU)IDC_BTN_REFRESH,
            GetModuleHandle(NULL), NULL);

        // --- Join button ---
        g_hwnd_join = CreateWindowA("BUTTON", "Rejoindre ->",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            // WS_DISABLED : disabled by default, enable when selected
            440, 325, 130, 30,
            hwnd, (HMENU)IDC_BTN_JOIN,
            GetModuleHandle(NULL), NULL);

        // --- statut ---
        g_hwnd_status = CreateWindowA("STATIC", "Recherche de serveurs...",
            WS_CHILD | WS_VISIBLE,
            10, 365, 400, 20,
            hwnd, (HMENU)IDC_STATUS_LABEL,
            GetModuleHandle(NULL), NULL);

        // launch the UDP listen thread
        std::thread(udp_listener_thread).detach();
        std::thread(cleanup_thread).detach();

        return 0;
    }

    // --------------------------------------------------------
    // WM_UPDATE_SERVERS : Custom message send by thread
    // Refresh the ListView with the current list
    // --------------------------------------------------------
    case WM_UPDATE_SERVERS:
        refresh_listview();
        return 0;

        // --------------------------------------------------------
        // WM_NOTIFY : Notify from child controls
        // ListView send LVN_ITEMCHANGED when selection change
        // --------------------------------------------------------
    case WM_NOTIFY: 
    {
        NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == IDC_SERVER_LIST) 
        {
            if (nmhdr->code == LVN_ITEMCHANGED) 
            {
                // Activate button join only if a server is selected
                bool has_selection = (get_selected_server() >= 0);
                EnableWindow(g_hwnd_join, has_selection ? TRUE : FALSE);
            }
            // Double-clic on a server = join directly
            if (nmhdr->code == NM_DBLCLK) 
            {
                PostMessage(hwnd, WM_COMMAND, IDC_BTN_JOIN, 0);
            }
        }
        return 0;
    }

    // --------------------------------------------------------
    // WM_COMMAND : clic on a button or action control
    // wParam (low word) = control ID
    // --------------------------------------------------------
    case WM_COMMAND: 
    {
        int ctrl_id = LOWORD(wParam);

        if (ctrl_id == IDC_BTN_REFRESH) 
        {
            // Clean the server list and wait next beacon
            {
                std::lock_guard<std::mutex> lock(servers_mutex);
                servers.clear();
            }
            refresh_listview();
            SetWindowTextA(g_hwnd_status, "Refresh...");
        }

        if (ctrl_id == IDC_BTN_JOIN)
        {
            int idx = get_selected_server();
            if (idx < 0) 
                return 0;
            ServerInfo sel;
            {
                std::lock_guard<std::mutex> lock(servers_mutex);
                if (idx >= (int)servers.size()) 
                    return 0;
                sel = servers[idx];
            }
            open_chat_window(sel);
        }
        return 0;
    }

    // --------------------------------------------------------
    // WM_DESTROY : window destroyed → leave the message loop
    // --------------------------------------------------------
    case WM_DESTROY:
        beacon_running.store(false);
        PostQuitMessage(0); // Send WM_QUIT in the message queue
        return 0;
    }

    // default way of managing messages
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================
// WINMAIN : starting point for Win32 GUI
// replace main() for windowed application
// ============================================================
auto initializeWinSockLibrary()
{
    WSADATA data;
    int init_result = WSAStartup(MAKEWORD(2, 2), &data);
    if (init_result != 0)
    {
        std::cout << "WSAStartup() échoué: " << init_result << std::endl;
        return false;
    }
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) 
{
    initializeWinSockLibrary();

    // --------------------------------------------------------
    // Register the window class
    // 
    // Before create a window, we need to describe its behavior
    // in a WNDCLASSA and registered it in windows
    // --------------------------------------------------------
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;          // function receiving message
    wc.hInstance = hInstance;
    wc.lpszClassName = "ChatClientClass"; // Unique class name
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // background color
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassA(&wc)) 
        return 1;

    // --------------------------------------------------------
    // Create main window
    // WS_OVERLAPPEDWINDOW = standard window with title, border,
    //                       buttons min/max/close
    // CW_USEDEFAULT = let windows choose the position/size
    // --------------------------------------------------------
    HWND hwnd = CreateWindowA(
        "ChatClientClass",          // classe registered
        "Chat - Recherche de serveurs", // window title
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        // & ~WS_THICKFRAME   : disable resize
        // & ~WS_MAXIMIZEBOX  : disable maximize button
        CW_USEDEFAULT, CW_USEDEFAULT,
        600, 420,                   // width, height in pixels
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) 
        return 1;

    ShowWindow(hwnd, nCmdShow);   // Display window
    UpdateWindow(hwnd);           // Force a first draw (WM_PAINT)

    // --------------------------------------------------------
    // MESSAGEs Loop
    // Heart of each Win32 application
    // GetMessage() block until it receive a message.
    // TranslateMessage() translate inputs in characters (WM_CHAR).
    // DispatchMessage() send a message to the right WndProc.
    // the loop finished when GetMessage() receive WM_QUIT (return 0).
    // --------------------------------------------------------
    MSG msg_loop;
    while (GetMessage(&msg_loop, NULL, 0, 0)) 
    {
        TranslateMessage(&msg_loop);
        DispatchMessage(&msg_loop);
    }

    WSACleanup();
    return static_cast<int>(msg_loop.wParam);
}*/