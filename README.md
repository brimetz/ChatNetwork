# ChatNetwork
C++ Chat application 

Personnal project to learn things around network:

- TCP
- UDP
- Sockets
- Protocoles réseau


# Compilation

server.cpp - Serveur de chat Win32 (Winsock2)
Compilation : g++ -std=c++17 server.cpp -o server.exe -lws2_32 -lpthread

ou avec MSVC : cl server.cpp /Feserver.exe

client.cpp - Client de chat Win32 (Winsock2)
Compilation : g++ -std=c++17 client.cpp -o client.exe -lws2_32 -lpthread
ou avec MSVC : cl client.cpp /Feclient.exe