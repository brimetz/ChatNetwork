#include "../../common.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <commctrl.h>  // ListView, Win32 controls
#include <map>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

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
}

auto createSocket()
{
    // AF_INET      : famille d'adresses IPv4 (AF_INET6 pour IPv6)
    // SOCK_STREAM  : socket TCP (flux d'octets fiable, ordonné)
    //                SOCK_DGRAM = UDP (datagrammes, sans connexion)
    return socket(AF_INET /*IPV4*/, SOCK_STREAM /*TCP*/, IPPROTO_TCP /*IPPROTO_TCP = protocole TCP explicite*/);
}