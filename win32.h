#ifndef MCMAP_WIN32_H
#define MCMAP_WIN32_H 1

#undef main

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#undef RGB

typedef SOCKET socket_t;

#endif /* MCMAP_WIN32_H */
