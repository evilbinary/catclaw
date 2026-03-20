#include "websocket.h"
#include "channels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
// Define Windows equivalents for Linux
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define MAKEWORD(a, b) ((a) | ((b) << 8))
#define WSAGetLastError() errno
#define closesocket close
#endif

// Check endianness
static bool is_little_endian(void) {
    uint16_t test = 0x1234;
    return *((uint8_t *)&test) == 0x34;
}

// Platform-specific initialization and cleanup
#ifdef _WIN32
static bool platform_init(void) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return false;
    }
    return true;
}

static void platform_cleanup(void) {
    WSACleanup();
}
#else
static bool platform_init(void) {
    // No initialization needed for Linux
    return true;
}

static void platform_cleanup(void) {
    // No cleanup needed for Linux
}
#endif

static bool websocket_handshake(WebSocketConnection *conn);
static bool websocket_parse_frame(WebSocketConnection *conn, char **message, int *message_len);
static bool websocket_send_frame(WebSocketConnection *conn, const char *data, int len);

bool websocket_server_init(WebSocketServer *server, int port, int max_connections) {
    struct sockaddr_in addr;

    // Initialize platform
    if (!platform_init()) {
        return false;
    }

    // Create server socket
    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        platform_cleanup();
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == SOCKET_ERROR) {
        fprintf(stderr, "setsockopt failed: %d\n", WSAGetLastError());
        closesocket(server->server_socket);
        platform_cleanup();
        return false;
    }

    // Bind socket
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server->server_socket, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(server->server_socket);
        platform_cleanup();
        return false;
    }

    // Listen for connections
    if (listen(server->server_socket, 5) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
        closesocket(server->server_socket);
        platform_cleanup();
        return false;
    }

    // Initialize connections
    server->port = port;
    server->running = false;
    server->max_connections = max_connections;
    server->connection_count = 0;
    server->connections = (WebSocketConnection *)malloc(sizeof(WebSocketConnection) * max_connections);
    if (!server->connections) {
        fprintf(stderr, "malloc failed\n");
        closesocket(server->server_socket);
        platform_cleanup();
        return false;
    }

    memset(server->connections, 0, sizeof(WebSocketConnection) * max_connections);

    printf("WebSocket server initialized on port %d\n", port);
    return true;
}

