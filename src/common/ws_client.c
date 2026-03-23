/**
 * WebSocket Client Implementation
 */
#include "ws_client.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// OpenSSL for SSL/TLS support
#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
// Define sleep macro for Windows
#define sleep(seconds) Sleep((seconds) * 1000)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
// Define Windows equivalents for Linux
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define WSAGetLastError() errno
#define closesocket close
#endif

// WebSocket opcodes
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xA

// Default buffer sizes
#define DEFAULT_RECV_BUFFER_SIZE (64 * 1024)
#define DEFAULT_SEND_BUFFER_SIZE (64 * 1024)

// Forward declarations
static bool ws_client_handshake(WsClient *client);
static bool ws_client_send_frame(WsClient *client, uint8_t opcode, const void *data, size_t len, bool mask);
static int ws_client_recv_frame(WsClient *client, uint8_t *opcode, char **payload, size_t *payload_len);
static void* ws_client_event_loop(void *arg);

// Base64 encoding
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *input, int length, char *output) {
    int i, j;
    unsigned char three_bytes[3];
    int output_index = 0;

    for (i = 0; i < length; i += 3) {
        three_bytes[0] = (i < length) ? input[i] : 0;
        three_bytes[1] = (i + 1 < length) ? input[i + 1] : 0;
        three_bytes[2] = (i + 2 < length) ? input[i + 2] : 0;

        output[output_index++] = base64_chars[three_bytes[0] >> 2];
        output[output_index++] = base64_chars[((three_bytes[0] & 0x03) << 4) | (three_bytes[1] >> 4)];
        output[output_index++] = base64_chars[((three_bytes[1] & 0x0F) << 2) | (three_bytes[2] >> 6)];
        output[output_index++] = base64_chars[three_bytes[2] & 0x3F];
    }

    if (length % 3 == 1) {
        output[output_index - 2] = '=';
        output[output_index - 1] = '=';
    } else if (length % 3 == 2) {
        output[output_index - 1] = '=';
    }
    output[output_index] = '\0';
}

// SHA-1 implementation (from websocket.c, same as server)
static void sha1(const unsigned char *input, int length, unsigned char *output) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    unsigned char *padded = (unsigned char *)malloc(length + 128);
    if (!padded) return;
    
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

    for (int i = 0; i < padded_length; i += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = (padded[i + j * 4] << 24) | (padded[i + j * 4 + 1] << 16) | 
                   (padded[i + j * 4 + 2] << 8) | padded[i + j * 4 + 3];
        }
        for (int j = 16; j < 80; j++) {
            w[j] = (w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16]);
            w[j] = (w[j] << 1) | (w[j] >> 31);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (j < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[j];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    free(padded);
    
    for (int i = 0; i < 20; i++) {
        output[i] = ((uint32_t[]){h0, h1, h2, h3, h4}[i / 4] >> (24 - (i % 4) * 8)) & 0xFF;
    }
}

// Random string generator for WebSocket key
static void generate_random_key(char *key, size_t len) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    srand((unsigned int)time(NULL) ^ (unsigned int)(size_t)key);
    for (size_t i = 0; i < len; i++) {
        key[i] = chars[rand() % (sizeof(chars) - 1)];
    }
    key[len] = '\0';
}

// Parse WebSocket URL
char* ws_client_parse_url(const char *url, char **host, char **path, int *port, bool *use_ssl) {
    if (!url) return NULL;
    
    *use_ssl = false;
    *port = 80;
    *host = NULL;
    *path = NULL;
    
    const char *p = url;
    
    // Parse scheme
    if (strncmp(p, "wss://", 6) == 0) {
        *use_ssl = true;
        *port = 443;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    } else {
        return NULL;
    }
    
    // Parse host
    const char *host_start = p;
    const char *host_end = strchr(p, '/');
    const char *port_sep = strchr(p, ':');
    
    if (port_sep && (!host_end || port_sep < host_end)) {
        // Port specified
        size_t host_len = port_sep - host_start;
        *host = (char *)malloc(host_len + 1);
        strncpy(*host, host_start, host_len);
        (*host)[host_len] = '\0';
        *port = atoi(port_sep + 1);
        p = host_end ? host_end : (port_sep + strlen(port_sep + 1) + 1);
    } else if (host_end) {
        size_t host_len = host_end - host_start;
        *host = (char *)malloc(host_len + 1);
        strncpy(*host, host_start, host_len);
        (*host)[host_len] = '\0';
        p = host_end;
    } else {
        *host = strdup(host_start);
        p = url + strlen(url);
    }
    
    // Parse path
    if (*p) {
        *path = strdup(p);
    } else {
        *path = strdup("/");
    }
    
    // Return full URL for logging
    return strdup(url);
}

