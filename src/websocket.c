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

// 64-bit byte order conversion functions
static uint64_t ntohll(uint64_t value);
static uint64_t htonll(uint64_t value);

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

// Base64 encoding function
static void base64_encode(const unsigned char *input, int length, char *output) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j;
    unsigned char three_bytes[3];
    int output_index = 0;

    for (i = 0; i < length; i += 3) {
        // Read up to 3 bytes
        three_bytes[0] = (i < length) ? input[i] : 0;
        three_bytes[1] = (i + 1 < length) ? input[i + 1] : 0;
        three_bytes[2] = (i + 2 < length) ? input[i + 2] : 0;

        // Encode into 4 bytes
        output[output_index++] = base64_chars[three_bytes[0] >> 2];
        output[output_index++] = base64_chars[((three_bytes[0] & 0x03) << 4) | (three_bytes[1] >> 4)];
        output[output_index++] = base64_chars[((three_bytes[1] & 0x0F) << 2) | (three_bytes[2] >> 6)];
        output[output_index++] = base64_chars[three_bytes[2] & 0x3F];
    }

    // Pad with '=' if necessary
    if (length % 3 == 1) {
        output[output_index - 2] = '=';
        output[output_index - 1] = '=';
    } else if (length % 3 == 2) {
        output[output_index - 1] = '=';
    }

    output[output_index] = '\0';
}

// Simple SHA-1 implementation (from RFC 3174)
static void sha1(const unsigned char *input, int length, unsigned char *output) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // Pad the input
    unsigned char padded[1024];
    memcpy(padded, input, length);
    padded[length] = 0x80;
    int padded_length = length + 1;
    while (padded_length % 64 != 56) {
        padded[padded_length++] = 0x00;
    }
    uint64_t bit_length = (uint64_t)length * 8;
    for (int i = 0; i < 8; i++) {
        padded[padded_length++] = (bit_length >> (56 - i * 8)) & 0xFF;
    }

    // Process each 512-bit block
    for (int i = 0; i < padded_length; i += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = (padded[i + j * 4] << 24) | (padded[i + j * 4 + 1] << 16) | (padded[i + j * 4 + 2] << 8) | padded[i + j * 4 + 3];
        }
        for (int j = 16; j < 80; j++) {
            w[j] = (w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16]);
            w[j] = (w[j] << 1) | (w[j] >> 31);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (j < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (j < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[j];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    // Output the hash
    output[0] = (h0 >> 24) & 0xFF;
    output[1] = (h0 >> 16) & 0xFF;
    output[2] = (h0 >> 8) & 0xFF;
    output[3] = h0 & 0xFF;
    output[4] = (h1 >> 24) & 0xFF;
    output[5] = (h1 >> 16) & 0xFF;
    output[6] = (h1 >> 8) & 0xFF;
    output[7] = h1 & 0xFF;
    output[8] = (h2 >> 24) & 0xFF;
    output[9] = (h2 >> 16) & 0xFF;
    output[10] = (h2 >> 8) & 0xFF;
    output[11] = h2 & 0xFF;
    output[12] = (h3 >> 24) & 0xFF;
    output[13] = (h3 >> 16) & 0xFF;
    output[14] = (h3 >> 8) & 0xFF;
    output[15] = h3 & 0xFF;
    output[16] = (h4 >> 24) & 0xFF;
    output[17] = (h4 >> 16) & 0xFF;
    output[18] = (h4 >> 8) & 0xFF;
    output[19] = h4 & 0xFF;
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

    // Generate response key
    char combined[512];
    snprintf(combined, sizeof(combined), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    
    unsigned char hash[20];
    sha1((unsigned char *)combined, strlen(combined), hash);
    
    char base64_hash[30];
    base64_encode(hash, 20, base64_hash);

    // Generate response
    char response[512];
    snprintf(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\n" 
             "Upgrade: websocket\r\n" 
             "Connection: Upgrade\r\n" 
             "Sec-WebSocket-Accept: %s\r\n" 
             "\r\n", base64_hash);

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

// Helper function for 64-bit byte order conversion
static uint64_t ntohll(uint64_t value) {
    if (is_little_endian()) {
        return ((uint64_t)ntohl((uint32_t)(value >> 32)) | ((uint64_t)ntohl((uint32_t)value) << 32));
    }
    return value;
}

static uint64_t htonll(uint64_t value) {
    if (is_little_endian()) {
        return ((uint64_t)htonl((uint32_t)(value >> 32)) | ((uint64_t)htonl((uint32_t)value) << 32));
    }
    return value;
}
