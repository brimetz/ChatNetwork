#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <string>
#include <thread>
#include <atomic>

#include <vector>
#include <mutex>

std::atomic<bool> running(true);

std::string currentMessage;
std::mutex  message_mutex;

// Thread d'écoute : reçoit les messages du serveur en arrière-plan
void receive_messages(SOCKET sock_fd)
{
    char buffer[1024];

    while (running.load())
    {
        memset(buffer, 0, sizeof(buffer));

        // recv() : bloque jusqu'à réception de données
        // Retourne : nb d'octets lus, 0 si serveur déconnecté, SOCKET_ERROR si erreur
        int n = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);

        if (n <= 0) 
        {
            if (running.load()) 
            { // évite le message si on a demandé la déconnexion
                std::cout << "\nServeur déconnecté." << std::endl;
            }
            running.store(false);
            break;
        }


        {
            std::lock_guard<std::mutex> lock(message_mutex);
            currentMessage.clear();
        }

        // Affichage du message reçu
        // \r efface la ligne de saisie en cours pour un affichage propre
        std::cout << "\r" << std::string(buffer, n-1) << std::endl;
        std::cout << "> " << std::flush; // re-afficher le prompt
    }
}

auto initializeWinLibrary()
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
    char* addrIP = "127.0.0.1";

    // enable ANSI Color in windows console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (!initializeWinLibrary())
    {
        return 1;
    }

    // Création du socket client
    SOCKET listenSocket = createSocket();
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "socket() échoué : " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Configuration de l'adresse du serveur
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // inet_pton : convertit "127.0.0.1" (texte) en entier 32-bit réseau
    // Retourne 1 si succès, 0 si adresse invalide, -1 si famille invalide
    if (inet_pton(AF_INET, addrIP, &server_addr.sin_addr) != 1) {
        std::cerr << "Adresse IP invalide." << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // connect() : établit la connexion TCP avec le serveur (bloquant)
    // Effectue le handshake SYN → SYN-ACK → ACK
    if (connect(listenSocket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR)
    {
        std::cerr << "connect() échoué : " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connecté au serveur sur " << addrIP << ":" << port << std::endl;
    std::cout << "Tapez vos messages (Ctrl+C ou 'quit' pour quitter) :" << std::endl;

    // Wait the pseudo asking from the server
    char prompt[128] = {};
    recv(listenSocket, prompt, sizeof(prompt) - 1, 0);
    std::cout << prompt << std::flush;

    // Lire le pseudo et l'envoyer au serveur (avec \n pour terminer la ligne)
    std::string pseudo;
    std::getline(std::cin, pseudo);
    pseudo += "\n"; // le serveur attend un '\n' pour savoir que c'est fini
    send(listenSocket, pseudo.c_str(), static_cast<int>(pseudo.size()), 0);

    // Lancer le thread d'écoute en arrière-plan
    // .detach() : le thread s'exécute indépendamment du thread principal
    std::thread(receive_messages, listenSocket).detach();

    // Thread principal : lire les messages depuis la console et les envoyer
    std::cout << "> " << std::flush;

    while (running.load() && std::getline(std::cin, currentMessage))
    {
        if (currentMessage == "quit" || currentMessage == "exit")
            break;
        if (currentMessage.empty())
        {
            std::cout << "> " << std::flush;
            continue;
        }

        // Ajouter \n pour que recv_line() cote serveur detecte la fin du message
        currentMessage += "\n";
        // send() : envoie les données au serveur
        int sent = send(listenSocket,
            currentMessage.c_str(),
            static_cast<int>(currentMessage.size()),
            0);

        if (sent == SOCKET_ERROR) 
        {
            std::cerr << "send() échoué : " << WSAGetLastError() << std::endl;
            break;
        }

        std::cout << "> " << std::flush;
    }

    // Arrêt propre
    running.store(false);

    // Shutdown the window library, will unlock recv() in the listen thread
    // SD_BOTH = arrêter l'envoi ET la réception
    shutdown(listenSocket, SD_BOTH); // SD_SEND / SD_RECEIVE / SD_BOTH

    closesocket(listenSocket);

    WSACleanup();
    return 0;
}