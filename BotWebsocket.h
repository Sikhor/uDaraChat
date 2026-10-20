/*
MIT License

Copyright (c) 2020 Meng Rao <raomeng1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <string>
#include <string.h>
#include <limits>
#include <memory>
#include <stdio.h>
#include <errno.h>

#ifdef DARA_WEBSOCKET_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <sys/time.h>
#endif

namespace websocket {

template<uint32_t RecvBufSize>
class SocketTcpConnection
{
public:
  ~SocketTcpConnection() { close("destruct"); }

  const char* getLastError() { return last_error_; };

  bool isConnected() { return fd_ >= 0; }

  bool connect(const char* server_host, uint16_t server_port) {
    server_host_ = server_host;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(server_port);
    const int result = getaddrinfo(server_host, service.c_str(), &hints, &addresses);
    if (result != 0) {
      char detail[768];
      snprintf(detail, sizeof(detail), "DNS lookup failed for %s: %s", server_host, gai_strerror(result));
      saveError(detail, false);
      return false;
    }
    int fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
      fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
      if (fd < 0) {
        saveError("TCP socket creation failed", true);
        continue;
      }
      if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
      saveError("TCP connection failed (check server address, port, listener and firewall)", true);
      ::close(fd);
      fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) return false;
    return open(fd);
  }

  bool getPeername(struct sockaddr_in& addr) {
    socklen_t addr_len = sizeof(addr);
    return ::getpeername(fd_, (struct sockaddr*)&addr, &addr_len) == 0;
  }

  void close(const char* reason, bool check_errno = false) {
    if (fd_ >= 0) {
      saveError(reason, check_errno);
#ifdef DARA_WEBSOCKET_TLS
      SSL_free(tls_);
      tls_ = nullptr;
      SSL_CTX_free(tls_context_);
      tls_context_ = nullptr;
#endif
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool write(const uint8_t* data, uint32_t size, bool more = false) {
    // Empty WebSocket control frames have a header but no payload. OpenSSL
    // does not treat a zero-byte SSL_write as a successful application write.
    if (size == 0) return true;
    if (!isConnected()) return false;
    int flags = MSG_NOSIGNAL;
    if (more) flags |= MSG_MORE;
    do {
#ifdef DARA_WEBSOCKET_TLS
      ERR_clear_error();
      errno = 0;
      int sent = SSL_write(tls_, data, static_cast<int>(size));
      if (sent <= 0) {
        const int saved_errno = errno;
        int error = SSL_get_error(tls_, sent);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) continue;
        char detail[768];
        char openssl_error[256] = "no OpenSSL detail available";
        const unsigned long code = ERR_get_error();
        if (code) ERR_error_string_n(code, openssl_error, sizeof(openssl_error));
        snprintf(detail, sizeof(detail), "TLS send failed (SSL error %d): %s%s%s",
                 error, openssl_error, saved_errno ? "; system error: " : "",
                 saved_errno ? strerror(saved_errno) : "");
        close(detail);
        return false;
      }
#else
      int sent = ::send(fd_, data, size, flags);
#endif
      if (sent < 0) {
        if (errno != EAGAIN) {
          close("send error", true);
          return false;
        }
        continue;
      }
      data += sent;
      size -= sent;
    } while (size != 0);
    return true;
  }

  template<typename Handler>
  bool read(Handler handler) {
#ifdef DARA_WEBSOCKET_TLS
    ERR_clear_error();
    errno = 0;
    int ret = SSL_read(tls_, recvbuf_ + tail_, static_cast<int>(RecvBufSize - tail_));
    if (ret <= 0) {
      const int saved_errno = errno;
      int error = SSL_get_error(tls_, ret);
      if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return false;
      if (error == SSL_ERROR_ZERO_RETURN) {
        close("TLS peer closed the connection (close_notify) without a WebSocket close frame");
      } else {
        char detail[768];
        char openssl_error[256] = "no OpenSSL detail available";
        const unsigned long code = ERR_get_error();
        if (code) ERR_error_string_n(code, openssl_error, sizeof(openssl_error));
        snprintf(detail, sizeof(detail), "TLS read failed (SSL error %d): %s%s%s; check server logs for timeout, restart or network closure",
                 error, openssl_error, saved_errno ? "; system error: " : "",
                 saved_errno ? strerror(saved_errno) : "");
        close(detail);
      }
      return false;
    }
#else
    int ret = ::read(fd_, recvbuf_ + tail_, RecvBufSize - tail_);
#endif
    if (ret <= 0) {
      if (ret < 0 && errno == EAGAIN) return false;
      if (ret < 0) {
        close("read error", true);
      }
      else {
        close("remote close");
      }
      return false;
    }
    tail_ += ret;

    uint32_t remaining = handler(recvbuf_ + head_, tail_ - head_);
    if (remaining == 0) {
      head_ = tail_ = 0;

    }
    else {
      head_ = tail_ - remaining;
      if (head_ >= RecvBufSize / 2) {
        memcpy(recvbuf_, recvbuf_ + head_, remaining);
        head_ = 0;
        tail_ = remaining;
      }
      else if (tail_ == RecvBufSize) {
        close("recv buf full");
      }
    }
    return true;
  }

protected:
  template<uint32_t>
  friend class SocketTcpServer;

  bool open(int fd) {
    fd_ = fd;
    head_ = tail_ = 0;
#ifdef DARA_WEBSOCKET_TLS
    // Send SNI and verify the configured DNS hostname, independent of its resolved IP.
    timeval timeout{3, 0};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    ERR_clear_error();
    tls_context_ = SSL_CTX_new(TLS_client_method());
    if (!tls_context_ || !SSL_CTX_set_default_verify_paths(tls_context_)) {
      closeTlsError("TLS trust store unavailable; check SSL_CERT_FILE / SSL_CERT_DIR");
      return false;
    }
    SSL_CTX_set_verify(tls_context_, SSL_VERIFY_PEER, nullptr);
    tls_ = SSL_new(tls_context_);
    if (!tls_ || !SSL_set_tlsext_host_name(tls_, server_host_.c_str()) ||
        !SSL_set1_host(tls_, server_host_.c_str()) ||
        !SSL_set_fd(tls_, fd_)) {
      closeTlsError("TLS setup failed");
      return false;
    }
    errno = 0;
    const int handshake = SSL_connect(tls_);
    if (handshake != 1) {
      const int saved_errno = errno;
      const int tls_error = SSL_get_error(tls_, handshake);
      const long verification = SSL_get_verify_result(tls_);
      char detail[768];
      if (verification != X509_V_OK) {
        snprintf(detail, sizeof(detail),
                 "TLS certificate verification failed for hostname %s: %s. Certificate must include this hostname in its SAN and chain to a trusted CA; for a private CA set SSL_CERT_FILE.",
                 server_host_.c_str(), X509_verify_cert_error_string(verification));
      } else if (tls_error == SSL_ERROR_SYSCALL && saved_errno != 0) {
        snprintf(detail, sizeof(detail), "TLS handshake I/O failed: %s. Check that the port serves WSS/TLS and the server is responding.", strerror(saved_errno));
      } else if (tls_error == SSL_ERROR_ZERO_RETURN || (tls_error == SSL_ERROR_SYSCALL && handshake == 0)) {
        snprintf(detail, sizeof(detail), "TLS handshake: server closed the connection before completion. Check server TLS configuration and logs.");
      } else {
        char openssl_error[256] = "no OpenSSL detail available";
        const unsigned long code = ERR_get_error();
        if (code) ERR_error_string_n(code, openssl_error, sizeof(openssl_error));
        snprintf(detail, sizeof(detail), "TLS handshake failed (SSL error %d): %s. Check that this port serves WSS/TLS and inspect server logs.", tls_error, openssl_error);
      }
      close(detail);
      return false;
    }
#endif

    int flags = fcntl(fd_, F_GETFL, 0);
    if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      close("fcntl O_NONBLOCK error", true);
      return false;
    }

    int yes = 1;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
      close("setsockopt TCP_NODELAY error", true);
      return false;
    }

    return true;
  }

#ifdef DARA_WEBSOCKET_TLS
  void closeTlsError(const char* stage) {
    char detail[768];
    char openssl_error[256] = "no OpenSSL detail available";
    const unsigned long code = ERR_get_error();
    if (code) ERR_error_string_n(code, openssl_error, sizeof(openssl_error));
    snprintf(detail, sizeof(detail), "%s: %s", stage, openssl_error);
    close(detail);
  }
#endif

  void saveError(const char* msg, bool check_errno) {
    snprintf(last_error_, sizeof(last_error_), "%s %s", msg, check_errno ? (const char*)strerror(errno) : "");
  }

#ifdef DARA_WEBSOCKET_TLS
  SSL_CTX* tls_context_ = nullptr;
  SSL* tls_ = nullptr;
#endif
  std::string server_host_;
  int fd_ = -1;
  uint32_t head_;
  uint32_t tail_;
  char recvbuf_[RecvBufSize];
  char last_error_[1024] = "";
};

template<uint32_t RecvBufSize = 4096>
class SocketTcpServer
{
public:
  using TcpConnection = SocketTcpConnection<RecvBufSize>;

  bool init(const char* interface, const char* server_ip, uint16_t server_port) {
    listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd_ < 0) {
      saveError("socket error");
      return false;
    }

    int flags = fcntl(listenfd_, F_GETFL, 0);
    if (fcntl(listenfd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      close("fcntl O_NONBLOCK error");
      return false;
    }

    int yes = 1;
    if (setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
      close("setsockopt SO_REUSEADDR error");
      return false;
    }

    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    inet_pton(AF_INET, server_ip, &(local_addr.sin_addr));
    local_addr.sin_port = htons(server_port);
    bzero(&(local_addr.sin_zero), 8);
    if (bind(listenfd_, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
      close("bind error");
      return false;
    }
    if (listen(listenfd_, 5) < 0) {
      close("listen error");
      return false;
    }

    return true;
  };

  void close(const char* reason) {
    if (listenfd_ >= 0) {
      saveError(reason);
      ::close(listenfd_);
      listenfd_ = -1;
    }
  }

  const char* getLastError() { return last_error_; };

  ~SocketTcpServer() { close("destruct"); }

  bool accept2(TcpConnection& conn) {
    struct sockaddr_in clientaddr;
    socklen_t addr_len = sizeof(clientaddr);
    int fd = ::accept(listenfd_, (struct sockaddr*)&(clientaddr), &addr_len);
    if (fd < 0) {
      return false;
    }
    if (!conn.open(fd)) {
      return false;
    }
    return true;
  }

private:
  void saveError(const char* msg) { snprintf(last_error_, sizeof(last_error_), "%s %s", msg, strerror(errno)); }

  int listenfd_ = -1;
  char last_error_[64] = "";
};

inline uint64_t getns() {
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static const uint8_t OPCODE_CONT = 0;
static const uint8_t OPCODE_TEXT = 1;
static const uint8_t OPCODE_BINARY = 2;
static const uint8_t OPCODE_CLOSE = 8;
static const uint8_t OPCODE_PING = 9;
static const uint8_t OPCODE_PONG = 10;

template<typename EventHandler, typename ConnUserData, bool RecvSegment, uint32_t RecvBufSize, bool SendMask>
class WSConnection
{
public:
  ConnUserData user_data;

  // get remote network address
  bool getPeername(struct sockaddr_in& addr) { return conn.getPeername(addr); }

  bool isConnected() { return conn.isConnected(); }

  // if sending a msg of multiple segments, only set fin to true for the last one
  void send(uint8_t opcode, const uint8_t* payload, uint32_t pl_len, bool fin = true) {
    uint8_t h[14];
    uint32_t h_len = 2;
    if (opcode >> 3) // if control
      fin = true;
    else {
      if (!send_fin) opcode = OPCODE_CONT;
      send_fin = fin;
    }
    h[0] = (opcode & 15) | ((uint8_t)fin << 7);
    h[1] = (uint8_t)SendMask << 7;
    if (pl_len < 126) {
      h[1] |= (uint8_t)pl_len;
    }
    else if (pl_len < 65536) {
      h[1] |= 126;
      *(uint16_t*)(h + 2) = htobe16(pl_len);
      h_len += 2;
    }
    else {
      h[1] |= 127;
      *(uint64_t*)(h + 2) = htobe64(pl_len);
      h_len += 8;
    }
    if (SendMask) { // for efficency and simplicity masking-key is always set to 0
      *(uint32_t*)(h + h_len) = 0;
      h_len += 4;
    }
    if (!conn.write(h, h_len, true)) return;
    if (pl_len != 0) conn.write(payload, pl_len, false);
  }

  // clean close the connection with optional status_code and reason
  void close(uint16_t status_code = 1005, const char* reason = "") {
    *(uint16_t*)close_reason = htobe16(status_code);
    uint32_t reason_len = snprintf((char*)close_reason + 2, sizeof(close_reason) - 2, "%s", reason);
    if (status_code != 1005) {
      send(OPCODE_CLOSE, close_reason, 2 + reason_len);
    }
    else
      send(OPCODE_CLOSE, nullptr, 0);
    conn.close("clean close");
  }

protected:
  template<typename, typename, bool, uint32_t, uint32_t>
  friend class WSServer;

  void init(uint64_t expire) {
    open = false;
    send_fin = true;
    *(uint16_t*)close_reason = htobe16(1006);
    close_reason[2] = 0;
    frame_size = 0;
    expire_time = expire;
  }

  uint32_t handleWSMsg(EventHandler* handler, uint8_t* data, uint32_t size) {
    // One TLS read can contain multiple WebSocket frames. Drain every complete
    // frame now: leaving a ping buffered until the next network read can cause
    // the server to time out while waiting for its pong.
    while (size) {
      const bool closing = (data[0] & 15) == OPCODE_CLOSE;
      const uint32_t remaining = handleOneWSMsg(handler, data, size);
      if (remaining == size) return size; // incomplete frame
      if (closing) return 0;
      data += size - remaining;
      size = remaining;
    }
    return 0;
  }

  uint32_t handleOneWSMsg(EventHandler* handler, uint8_t* data, uint32_t size) {
    if (size < 2) return size;
    const uint32_t extended = (data[1] & 127) == 126 ? 2 : ((data[1] & 127) == 127 ? 8 : 0);
    const uint32_t header = 2 + extended + ((data[1] & 128) ? 4 : 0);
    if (size < header) return size;
    const uint8_t* data_end = data + size;
    uint8_t opcode = data[0] & 15;
    bool beg = opcode != OPCODE_CONT, fin = data[0] >> 7; //, control = opcode >> 3;
    bool mask = data[1] >> 7;
    uint8_t mask_key[4];
    uint64_t pl_len = data[1] & 127;
    data += 2;
    if (pl_len == 126) {
      pl_len = be16toh(*(uint16_t*)data);
      data += 2;
    }
    else if (pl_len == 127) {
      pl_len = be64toh(*(uint64_t*)data) & ~(1ULL << 63);
      data += 8;
    }
    if (mask) {
      *(uint32_t*)mask_key = *(uint32_t*)data;
      data += 4;
    }
    if (data_end - data < (int64_t)pl_len) {
      if (size + (data + pl_len - data_end) > RecvBufSize) close(1009);
      return size;
    }
    if (mask) {
      for (uint64_t i = 0; i < pl_len; i++) data[i] ^= mask_key[i & 3];
    }
    if (RecvSegment || (beg && fin)) {
      if (opcode == OPCODE_CLOSE) {
        uint16_t status_code = 1005;
        char reason[128] = {0};
        if (pl_len >= 2) {
          status_code = be16toh(*(uint16_t*)data);
          uint64_t reason_len = std::min(sizeof(reason) - 1, pl_len - 2);
          memcpy(reason, data + 2, reason_len);
          reason[reason_len] = 0;
        }
        close(status_code, reason);
      }
      else {
#if __cplusplus >= 201703L
        if constexpr (RecvSegment) {
#else
        if (RecvSegment) {
#endif
          if (beg) recv_opcode = opcode;
          handler->onWSSegment(*this, recv_opcode, data, pl_len, frame_size, fin);
          if (fin)
            frame_size = 0;
          else
            frame_size += pl_len;
        }
        else
          handler->onWSMsg(*this, opcode, data, pl_len);
      }
    }
#if __cplusplus >= 201703L
    else if constexpr (!RecvSegment) {
#else
    else {
#endif
      if (frame_size + pl_len > RecvBufSize)
        close(1009);
      else {
        memcpy(frame + frame_size, data, pl_len);
        frame_size += pl_len;
        if (beg) recv_opcode = opcode;
        if (fin) {
          handler->onWSMsg(*this, recv_opcode, frame, frame_size);
          frame_size = 0;
        }
      }
    }
    return data_end - (data + pl_len);
  }

  void handleWSClose(EventHandler* handler) {
    uint16_t status_code = be16toh(*(uint16_t*)close_reason);
    const char* reason = (const char*)close_reason + 2;
    if (status_code == 1006) reason = conn.getLastError();
    handler->onWSClose(*this, status_code, reason);
  }

  bool open;
  bool send_fin;
  uint8_t recv_opcode;
  uint32_t frame_size;
  uint64_t expire_time;
  uint8_t frame[RecvSegment ? 0 : RecvBufSize];
  typename SocketTcpServer<RecvBufSize>::TcpConnection conn;
  uint8_t close_reason[128]; // first 2 bytes are status_code(big endian)
};

template<typename EventHandler, typename ConnUserData = char, bool RecvSegment = false, uint32_t RecvBufSize = 4096,
         typename ConnectionType = WSConnection<EventHandler, ConnUserData, RecvSegment, RecvBufSize, true>>
class WSClient : public ConnectionType
{
public:
  using Connection = ConnectionType;
  // using Connection = WSConnection<EventHandler, ConnUserData, RecvSegment, RecvBufSize, true>;

  const char* getLastError() { return this->conn.getLastError(); }

  // timeout: connect timeout in milliseconds, 0 means no limit
  // if failed, call getLastError() for the reason
  bool wsConnect(uint64_t timeout, const char* server_ip, uint16_t server_port, const char* request_uri,
                 const char* host, const char* origin = nullptr, const char* protocol = nullptr,
                 const char* extensions = nullptr, char* resp_protocol = nullptr, uint32_t resp_protocol_size = 0,
                 char* resp_extensions = nullptr, uint32_t resp_extensions_size = 0) {
    uint64_t now = getns();
    uint64_t expire = timeout > 0 ? now + timeout * 1000000 : std::numeric_limits<uint64_t>::max();
    if (!this->conn.connect(server_ip, server_port)) return false;
    if (getns() > expire) {
      this->conn.close("Connection timeout: TCP/TLS setup exceeded the WebSocket connection deadline");
      return false;
    }
    this->init(expire);
    char req[2048];
    uint32_t req_len =
      snprintf(req, sizeof(req),
               "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
               "dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n",
               request_uri, host);
    if (origin) req_len += snprintf(req + req_len, sizeof(req) - req_len, "Origin: %s\r\n", origin);
    if (protocol) req_len += snprintf(req + req_len, sizeof(req) - req_len, "Sec-WebSocket-Protocol: %s\r\n", protocol);
    if (extensions)
      req_len += snprintf(req + req_len, sizeof(req) - req_len, "Sec-WebSocket-Extensions: %s\r\n", extensions);
    req_len += snprintf(req + req_len, sizeof(req) - req_len, "\r\n");
    if (req_len >= sizeof(req) - 1) {
      this->conn.close("request msg too long");
      return false;
    }
    this->conn.write((uint8_t*)req, req_len);
    while (!this->open && this->isConnected()) {
      this->conn.read([&](const char* data, uint32_t size) -> uint32_t {
        const char* data_end = data + size;
        bool status_code_checked = false, upgrade_checked = false, connection_checked = false, accept_checked = false;
        while (true) {
          const char* ln = (char*)memchr(data, '\n', data_end - data);
          if (!ln) return size;
          if (*--ln != '\r') break;
          if (!status_code_checked) { // first line
            if (memcmp(data, "HTTP/", 5)) break;
            const char* status_code = (char*)memchr(data, ' ', ln - data);
            if (!status_code) break;
            while (*status_code == ' ') status_code++;
            if (memcmp(status_code, "101 ", 4)) {
              char detail[256];
              if (ln - status_code >= 3 && status_code[0] >= '0' && status_code[0] <= '9' &&
                  status_code[1] >= '0' && status_code[1] <= '9' && status_code[2] >= '0' && status_code[2] <= '9') {
                snprintf(detail, sizeof(detail), "WebSocket handshake rejected: HTTP %.3s (expected 101). Check endpoint path, reverse proxy and server logs.", status_code);
                this->conn.close(detail);
                return 0;
              }
              break;
            }
            status_code_checked = true;
          }
          else {
            const char* val_end = ln;
            while (val_end[-1] == ' ') val_end--;
            if (val_end == data) { // end of headers
              if (!upgrade_checked || !connection_checked || !accept_checked) break;
              this->open = true;
              return data_end - ln - 2;
            }
            const char* colon = (char*)memchr(data, ':', ln - data);
            if (!colon) break;
            const char* val = colon + 1;
            while (*val == ' ') val++;
            uint32_t key_len = colon - data;
            uint32_t val_len = val_end - val;
            if (key_len == 7 && !memcmp(data, "Upgrade", 7)) {
              if (memcmp(val, "websocket", 9)) break;
              upgrade_checked = true;
            }
            else if (key_len == 10 && !memcmp(data, "Connection", 10)) {
              if (!memcmp(val, "Upgrade", 7)) connection_checked = true;
            }
            else if (key_len == 20 && !memcmp(data, "Sec-WebSocket-Accept", 20)) {
              if (val_len != 28 || memcmp(val, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", 28)) break;
              accept_checked = true;
            }
            else if (key_len == 22 && !memcmp(data, "Sec-WebSocket-Protocol", 22) && resp_protocol_size > 0) {
              uint32_t cp_len = std::min(resp_protocol_size - 1, val_len);
              memcpy(resp_protocol, val, cp_len);
              resp_protocol[cp_len] = 0;
            }
            else if (key_len == 24 && !memcmp(data, "Sec-WebSocket-Extensions", 24) && resp_extensions_size > 0) {
              uint32_t cp_len = std::min(resp_extensions_size - 1, val_len);
              memcpy(resp_extensions, val, cp_len);
              resp_extensions[cp_len] = 0;
            }
          }
          data = ln + 2; // skip \r\n
        }
        this->conn.close("WebSocket handshake rejected: expected HTTP 101 Switching Protocols with valid Upgrade headers; check endpoint path, proxy configuration and server logs");
        return size;
      });
      if (getns() > expire) this->conn.close("WebSocket handshake timeout: server did not complete the HTTP Upgrade within the connection deadline");
    }
    return this->isConnected();
  }

  void poll(EventHandler* handler) {
    this->conn.read([&](const char* data, uint32_t size) { return this->handleWSMsg(handler, (uint8_t*)data, size); });
    if (!this->isConnected()) this->handleWSClose(handler);
  }
};

template<typename EventHandler, typename ConnUserData = char, bool RecvSegment = false, uint32_t RecvBufSize = 4096,
         uint32_t MaxConns = 10>
class WSServer
{
public:
  using TcpServer = SocketTcpServer<RecvBufSize>;
  using Connection = WSConnection<EventHandler, ConnUserData, RecvSegment, RecvBufSize, false>;

  WSServer() {
    for (int i = 0; i < MaxConns; i++) {
      conns_[i] = conns_data_ + i;
    }
  }

  const char* getLastError() { return server_.getLastError(); }

  // newconn_timeout: new tcp connection max inactive time in milliseconds, 0 means no limit
  // openconn_timeout: open ws connection max inactive time in milliseconds, 0 means no limit
  // if failed, call getLastError() for the reason
  bool init(const char* server_ip, uint16_t server_port, uint64_t newconn_timeout = 0, uint64_t openconn_timeout = 0) {
    newconn_timeout_ = newconn_timeout * 1000000;
    openconn_timeout_ = openconn_timeout * 1000000;
    return server_.init("", server_ip, server_port);
  }

  void poll(EventHandler* handler) {
    uint64_t now = getns();
    uint64_t new_expire = newconn_timeout_ ? now + newconn_timeout_ : std::numeric_limits<uint64_t>::max();
    uint64_t open_expire = openconn_timeout_ ? now + openconn_timeout_ : std::numeric_limits<uint64_t>::max();
    if (conns_cnt_ < MaxConns) {
      Connection& new_conn = *conns_[conns_cnt_];
      if (server_.accept2(new_conn.conn)) {
        new_conn.init(new_expire);
        conns_cnt_++;
      }
    }
    for (int i = 0; i < conns_cnt_;) {
      Connection& conn = *conns_[i];
      conn.conn.read([&](const char* data, uint32_t size) {
        uint32_t remaining =
          conn.open ? conn.handleWSMsg(handler, (uint8_t*)data, size) : handleHttpRequest(handler, conn, data, size);
        if (remaining < size) conn.expire_time = conn.open ? open_expire : new_expire;
        return remaining;
      });
      if (now > conn.expire_time) conn.conn.close("timeout");
      if (conn.isConnected())
        i++;
      else {
        if (conn.open) conn.handleWSClose(handler);
        std::swap(conns_[i], conns_[--conns_cnt_]);
      }
    }
  }

private:
  static uint32_t rol(uint32_t value, uint32_t bits) { return (value << bits) | (value >> (32 - bits)); }
  // Be cautious that *in* will be modified and up to 64 bytes will be appended, so make sure in buffer is long enough
  static uint32_t sha1base64(uint8_t* in, uint64_t in_len, char* out) {
    uint32_t h0[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t total_len = in_len;
    in[total_len++] = 0x80;
    int padding_size = (64 - (total_len + 8) % 64) % 64;
    while (padding_size--) in[total_len++] = 0;
    for (uint64_t i = 0; i < total_len; i += 4) {
      uint32_t& w = *(uint32_t*)(in + i);
      w = be32toh(w);
    }
    *(uint32_t*)(in + total_len) = (uint32_t)(in_len >> 29);
    *(uint32_t*)(in + total_len + 4) = (uint32_t)(in_len << 3);
    for (uint8_t* in_end = in + total_len + 8; in < in_end; in += 64) {
      uint32_t* w = (uint32_t*)in;
      uint32_t h[5];
      memcpy(h, h0, sizeof(h));
      for (uint32_t i = 0, j = 0; i < 80; i++, j += 4) {
        uint32_t &a = h[j % 5], &b = h[(j + 1) % 5], &c = h[(j + 2) % 5], &d = h[(j + 3) % 5], &e = h[(j + 4) % 5];
        if (i >= 16) w[i & 15] = rol(w[(i + 13) & 15] ^ w[(i + 8) & 15] ^ w[(i + 2) & 15] ^ w[i & 15], 1);
        if (i < 40) {
          if (i < 20)
            e += ((b & (c ^ d)) ^ d) + 0x5A827999;
          else
            e += (b ^ c ^ d) + 0x6ED9EBA1;
        }
        else {
          if (i < 60)
            e += (((b | c) & d) | (b & c)) + 0x8F1BBCDC;
          else
            e += (b ^ c ^ d) + 0xCA62C1D6;
        }
        e += w[i & 15] + rol(a, 5);
        b = rol(b, 30);
      }
      for (int i = 0; i < 5; i++) h0[i] += h[i];
    }
    const char* base64tb = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t triples[7] = {h0[0] >> 8,
                           (h0[0] << 16) | (h0[1] >> 16),
                           (h0[1] << 8) | (h0[2] >> 24),
                           h0[2],
                           h0[3] >> 8,
                           (h0[3] << 16) | (h0[4] >> 16),
                           h0[4] << 8};
    for (uint32_t i = 0; i < 7; i++) {
      out[i * 4] = base64tb[(triples[i] >> 18) & 63];
      out[i * 4 + 1] = base64tb[(triples[i] >> 12) & 63];
      out[i * 4 + 2] = base64tb[(triples[i] >> 6) & 63];
      out[i * 4 + 3] = base64tb[triples[i] & 63];
    }
    out[27] = '=';
    return 28;
  }

  uint32_t handleHttpRequest(EventHandler* handler, Connection& conn, const char* data, uint32_t size) {
    const char* data_end = data + size;
    const int ValueBufSize = 128;
    char request_uri[1024] = {0};
    char host[ValueBufSize] = {0};
    char origin[ValueBufSize] = {0};
    char wskey[ValueBufSize] = {0};
    char wsprotocol[ValueBufSize] = {0};
    char wsextensions[ValueBufSize] = {0};
    bool upgrade_checked = false, connection_checked = false, wsversion_checked = false;
    while (true) {
      const char* ln = (char*)memchr(data, '\n', data_end - data);
      if (!ln) return size;
      if (*--ln != '\r') break;
      if (request_uri[0] == 0) { // first line
        if (memcmp(data, "GET ", 4)) break;
        data += 4;
        while (*data == ' ') data++;
        const char* uri_end = (char*)memchr(data, ' ', ln - data);
        uint32_t uri_len = uri_end - data;
        if (!uri_end || uri_len >= sizeof(request_uri)) break;
        memcpy(request_uri, data, uri_len);
        request_uri[uri_len] = 0;
      }
      else {
        const char* val_end = ln;
        while (val_end[-1] == ' ') val_end--;
        if (val_end == data) { // end of headers
          if (!host[0] || !wskey[0] || !upgrade_checked || !connection_checked || !wsversion_checked) break;
          char resp_wsprotocol[ValueBufSize] = {0};
          char resp_wsextensions[ValueBufSize] = {0};
          char resp[1024];
          uint32_t resp_len = 0;
          bool accept = handler->onWSConnect(
            conn, request_uri, host, origin[0] ? origin : nullptr, wsprotocol[0] ? wsprotocol : nullptr,
            wsextensions[0] ? wsextensions : nullptr, resp_wsprotocol, ValueBufSize, resp_wsextensions, ValueBufSize);
          if (accept) {
            conn.open = true;
            memcpy(wskey + 24, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
            char accept_str[32];
            accept_str[sha1base64((uint8_t*)wskey, 24 + 36, accept_str)] = 0;
            resp_len = sprintf(resp,
                               "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
                               "Upgrade\r\nSec-WebSocket-Accept: %s\r\n",
                               accept_str);
          }
          else {
            resp_len = sprintf(resp, "HTTP/1.1 403 Forbidden\r\nSec-WebSocket-Version: 13\r\n");
          }
          if (resp_wsprotocol[0])
            resp_len += sprintf(resp + resp_len, "Sec-WebSocket-Protocol: %s\r\n", resp_wsprotocol);
          if (resp_wsextensions[0])
            resp_len += sprintf(resp + resp_len, "Sec-WebSocket-Extensions: %s\r\n", resp_wsextensions);
          resp_len += sprintf(resp + resp_len, "\r\n");
          conn.conn.write((uint8_t*)resp, resp_len);
          return data_end - ln - 2;
        }
        const char* colon = (char*)memchr(data, ':', ln - data);
        if (!colon) break;
        const char* val = colon + 1;
        while (*val == ' ') val++;
        uint32_t key_len = colon - data;
        uint32_t val_len = val_end - val;
        if (val_len < ValueBufSize) {
          if (key_len == 4 && !memcmp(data, "Host", 4)) {
            memcpy(host, val, val_len);
            host[val_len] = 0;
          }
          else if (key_len == 6 && !memcmp(data, "Origin", 6)) {
            memcpy(origin, val, val_len);
            origin[val_len] = 0;
          }
          else if (key_len == 7 && !memcmp(data, "Upgrade", 7)) {
            if (memcmp(val, "websocket", 9)) break;
            upgrade_checked = true;
          }
          else if (key_len == 10 && !memcmp(data, "Connection", 10)) {
            if (!memcmp(val, "Upgrade", 7)) connection_checked = true;
          }
          else if (key_len == 17 && !memcmp(data, "Sec-WebSocket-Key", 17)) {
            if (val_len != 24) break;
            memcpy(wskey, val, val_len);
          }
          else if (key_len == 21 && !memcmp(data, "Sec-WebSocket-Version", 21)) {
            if (val_len != 2 || memcmp(val, "13", 2)) break;
            wsversion_checked = true;
          }
          else if (key_len == 22 && !memcmp(data, "Sec-WebSocket-Protocol", 22)) {
            memcpy(wsprotocol, val, val_len);
            wsprotocol[val_len] = 0;
          }
          else if (key_len == 24 && !memcmp(data, "Sec-WebSocket-Extensions", 24)) {
            memcpy(wsextensions, val, val_len);
            wsextensions[val_len] = 0;
          }
        }
      }
      data = ln + 2; // skip \r\n
    }
    const char* resp400 = "HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\n\r\n";
    conn.conn.write((uint8_t*)resp400, strlen(resp400));
    conn.conn.close("bad request");
    return size;
  }

private:
  uint64_t newconn_timeout_;
  uint64_t openconn_timeout_;
  TcpServer server_;

  uint32_t conns_cnt_ = 0;
  Connection* conns_[MaxConns];
  Connection conns_data_[MaxConns];
};

} // namespace websocket