// Set socket non-blocking
static bool set_socket_nonblocking(SOCKET sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

// Set socket blocking
static bool set_socket_blocking(SOCKET sock) {
#ifdef _WIN32
    u_long mode = 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) != -1;
#endif
}

// Create WebSocket client
WsClient* ws_client_create(const WsClientConfig *config) {
    if (!config || !config->url) return NULL;
    
    WsClient *client = (WsClient *)calloc(1, sizeof(WsClient));
    if (!client) return NULL;
    
    // Parse URL
    client->url = ws_client_parse_url(config->url, &client->host, &client->path, 
                                       &client->port, &client->use_ssl);
    if (!client->url || !client->host) {
        free(client);
        return NULL;
    }
    
    // Set configuration
    client->ping_interval_sec = config->ping_interval_sec > 0 ? config->ping_interval_sec : 30;
    client->reconnect_interval_sec = config->reconnect_interval_sec > 0 ? config->reconnect_interval_sec : 5;
    client->max_reconnect_count = config->max_reconnect_count;
    client->connect_timeout_sec = config->connect_timeout_sec > 0 ? config->connect_timeout_sec : 10;
    
    // Allocate buffers
    size_t recv_size = config->recv_buffer_size > 0 ? config->recv_buffer_size : DEFAULT_RECV_BUFFER_SIZE;
    client->recv_buffer = (char *)malloc(recv_size);
    client->recv_buffer_size = recv_size;
    
    client->send_buffer = (char *)malloc(DEFAULT_SEND_BUFFER_SIZE);
    client->send_buffer_size = DEFAULT_SEND_BUFFER_SIZE;
    
    if (!client->recv_buffer || !client->send_buffer) {
        ws_client_destroy(client);
        return NULL;
    }
    
    client->socket = INVALID_SOCKET;
    client->state = WS_CLIENT_DISCONNECTED;
    client->running = false;
    
    log_info("[WSClient] Created client for %s:%d", client->host, client->port);
    return client;
}

// Destroy WebSocket client
void ws_client_destroy(WsClient *client) {
    if (!client) return;
    
    ws_client_stop(client);
    ws_client_disconnect(client);
    
    free(client->url);
    free(client->host);
    free(client->path);
    free(client->recv_buffer);
    free(client->send_buffer);
    free(client);
}

// Perform WebSocket handshake
static bool ws_client_handshake(WsClient *client) {
    if (client->socket == INVALID_SOCKET) return false;
    
    // Generate WebSocket key
    char ws_key[32];
    generate_random_key(ws_key, 16);
    
    // Base64 encode the key
    char ws_key_b64[32];
    base64_encode((unsigned char *)ws_key, 16, ws_key_b64);
    
    // Build handshake request
    char request[4096];
    int len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s"  // Extra headers
        "\r\n",
        client->path,
        client->host, client->port,
        ws_key_b64,
        ""   // Extra headers placeholder
    );
    
    // Send handshake request
    int sent;
#ifdef HAVE_OPENSSL
    if (client->ssl) {
        sent = SSL_write((SSL*)client->ssl, request, len);
    } else
#endif
    {
        sent = send(client->socket, request, len, 0);
    }
    if (sent != len) {
        log_error("[WSClient] Failed to send handshake request");
        return false;
    }
    
    // Receive handshake response
    char response[4096];
    int recv_len;
#ifdef HAVE_OPENSSL
    if (client->ssl) {
        recv_len = SSL_read((SSL*)client->ssl, response, sizeof(response) - 1);
    } else
