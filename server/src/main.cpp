#define WIN32_LEAN_AND_MEAN  // évite les conflits entre windows.h et winsock2.h
#include <winsock2.h>        // API socket Windows
#include <ws2tcpip.h>        // inet_pton, inet_ntop (InetPton sur certains MSVC)
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

// ANSI Colors
const std::vector<std::string> COLORS =
{
    "\033[31m",  // Red
    "\033[32m",  // Green
    "\033[33m",  // Yellow
    "\033[34m",  // Blue
    "\033[35m",  // Magenta
    "\033[36m",  // Cyan
    "\033[91m",  // Light red
    "\033[92m",  // Light green
    "\033[93m",  // Light yellow
    "\033[94m",  // Light blue
};
const std::string RESET = "\033[0m";

struct Client
{
    SOCKET fd; // Socket Description
    std::string pseudo; // Client Pseudo
    std::string color; // ANSI color linked
};


std::vector<Client> clients; // liste des sockets clients connectés
std::mutex          clients_mutex; // mutex to access clients vector array
int next_color_index = 0; // Next Color to link

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
    while (true) {
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) return false;  // 0 = deconnexion, <0 = erreur
        if (c == '\n') break;
        if (c != '\r') out += c;   // ignorer \r (Windows envoie \r\n)
    }
    return true;
}

// Fonction exécutée dans un thread par client
void handle_client(SOCKET client_fd) 
{
    // Ask pseudo
    send_to(client_fd, "Entrez votre pseudo : ");
    std::string pseudo;
    if (!recv_line(client_fd, pseudo) || pseudo.empty()) 
    {
        closesocket(client_fd);
        return;
    }
    //if(pseudo.size() > 20) pseudo = pseudo.substr(0, 20); // limiter a 20 caracteres
    
    // Link a color
    std::string color;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        color = COLORS[next_color_index % COLORS.size()];
        next_color_index++;
        clients.push_back({ client_fd, pseudo, color });
    }

    // Display connexion on Server console
    std::cout << color << "[+] " << pseudo << " a rejoint le chat" << RESET << std::endl;

    // Welcoming message
    send_to(client_fd, color + "Bienvenue " + pseudo + " !" + RESET + "\n");
    std::string join_msg = color + "*** " + pseudo + " a rejoint le chat ***" + RESET + "\n";
    broadcast(join_msg, client_fd);

    // Chat loop
    while (true) 
    {
        std::string msg;
        if (!recv_line(client_fd, msg)) 
            break; // deconnexion
        if (msg.empty()) 
            continue;

        // Formater : [pseudo en couleur] message en blanc
        std::string formatted = color + "[" + pseudo + "] " + RESET + msg + "\n";

        std::cout << color << "[" << pseudo << "] " << RESET << msg << std::endl;
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

    std::string leave_msg = color + "*** " + pseudo + " a quitte le chat ***" + RESET + "\n";
    broadcast_all(leave_msg);
    std::cout << color << "[-] " << pseudo << " a quitte le chat" << RESET << std::endl;

    closesocket(client_fd);
}

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
    // AF_INET      : famille d'adresses IPv4 (AF_INET6 pour IPv6)
    // SOCK_STREAM  : socket TCP (flux d'octets fiable, ordonné)
    //                SOCK_DGRAM = UDP (datagrammes, sans connexion)
    return socket(AF_INET /*IPV4*/, SOCK_STREAM /*TCP*/, IPPROTO_TCP /*IPPROTO_TCP = protocole TCP explicite*/);
}

int main() 
{
    int port = 8080;

    // enable ANSI Color in windows console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (!initializeWinLibrary())
    {
        return 1;
    }

    // Création du socket serveur
    SOCKET listenSocket = createSocket();
    if (listenSocket == INVALID_SOCKET) 
    {
        std::cerr << "socket() échoué : " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&opt), sizeof(opt));

    // Configuration de l'adresse
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);   // htons : host to network short
    addr.sin_addr.s_addr = INADDR_ANY;    // écouter sur toutes les interfaces

    // bind() : attache le socket à l'adresse IP:port
    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
    {
        std::cerr << "bind() échoué : " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // listen() : passe en mode écoute passive
    // 10 = backlog (taille de la file d'attente des connexions entrantes)
    if (listen(listenSocket, 10) == SOCKET_ERROR) 
    {
        std::cerr << "listen() échoué : " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Serveur en écoute sur le port " << port << "..." << std::endl;

    // Boucle principale : accepter les clients indéfiniment
    while (true) 
    {
        sockaddr_in client_addr{};
        int         client_len = sizeof(client_addr); // int sous Windows (pas socklen_t)

        // accept() : bloque jusqu'à ce qu'un client se connecte
        // Retourne un nouveau SOCKET dédié à ce client
        SOCKET client_fd = accept(listenSocket,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len);

        if (client_fd == INVALID_SOCKET) 
        {
            std::cerr << "accept() échoué : " << WSAGetLastError() << std::endl;
            continue;
        }

        // Afficher l'IP du client
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "Nouveau client depuis : " << client_ip << std::endl;

        // Chaque client est géré dans son propre thread
        std::thread(handle_client, client_fd).detach();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}