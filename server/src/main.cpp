#include "../../common.h"

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <string>
#include <atomic>

// ============================================================
// Server configuration
// ============================================================
std::string SERVER_NAME = "My Server Chat";

// ============================================================
// Client Struct : Client connected in TCP
// ============================================================
struct Client 
{
    SOCKET      fd;       // socket TCP for this client
    std::string pseudo;   // pseudo chose at the connection
    std::string color;    // color ANSI linked
    COLORREF    colorref; // same color in COLORREF Win32
};

std::vector<Client> clients;
std::mutex          clients_mutex;
std::atomic<int>    client_count(0); // Clients currently connected
int                 color_index = 0; // Next color index

// ============================================================
// Network tools
// ============================================================

// Send message to one socket
void send_to(SOCKET fd, const std::string& msg)
{
    send(fd, msg.c_str(), static_cast<int>(msg.size()), 0);
}

// Send a message to all clients except the sender
void broadcast(const std::string& msg, SOCKET sender) 
{
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (Client client : clients)
    {
        if (client.fd != sender) 
        {
            send_to(client.fd, msg);
        }
    }
}

// Send a message at all clients.
void broadcast_all(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (Client client : clients)
    {
        send_to(client.fd, msg);
    }
}

// Receive a complete line from a socket
// return false if connexion is closed or there is an error
bool recv_line(SOCKET fd, std::string& out)
{
    out.clear();
    char c;
    while (true) 
    {
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) return false;  // 0 = deconnexion, <0 = error
        if (c == '\n') break;
        if (c != '\r') out += c;   // ignored \r (Windows send \r\n)
    }
    return true;
}

// ============================================================
// Thread managing Beacon UDP
// 
// - create a UDP socket
// - active SO_BROADCAST on the socket
// - Send to 255.255.255.255:UDP_PORT
// ============================================================
void udp_beacon_thread() 
{
    // create a UDP socket (SOCK_DGRAM = without connection, datagrammes)
    SOCKET udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock == INVALID_SOCKET) 
    {
        std::cerr << "[UDP] socket() failed : " << WSAGetLastError() << std::endl;
        return;
    }

    // activate the broadcast on this socket
    // without this option, sendto() to 255.255.255.255 return an error
    int broadcast_opt = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast_opt), sizeof(broadcast_opt));

    // Destination address : broadcast local network, port UDP_PORT
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST; // = 255.255.255.255

    std::cout << "[UDP] Beacon started (port " << UDP_PORT << ")" << std::endl;

    while (true) 
    {
        // load the beacon pack
        UdpBeacon beacon{};
        // strncpy_s : securate version of strncpy (MSVC)
        // copy MAX_SERVER_NAME-1 characters + '\0' final
        strncpy_s(beacon.server_name, SERVER_NAME.c_str(), MAX_SERVER_NAME - 1);
        beacon.client_count = client_count.load(); // atomic read

        // send the beacon in broadcast
        // sendto() = send() for UDP (No connexion needed)
        // we precise the destination address at each send
        sendto(udp_sock,
            reinterpret_cast<const char*>(&beacon),     // raw data
            sizeof(beacon),                             // size stable
            0,
            reinterpret_cast<sockaddr*>(&broadcast_addr),
            sizeof(broadcast_addr));

        // Wait the next beacon
        Sleep(UDP_BEACON_MS); // Sleep() = Win32 function (equal to sleep())
    }

    closesocket(udp_sock);
}