#endif
    {
        recv_len = recv(client->socket, response, sizeof(response) - 1, 0);
    }
    if (recv_len <= 0) {
        log_error("[WSClient] Failed to receive handshake response");
        return false;
    }
    response[recv_len] = '\0';
    
    // Verify response
    if (strstr(response, "HTTP/1.1 101") == NULL) {
        log_error("[WSClient] Handshake failed: %s", response);
        return false;
    }
    
    // Verify Sec-WebSocket-Accept
    char expected_accept[64];
    char combined[128];
    snprintf(combined, sizeof(combined), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", ws_key_b64);
    
    unsigned char hash[20];
    sha1((unsigned char *)combined, strlen(combined), hash);
    base64_encode(hash, 20, expected_accept);
    
    // Find Sec-WebSocket-Accept in response
    char *accept_start = strstr(response, "Sec-WebSocket-Accept: ");
    if (!accept_start) {
        log_error("[WSClient] Missing Sec-WebSocket-Accept header");
        return false;
    }
    
    accept_start += 21;  // Skip header name
    // Skip leading spaces
    while (*accept_start == ' ') accept_start++;
    char *accept_end = strstr(accept_start, "\r\n");
    if (!accept_end) return false;
    
    *accept_end = '\0';
    log_debug("[WSClient] Expected accept: %s", expected_accept);
    log_debug("[WSClient] Got accept: %s", accept_start);
    if (strcmp(accept_start, expected_accept) != 0) {
        log_error("[WSClient] Invalid Sec-WebSocket-Accept");
        return false;
    }
    
    log_info("[WSClient] Handshake completed successfully");
    return true;
}

// Connect to server
bool ws_client_connect(WsClient *client) {
    if (!client) return false;
    
    if (client->state == WS_CLIENT_CONNECTED) {
        return true;
    }
    
    client->state = WS_CLIENT_CONNECTING;
    if (client->on_state_change) {
        client->on_state_change(client, WS_CLIENT_CONNECTING, client->user_data);
    }
    
    // Create socket
    client->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->socket == INVALID_SOCKET) {
        log_error("[WSClient] Failed to create socket: %d", WSAGetLastError());
        client->state = WS_CLIENT_ERROR;
        return false;
    }
    
    // Resolve host
    struct hostent *he = gethostbyname(client->host);
    if (!he) {
        log_error("[WSClient] Failed to resolve host: %s", client->host);
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
        client->state = WS_CLIENT_ERROR;
        return false;
    }
    
    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(client->port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    // Set non-blocking for connect timeout
    set_socket_nonblocking(client->socket);
    
    int result = connect(client->socket, (struct sockaddr *)&addr, sizeof(addr));
    
#ifdef _WIN32
    if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
#else
    if (result < 0 && errno != EINPROGRESS) {
#endif
        log_error("[WSClient] Connect failed: %d", WSAGetLastError());
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
        client->state = WS_CLIENT_ERROR;
        return false;
    }
    
    // Wait for connection with timeout
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(client->socket, &write_fds);
    
    struct timeval timeout;
    timeout.tv_sec = client->connect_timeout_sec;
    timeout.tv_usec = 0;
    
    result = select(client->socket + 1, NULL, &write_fds, NULL, &timeout);
    if (result <= 0) {
        log_error("[WSClient] Connect timeout or error");
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
        client->state = WS_CLIENT_ERROR;
        return false;
    }
    
    // Check for connection errors
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(client->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &len) < 0 || error != 0) {
        log_error("[WSClient] Connect failed: %d", error);
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
        client->state = WS_CLIENT_ERROR;
        return false;
    }
    
    // Set blocking again for handshake
    set_socket_blocking(client->socket);
    
#ifdef HAVE_OPENSSL
    // Setup SSL if needed
    if (client->use_ssl) {
        SSL_library_init();
        SSL_load_error_strings();
        
        SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            log_error("[WSClient] Failed to create SSL context");
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
            client->state = WS_CLIENT_ERROR;
            return false;
        }
        
        SSL *ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            log_error("[WSClient] Failed to create SSL structure");
            SSL_CTX_free(ssl_ctx);
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
            client->state = WS_CLIENT_ERROR;
            return false;
        }
        
        SSL_set_fd(ssl, client->socket);
        
        // Set hostname for SNI
        SSL_set_tlsext_host_name(ssl, client->host);
        
        // Perform SSL handshake
        int ssl_ret = SSL_connect(ssl);
        if (ssl_ret != 1) {
            unsigned long err = ERR_get_error();
            log_error("[WSClient] SSL handshake failed: %s", ERR_error_string(err, NULL));
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
            client->state = WS_CLIENT_ERROR;
            return false;
        }
        
        client->ssl = ssl;
        client->ssl_ctx = ssl_ctx;
        log_info("[WSClient] SSL connection established");
    }
