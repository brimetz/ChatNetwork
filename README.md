# ChatNetwork
C++ Chat application 

Personnal project to learn things around network:

- TCP
- UDP
- Sockets
- Protocoles r�seau


# Compilation

server.cpp - Serveur de chat Win32 (Winsock2)
Compilation : g++ -std=c++17 server.cpp -o server.exe -lws2_32 -lpthread

ou avec MSVC : cl server.cpp /Feserver.exe

client.cpp - Client de chat Win32 (Winsock2)
Compilation : g++ -std=c++17 client.cpp -o client.exe -lws2_32 -lpthread
ou avec MSVC : cl client.cpp /Feclient.exe


cl client/src/main.cpp /EHsc /std:c++17 /Feclient.exe /link ws2_32.lib user32.lib gdi32.lib comctl32.lib
cl server/src/main.cpp /EHsc /std:c++17 /Feserver.exe /link ws2_32.lib user32.lib gdi32.lib comctl32.lib