// ============================================================
// Thread for each TCP Client
// ============================================================
void handle_client(SOCKET client_fd) 
{
    // Ask pseudo
    send_to(client_fd, "PSEUDO_REQUEST\n");
    // Send tag "PSEUDO_REQUEST" for the client to detect that it is a special message

    std::string pseudo;
    if (!recv_line(client_fd, pseudo) || pseudo.empty()) 
    {
        closesocket(client_fd);
        return;
    }

    if (pseudo.size() > MAX_PSEUDO) 
        pseudo = pseudo.substr(0, MAX_PSEUDO); // limit the pseudo's length
    
    // Link a color
    std::string color;
    COLORREF    colorref; 
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        int idx = color_index % ANSI_COLOR_COUNT;
        color = ANSI_COLORS[idx];
        colorref = WIN32_COLORS[idx];
        color_index++;
        clients.push_back({ client_fd, pseudo, color, colorref });
    }
    client_count.fetch_add(1); // Add one to the count

    // Send Color to the client as RGB text
    int r = GetRValue(colorref);
    int g = GetGValue(colorref);
    int b = GetBValue(colorref);
    std::string color_msg = "COLOR " + std::to_string(r) + " " +
        std::to_string(g) + " " +
        std::to_string(b) + "\n";
    send_to(client_fd, color_msg);

    // Display connexion on Server console
    std::cout << color << "[+] " << pseudo << " a rejoint le chat" << ANSI_RESET << std::endl;

    // Welcoming message
    send_to(client_fd, color + "Bienvenue " + pseudo + " !" + ANSI_RESET + "\n");
    std::string join_msg = color + "*** " + pseudo + " a rejoint le chat ***" + ANSI_RESET + "\n";
    broadcast(join_msg, client_fd);

    // Chat loop
    while (true) 
    {
        std::string msg;
        if (!recv_line(client_fd, msg)) 
            break; // deconnexion
        if (msg.empty()) 
            continue;

        // Formater : [pseudo in color] message in white
        std::string formatted = color + "[" + pseudo + "] " + ANSI_RESET + msg + "\n";

        std::cout << color << "[" << pseudo << "] " << ANSI_RESET << msg << std::endl;
        broadcast(formatted, client_fd);
    }

    // Deconnexion
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(
            std::remove_if(clients.begin(), clients.end(),
                [client_fd](const Client& c) { return c.fd == client_fd; }),
            clients.end()
        );
    }

    std::string leave_msg = color + "*** " + pseudo + " left the chat ***" + ANSI_RESET + "\n";
    broadcast_all(leave_msg);
    std::cout << color << "[-] " << pseudo << " left the chat" << ANSI_RESET << std::endl;

    closesocket(client_fd);
}
// ============================================================
// Main
// ============================================================
auto initializeWinLibrary()
{
    WSADATA data;
    int init_result = WSAStartup(MAKEWORD(2, 2), &data);
    if (init_result != 0)
    {
        std::cout << "WSAStartup() failed: " << init_result << std::endl;
        return false;
    }
    return true;
}

auto createSocket()
{
    // AF_INET      : IPv4 (AF_INET6 pour IPv6)
    // SOCK_STREAM  : socket TCP (flux d'octets fiable, stable)
    //                SOCK_DGRAM = UDP (datagrammes, sans connexion)
    return socket(AF_INET /*IPV4*/, SOCK_STREAM /*TCP*/, IPPROTO_TCP /*IPPROTO_TCP = protocole TCP explicite*/);
}

int main(int argc, char* argv[])
{
    // Optionnal: Pass the server name as argument
    // example: server.exe "Bap's server"
    if (argc >= 2) 
        SERVER_NAME = argv[1];

    // enable ANSI Color in windows console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (!initializeWinLibrary())
    {
        return 1;
    }

    // Server socket creation
    SOCKET listenSocket = createSocket();
    if (listenSocket == INVALID_SOCKET) 
    {
        std::cerr << "socket() failed : " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&opt), sizeof(opt));

    // adress configuration
    sockaddr_in addr{};
    addr.sin_family = AF_INET;          // IPV4
    addr.sin_port = htons(TCP_PORT);    // htons : host to network short
    addr.sin_addr.s_addr = INADDR_ANY;  // listen on each IP : 255.255.255.255

    // bind() : attach the socket to the address IP:port
    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
    {
        std::cerr << "bind() failed : " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // listen() : pass in passive listen mode
    // 10 = backlog (size of the connection waiting queue)
    if (listen(listenSocket, 10) == SOCKET_ERROR) 
    {
        std::cerr << "listen() failed : " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Launch UDP Thread beacon in background
    std::thread(udp_beacon_thread).detach();

    std::cout << "=== Server \"" << SERVER_NAME << "\" ===" << std::endl;
    std::cout << "TCP listen on port " << TCP_PORT << std::endl;
    std::cout << "UDP beacon on port    " << UDP_PORT << std::endl;
    std::cout << "(launch client.exe to connect)" << std::endl;

    // Principal loop : Accept clients
    while (true) 
    {
        sockaddr_in client_addr{};
        int         client_len = sizeof(client_addr);

        // accept() : block the thread until someone connect
        // Return a new SOCKET for this client
        SOCKET client_fd = accept(listenSocket,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len);

        if (client_fd == INVALID_SOCKET) 
        {
            std::cerr << "accept() failed : " << WSAGetLastError() << std::endl;
            continue;
        }

        // Display client IP
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "New client from : " << client_ip << std::endl;

        // Each client is managed in its own thread
        std::thread(handle_client, client_fd).detach();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}