#endif
    
    // Perform WebSocket handshake
    if (!ws_client_handshake(client)) {
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
        client->state = WS_CLIENT_ERROR;
        return false;
    }
    
    // Set non-blocking for event loop
    set_socket_nonblocking(client->socket);
    
    client->state = WS_CLIENT_CONNECTED;
    client->recv_buffer_len = 0;
    
    if (client->on_state_change) {
        client->on_state_change(client, WS_CLIENT_CONNECTED, client->user_data);
    }
    
    log_info("[WSClient] Connected to %s:%d", client->host, client->port);
    return true;
}

// Disconnect
void ws_client_disconnect(WsClient *client) {
    if (!client) return;
    
    if (client->socket != INVALID_SOCKET) {
        // Send close frame
        ws_client_send_frame(client, WS_OPCODE_CLOSE, NULL, 0, true);
        
#ifdef HAVE_OPENSSL
        // Cleanup SSL
        if (client->ssl) {
            SSL_shutdown((SSL*)client->ssl);
            SSL_free((SSL*)client->ssl);
            client->ssl = NULL;
        }
        if (client->ssl_ctx) {
            SSL_CTX_free((SSL_CTX*)client->ssl_ctx);
            client->ssl_ctx = NULL;
        }
#endif
        
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
    }
    
    client->state = WS_CLIENT_DISCONNECTED;
    if (client->on_state_change) {
        client->on_state_change(client, WS_CLIENT_DISCONNECTED, client->user_data);
    }
    
    log_info("[WSClient] Disconnected");
}

// Send WebSocket frame
static bool ws_client_send_frame(WsClient *client, uint8_t opcode, const void *data, size_t len, bool mask) {
    if (client->socket == INVALID_SOCKET) return false;
    if (len > client->send_buffer_size - 14) return false;  // Max frame overhead
    
    uint8_t *frame = (uint8_t *)client->send_buffer;
    size_t offset = 0;
    
    // First byte: FIN and opcode
    frame[offset++] = 0x80 | (opcode & 0x0F);
    
    // Second byte: Mask and payload length
    if (mask) {
        frame[offset] = 0x80;
    }
    
    if (len <= 125) {
        frame[offset++] |= (uint8_t)len;
    } else if (len <= 65535) {
        frame[offset++] |= 126;
        frame[offset++] = (len >> 8) & 0xFF;
        frame[offset++] = len & 0xFF;
    } else {
        frame[offset++] |= 127;
        for (int i = 7; i >= 0; i--) {
            frame[offset++] = (len >> (i * 8)) & 0xFF;
        }
    }
    
    // Masking key
    uint8_t mask_key[4] = {0};
    if (mask) {
        srand((unsigned int)time(NULL));
        for (int i = 0; i < 4; i++) {
            mask_key[i] = rand() & 0xFF;
        }
        memcpy(frame + offset, mask_key, 4);
        offset += 4;
    }
    
    // Payload
    if (data && len > 0) {
        if (mask) {
            const uint8_t *src = (const uint8_t *)data;
            for (size_t i = 0; i < len; i++) {
                frame[offset + i] = src[i] ^ mask_key[i % 4];
            }
        } else {
            memcpy(frame + offset, data, len);
        }
        offset += len;
    }
    
    // Send frame
    ssize_t sent;
#ifdef HAVE_OPENSSL
    if (client->ssl) {
        sent = SSL_write((SSL*)client->ssl, frame, offset);
    } else
#endif
    {
        sent = send(client->socket, (char *)frame, offset, 0);
    }
    return sent == (ssize_t)offset;
}

