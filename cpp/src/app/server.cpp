/* =============================================================
   LOA Hybrid — Minimal single-threaded HTTP/1.1 server
   Winsock on Windows, BSD sockets elsewhere.
   ============================================================= */
#include "app/server.hpp"
#include "hybrid/hybrid.hpp"
#include "app/summary.hpp"
#include "common/json.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
  #define BADSOCK INVALID_SOCKET
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSESOCK close
  #define BADSOCK (-1)
#endif

namespace {

const std::size_t MAX_BODY = 1 << 20;   // 1 MB is far more than any config

std::string mimeOf(const std::string& path) {
  auto ends = [&](const char* ext) {
    const std::size_t n = std::strlen(ext);
    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".css"))  return "text/css; charset=utf-8";
  if (ends(".js"))   return "application/javascript; charset=utf-8";
  if (ends(".json")) return "application/json; charset=utf-8";
  if (ends(".svg"))  return "image/svg+xml";
  if (ends(".png"))  return "image/png";
  if (ends(".ico"))  return "image/x-icon";
  if (ends(".md"))   return "text/markdown; charset=utf-8";
  return "application/octet-stream";
}

void sendAll(sock_t s, const std::string& data) {
  std::size_t off = 0;
  while (off < data.size()) {
    const int n = send(s, data.data() + off, static_cast<int>(data.size() - off), 0);
    if (n <= 0) return;
    off += static_cast<std::size_t>(n);
  }
}

void reply(sock_t s, int code, const char* status, const std::string& type, const std::string& body) {
  std::ostringstream h;
  h << "HTTP/1.1 " << code << ' ' << status << "\r\n"
    << "Content-Type: " << type << "\r\n"
    << "Content-Length: " << body.size() << "\r\n"
    << "Cache-Control: no-store\r\n"
    << "Connection: close\r\n\r\n";
  sendAll(s, h.str() + body);
}

void replyJson(sock_t s, int code, const char* status, const json::Value& v) {
  reply(s, code, status, "application/json; charset=utf-8", v.dump());
}

json::Value errorJson(const std::string& msg) {
  json::Value e = json::Value::object();
  e.set("ok", false); e.set("error", msg);
  return e;
}

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path.c_str(), std::ios::binary);
  if (!f) return false;
  std::ostringstream ss; ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Try to parse one request out of what has arrived so far.
// Returns 1 = complete, 0 = need more bytes, -1 = malformed / too large.
int parseRequest(const std::string& buf, std::string& method, std::string& path, std::string& body) {
  const std::size_t hdrEnd = buf.find("\r\n\r\n");
  if (hdrEnd == std::string::npos) return buf.size() > 64 * 1024 ? -1 : 0;
  std::istringstream hs(buf.substr(0, hdrEnd));
  std::string line;
  std::getline(hs, line);
  std::istringstream rl(line);
  rl >> method >> path;
  if (method.empty() || path.empty()) return -1;

  std::size_t len = 0;
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::size_t c = line.find(':');
    if (c == std::string::npos) continue;
    std::string k = line.substr(0, c);
    for (auto& ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (k == "content-length") len = static_cast<std::size_t>(std::strtoul(line.c_str() + c + 1, nullptr, 10));
  }
  if (len > MAX_BODY) return -1;
  if (buf.size() < hdrEnd + 4 + len) return 0;
  body = buf.substr(hdrEnd + 4, len);
  return 1;
}