bool websocket_server_start(WebSocketServer *server) {
    if (server->running) {
        printf("WebSocket server is already running\n");
        return true;
    }

    server->running = true;
    printf("WebSocket server started on port %d\n", server->port);

    // Start server thread
    // For simplicity, we'll use a blocking approach here
    while (server->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server->server_socket, &read_fds);

        // Add existing connections to the set
        for (int i = 0; i < server->max_connections; i++) {
            if (server->connections[i].socket != 0) {
                FD_SET(server->connections[i].socket, &read_fds);
            }
        }

        // Wait for activity
        int activity = select(0, &read_fds, NULL, NULL, NULL);
        if (activity == SOCKET_ERROR) {
            fprintf(stderr, "select failed: %d\n", WSAGetLastError());
            break;
        }

        // Check for new connections
        if (FD_ISSET(server->server_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            int client_addr_len = sizeof(client_addr);
            SOCKET client_socket = accept(server->server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
            if (client_socket == INVALID_SOCKET) {
                fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
                continue;
            }

            // Find an empty slot for the new connection
            int slot = -1;
            for (int i = 0; i < server->max_connections; i++) {
                if (server->connections[i].socket == 0) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1) {
                fprintf(stderr, "Maximum connections reached\n");
                closesocket(client_socket);
                continue;
            }

            // Initialize new connection
            server->connections[slot].socket = client_socket;
            server->connections[slot].handshake_completed = false;
            server->connections[slot].buffer_len = 0;
            server->connection_count++;

            printf("New connection accepted\n");
        }

        // Check existing connections for data
        for (int i = 0; i < server->max_connections; i++) {
            if (server->connections[i].socket != 0 && FD_ISSET(server->connections[i].socket, &read_fds)) {
                WebSocketConnection *conn = &server->connections[i];
                int bytes_received = recv(conn->socket, conn->buffer + conn->buffer_len, sizeof(conn->buffer) - conn->buffer_len, 0);

                if (bytes_received <= 0) {
                    // Connection closed or error
                    printf("Connection closed\n");
                    closesocket(conn->socket);
                    conn->socket = 0;
                    conn->handshake_completed = false;
                    conn->buffer_len = 0;
                    server->connection_count--;
                } else {
                    conn->buffer_len += bytes_received;

                    if (!conn->handshake_completed) {
                        // Handle handshake
                        if (websocket_handshake(conn)) {
                            conn->handshake_completed = true;
                            printf("WebSocket handshake completed\n");
                        }
                    } else {
                        // Handle WebSocket frames
                        char *message = NULL;
                        int message_len = 0;
                        while (websocket_parse_frame(conn, &message, &message_len)) {
                            if (message) {
                                printf("Received message: %s\n", message);
                                // Handle message through channels
                                channels_handle_websocket_message(message);
                                // Echo message back
                                websocket_send(conn, message);
                                free(message);
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

void websocket_server_stop(WebSocketServer *server) {
    if (!server->running) {
        printf("WebSocket server is not running\n");
        return;
    }

    server->running = false;

    // Close all connections
    for (int i = 0; i < server->max_connections; i++) {
        if (server->connections[i].socket != 0) {
            closesocket(server->connections[i].socket);
            server->connections[i].socket = 0;
        }
    }

    // Close server socket
    closesocket(server->server_socket);
    platform_cleanup();

    printf("WebSocket server stopped\n");
}

void websocket_server_cleanup(WebSocketServer *server) {
    if (server->running) {
        websocket_server_stop(server);
    }

    if (server->connections) {
        free(server->connections);
        server->connections = NULL;
    }

    printf("WebSocket server cleaned up\n");
}

bool websocket_send(WebSocketConnection *conn, const char *message) {
    if (!conn->handshake_completed) {
        fprintf(stderr, "Handshake not completed\n");
        return false;
    }

    return websocket_send_frame(conn, message, strlen(message));
}

static bool websocket_handshake(WebSocketConnection *conn) {
    // Look for end of HTTP request
    char *end = strstr(conn->buffer, "\r\n\r\n");
    if (!end) {
        return false;
    }

    // Find Sec-WebSocket-Key header
    char *key_start = strstr(conn->buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        fprintf(stderr, "Sec-WebSocket-Key header not found\n");
        return false;
    }

    key_start += 19; // Skip "Sec-WebSocket-Key: "
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) {
        fprintf(stderr, "Invalid Sec-WebSocket-Key header\n");
        return false;
    }

    // Extract key
    int key_len = key_end - key_start;
    char key[256];
    strncpy(key, key_start, key_len);
    key[key_len] = '\0';

    // Generate response key (simplified - in real implementation use SHA-1)
    char response[256];
    snprintf(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\n" 
             "Upgrade: websocket\r\n" 
             "Connection: Upgrade\r\n" 
             "Sec-WebSocket-Accept: dGhlIHNhbXBsZSBub25jZQ==\r\n" 
             "\r\n");

    // Send response
    send(conn->socket, response, strlen(response), 0);

    // Clear buffer
    conn->buffer_len = 0;

    return true;
}

static bool websocket_parse_frame(WebSocketConnection *conn, char **message, int *message_len) {
    if (conn->buffer_len < 2) {
        return false;
    }

    uint8_t *buffer = (uint8_t *)conn->buffer;
    int offset = 0;

    // First byte: FIN and opcode
    bool fin = (buffer[offset] & 0x80) != 0;
    uint8_t opcode = buffer[offset++] & 0x0F;

    // Second byte: Mask and payload length
    bool mask = (buffer[offset] & 0x80) != 0;
    uint64_t payload_len = buffer[offset++] & 0x7F;

    if (payload_len == 126) {
        if (conn->buffer_len < offset + 2) {
            return false;
        }
        payload_len = ntohs(*(uint16_t *)&buffer[offset]);
        offset += 2;
    } else if (payload_len == 127) {
        if (conn->buffer_len < offset + 8) {
            return false;
        }
        payload_len = ntohll(*(uint64_t *)&buffer[offset]);
        offset += 8;
    }

    // Masking key
    uint8_t mask_key[4] = {0};
    if (mask) {
        if (conn->buffer_len < offset + 4) {
            return false;
        }
        memcpy(mask_key, &buffer[offset], 4);
        offset += 4;
    }

    // Payload
    if (conn->buffer_len < offset + payload_len) {
        return false;
    }

    // Allocate message
    *message = (char *)malloc(payload_len + 1);
    if (!*message) {
        return false;
    }

    // Copy and unmask payload
    for (uint64_t i = 0; i < payload_len; i++) {
        (*message)[i] = buffer[offset + i] ^ mask_key[i % 4];
    }
    (*message)[payload_len] = '\0';
    *message_len = payload_len;

    // Shift buffer
    conn->buffer_len -= offset + payload_len;
    memmove(conn->buffer, conn->buffer + offset + payload_len, conn->buffer_len);

    return true;
}

static bool websocket_send_frame(WebSocketConnection *conn, const char *data, int len) {
    uint8_t frame[4096];
    int offset = 0;

    // First byte: FIN and opcode
    frame[offset++] = 0x81; // FIN = 1, opcode = 1 (text)

    // Second byte: Mask and payload length
    if (len <= 125) {
        frame[offset++] = len;
    } else if (len <= 65535) {
        frame[offset++] = 126;
        *(uint16_t *)&frame[offset] = htons(len);
        offset += 2;
    } else {
        frame[offset++] = 127;
        *(uint64_t *)&frame[offset] = htonll(len);
        offset += 8;
    }

    // Payload
    memcpy(&frame[offset], data, len);
    offset += len;

    // Send frame
    int bytes_sent = send(conn->socket, (char *)frame, offset, 0);
    return bytes_sent == offset;
}