// Receive and parse WebSocket frame
static int ws_client_recv_frame(WsClient *client, uint8_t *opcode, char **payload, size_t *payload_len) {
    if (client->socket == INVALID_SOCKET) return -1;
    
    // Try to receive more data
    size_t space = client->recv_buffer_size - client->recv_buffer_len;
    if (space > 0) {
        ssize_t recv_len;
#ifdef HAVE_OPENSSL
        if (client->ssl) {
            recv_len = SSL_read((SSL*)client->ssl, 
                               client->recv_buffer + client->recv_buffer_len, 
                               space);
        } else
#endif
        {
            recv_len = recv(client->socket, 
                           client->recv_buffer + client->recv_buffer_len, 
                           space, 0);
        }
        if (recv_len > 0) {
            client->recv_buffer_len += recv_len;
        } else if (recv_len == 0) {
            return -1;  // Connection closed
        }
#ifdef _WIN32
        else if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
        else if (errno != EWOULDBLOCK && errno != EAGAIN) {
#endif
            return -1;  // Error
        }
    }
    
    // Check if we have enough data for frame header
    if (client->recv_buffer_len < 2) {
        return 0;  // Need more data
    }
    
    uint8_t *buffer = (uint8_t *)client->recv_buffer;
    size_t offset = 0;
    
    // First byte: FIN and opcode
    bool fin = (buffer[offset] & 0x80) != 0;
    *opcode = buffer[offset++] & 0x0F;
    
    // Second byte: Mask and payload length
    bool masked = (buffer[offset] & 0x80) != 0;
    uint64_t len = buffer[offset++] & 0x7F;
    
    if (len == 126) {
        if (client->recv_buffer_len < offset + 2) return 0;
        len = ((uint16_t)buffer[offset] << 8) | buffer[offset + 1];
        offset += 2;
    } else if (len == 127) {
        if (client->recv_buffer_len < offset + 8) return 0;
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | buffer[offset + i];
        }
        offset += 8;
    }
    
    // Masking key (server shouldn't mask, but handle it anyway)
    uint8_t mask_key[4] = {0};
    if (masked) {
        if (client->recv_buffer_len < offset + 4) return 0;
        memcpy(mask_key, buffer + offset, 4);
        offset += 4;
    }
    
    // Check if we have full payload
    if (client->recv_buffer_len < offset + len) {
        return 0;  // Need more data
    }
    
    // Allocate and copy payload
    *payload = (char *)malloc(len + 1);
    if (!*payload) return -1;
    
    if (masked) {
        for (uint64_t i = 0; i < len; i++) {
            (*payload)[i] = buffer[offset + i] ^ mask_key[i % 4];
        }
    } else {
        memcpy(*payload, buffer + offset, len);
    }
    (*payload)[len] = '\0';
    *payload_len = len;
    
    // Shift buffer
    size_t consumed = offset + len;
    client->recv_buffer_len -= consumed;
    if (client->recv_buffer_len > 0) {
        memmove(client->recv_buffer, client->recv_buffer + consumed, client->recv_buffer_len);
    }
    
    (void)fin;  // Not used for now
    return 1;
}