void handle(sock_t c, const std::string& webRoot, const std::string& method, std::string path, const std::string& body) {
  const std::size_t q = path.find('?');
  if (q != std::string::npos) path.resize(q);

  if (path == "/api/summary") {   // results.html: paper comparison computed from web/data
    try { replyJson(c, 200, "OK", resultsSummary(webRoot)); }
    catch (const std::exception& e) { replyJson(c, 500, "Internal Server Error", errorJson(e.what())); }
    return;
  }
  if (path == "/api/health") {
    json::Value v = json::Value::object();
    v.set("ok", true); v.set("engine", "loa_hybrid (C++)");
    replyJson(c, 200, "OK", v);
    return;
  }
  if (path == "/api/run" || path == "/api/check") {
    if (method != "POST") { replyJson(c, 405, "Method Not Allowed", errorJson("use POST")); return; }
    json::Value cfg;
    try { cfg = json::parse(body); }
    catch (const std::exception& e) { replyJson(c, 400, "Bad Request", errorJson(e.what())); return; }
    const bool run = path == "/api/run";
    if (run) { std::printf("[run] %u-byte config\n", static_cast<unsigned>(body.size())); std::fflush(stdout); }
    try { replyJson(c, 200, "OK", run ? runHybrid(cfg) : checkConfig(cfg)); }
    catch (const std::exception& e) { replyJson(c, 500, "Internal Server Error", errorJson(e.what())); }
    return;
  }
  if (method != "GET") { reply(c, 405, "Method Not Allowed", "text/plain", "method not allowed"); return; }

  // Static files: refuse anything that tries to climb out of webRoot
  if (path == "/") path = "/index.html";
  if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
    reply(c, 403, "Forbidden", "text/plain", "forbidden"); return;
  }
  std::string data;
  if (!readFile(webRoot + path, data)) { reply(c, 404, "Not Found", "text/plain", "not found: " + path); return; }
  reply(c, 200, "OK", mimeOf(path), data);
}

} // namespace

int serve(int port, const std::string& webRoot) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif
  sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
  if (srv == BADSOCK) { std::fprintf(stderr, "socket() failed\n"); return 1; }
  int yes = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<unsigned short>(port));
  addr.sin_addr.s_addr = htonl(0x7F000001);   // 127.0.0.1 only
  if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    std::fprintf(stderr, "Cannot bind 127.0.0.1:%d (already in use?)\n", port);
    CLOSESOCK(srv);
    return 1;
  }
  listen(srv, 16);

  std::string probe;
  if (!readFile(webRoot + "/index.html", probe))
    std::fprintf(stderr, "Warning: %s/index.html not found. Pass --web <folder>.\n", webRoot.c_str());
  std::printf("LOA Hybrid server running at http://localhost:%d  (web root: %s)\n", port, webRoot.c_str());
  std::printf("Press Ctrl+C to stop.\n");
  std::fflush(stdout);

  // select() loop: browsers open idle (pre)connections, so never block on one client.
  // Requests are still answered one at a time; each finished socket is closed.
  struct Client { sock_t s; std::string buf; };
  std::vector<Client> clients;
  for (;;) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(srv, &rd);
    int maxfd = static_cast<int>(srv);
    for (const auto& c : clients) { FD_SET(c.s, &rd); maxfd = std::max(maxfd, static_cast<int>(c.s)); }
    if (select(maxfd + 1, &rd, nullptr, nullptr, nullptr) <= 0) continue;

    if (FD_ISSET(srv, &rd)) {
      sock_t c = accept(srv, nullptr, nullptr);
      if (c != BADSOCK) {
        if (clients.size() >= 48) CLOSESOCK(c);     // stay well under FD_SETSIZE
        else clients.push_back({ c, std::string() });
      }
    }
    for (std::size_t i = 0; i < clients.size();) {
      Client& c = clients[i];
      bool drop = false;
      if (FD_ISSET(c.s, &rd)) {
        char tmp[8192];
        const int n = recv(c.s, tmp, sizeof tmp, 0);
        if (n <= 0) drop = true;
        else {
          c.buf.append(tmp, static_cast<std::size_t>(n));
          std::string method, path, body;
          const int st = parseRequest(c.buf, method, path, body);
          if (st < 0) { reply(c.s, 400, "Bad Request", "text/plain", "bad request"); drop = true; }
          else if (st > 0) { handle(c.s, webRoot, method, path, body); drop = true; }
        }
      }
      if (drop) { CLOSESOCK(c.s); clients.erase(clients.begin() + static_cast<long>(i)); }
      else i++;
    }
  }
}