// Event loop thread
static void* ws_client_event_loop(void *arg) {
    WsClient *client = (WsClient *)arg;
    
    time_t last_ping = time(NULL);
    
    while (client->running) {
        if (client->state != WS_CLIENT_CONNECTED) {
            // Try to reconnect
            sleep(1);
            continue;
        }
        
        bool has_data = false;
        
#ifdef HAVE_OPENSSL
        // For SSL, check SSL_pending() first as select() doesn't see SSL buffer
        if (client->ssl && SSL_pending((SSL*)client->ssl) > 0) {
            has_data = true;
        }
#endif
        
        if (!has_data) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client->socket, &read_fds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            int result = select(client->socket + 1, &read_fds, NULL, NULL, &timeout);
            
            if (!client->running) break;
            
            if (result > 0) {
                has_data = true;
            }
        }
        
        if (has_data) {
            // Receive and process frames
            uint8_t opcode;
            char *payload = NULL;
            size_t payload_len = 0;
            
            int recv_result = ws_client_recv_frame(client, &opcode, &payload, &payload_len);
            
            if (recv_result < 0) {
                log_error("[WSClient] Connection lost");
                client->state = WS_CLIENT_ERROR;
                if (client->on_state_change) {
                    client->on_state_change(client, WS_CLIENT_ERROR, client->user_data);
                }
                continue;
            }
            
            if (recv_result > 0 && payload) {
                switch (opcode) {
                    case WS_OPCODE_TEXT:
                    case WS_OPCODE_BINARY:
                        if (client->on_message) {
                            bool continue_receiving = client->on_message(client, payload, payload_len, client->user_data);
                            if (!continue_receiving) {
                                free(payload);
                                client->running = false;
                                break;
                            }
                        }
                        break;
                        
                    case WS_OPCODE_PING:
                        ws_client_send_pong(client, payload, payload_len);
                        break;
                        
                    case WS_OPCODE_PONG:
                        log_debug("[WSClient] Received PONG");
                        break;
                        
                    case WS_OPCODE_CLOSE:
                        log_info("[WSClient] Received CLOSE frame");
                        client->state = WS_CLIENT_CLOSING;
                        client->running = false;
                        break;
                }
                free(payload);
            }
        }
        
        // Send periodic ping
        time_t now = time(NULL);
        if (client->ping_interval_sec > 0 && 
            now - last_ping >= client->ping_interval_sec) {
            ws_client_send_ping(client, NULL, 0);
            last_ping = now;
        }
    }
    
    return NULL;
}

// Start event loop
bool ws_client_start(WsClient *client) {
    if (!client || client->running) return false;
    
    if (!ws_client_connect(client)) {
        return false;
    }
    
    client->running = true;
    
#ifndef _WIN32
    pthread_t thread;
    if (pthread_create(&thread, NULL, ws_client_event_loop, client) != 0) {
        log_error("[WSClient] Failed to create event loop thread");
        client->running = false;
        return false;
    }
    client->thread = (void *)(uintptr_t)thread;
#else
    // Windows: Create a thread for the event loop
    HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ws_client_event_loop, client, 0, NULL);
    if (thread == NULL) {
        log_error("[WSClient] Failed to create event loop thread");
        client->running = false;
        return false;
    }
    client->thread = thread;
#endif
    
    log_info("[WSClient] Event loop started");
    return true;
}

// Stop event loop
void ws_client_stop(WsClient *client) {
    if (!client) return;
    
    client->running = false;
    
#ifndef _WIN32
    if (client->thread) {
        pthread_t thread = (pthread_t)(uintptr_t)client->thread;
        pthread_join(thread, NULL);
        client->thread = NULL;
    }
#else
    if (client->thread) {
        WaitForSingleObject((HANDLE)client->thread, INFINITE);
        CloseHandle((HANDLE)client->thread);
        client->thread = NULL;
    }
#endif
    
    ws_client_disconnect(client);
}

// Send text message
bool ws_client_send_text(WsClient *client, const char *text) {
    return ws_client_send_frame(client, WS_OPCODE_TEXT, text, strlen(text), true);
}

// Send binary message
bool ws_client_send_binary(WsClient *client, const void *data, size_t len) {
    return ws_client_send_frame(client, WS_OPCODE_BINARY, data, len, true);
}

// Send ping
bool ws_client_send_ping(WsClient *client, const void *data, size_t len) {
    return ws_client_send_frame(client, WS_OPCODE_PING, data, len, true);
}

// Send pong
bool ws_client_send_pong(WsClient *client, const void *data, size_t len) {
    return ws_client_send_frame(client, WS_OPCODE_PONG, data, len, true);
}

// Get state
WsClientState ws_client_get_state(WsClient *client) {
    return client ? client->state : WS_CLIENT_ERROR;
}

// Check connected
bool ws_client_is_connected(WsClient *client) {
    return client && client->state == WS_CLIENT_CONNECTED;
}

// Set message callback
void ws_client_set_message_callback(WsClient *client, 
                                     WsClientMessageCallback callback, 
                                     void *user_data) {
    if (client) {
        client->on_message = callback;
        client->user_data = user_data;
    }
}

// Set state callback
void ws_client_set_state_callback(WsClient *client,
                                   WsClientStateCallback callback,
                                   void *user_data) {
    if (client) {
        client->on_state_change = callback;
        // Note: user_data should be set via message callback
        (void)user_data;
    }
}
