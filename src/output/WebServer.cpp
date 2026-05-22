#include "WebServer.h"

#include "model/MonteCarlo.h"
#include "util/CsvParser.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

std::string urlDecodeLocal(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            const char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out += decoded;
            i += 2;
        } else if (value[i] == '+') {
            out += ' ';
        } else {
            out += value[i];
        }
    }

    return out;
}

std::map<std::string, std::string> parseUrlEncoded(const std::string& body) {
    std::map<std::string, std::string> values;
    std::stringstream ss(body);
    std::string part;

    while (std::getline(ss, part, '&')) {
        const size_t eq = part.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = urlDecodeLocal(part.substr(0, eq));
        const std::string value = urlDecodeLocal(part.substr(eq + 1));
        values[key] = value;
    }

    return values;
}

std::string statusText(int statusCode) {
    switch (statusCode) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

std::string readFullRequest(int clientFd) {
    std::string request;
    char buffer[4096];
    ssize_t bytesRead = 0;

    do {
        bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytesRead > 0) {
            request.append(buffer, static_cast<size_t>(bytesRead));
        }

        if (bytesRead <= 0) {
            break;
        }

        const size_t headerEnd = request.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            continue;
        }

        const std::string headers = request.substr(0, headerEnd);
        const size_t contentLengthPos = headers.find("Content-Length:");
        size_t expectedBody = 0;
        if (contentLengthPos != std::string::npos) {
            const size_t lineEnd = headers.find("\r\n", contentLengthPos);
            std::string lenField = headers.substr(
                contentLengthPos + std::strlen("Content-Length:"),
                lineEnd == std::string::npos ? std::string::npos : lineEnd - contentLengthPos - std::strlen("Content-Length:"));
            expectedBody = static_cast<size_t>(std::max(0, std::stoi(lenField)));
        }

        const size_t currentBody = request.size() - (headerEnd + 4);
        if (currentBody >= expectedBody) {
            break;
        }
    } while (bytesRead > 0);

    return request;
}

} // namespace

WebServer::WebServer(Season season,
                     const std::string& schedulePath,
                     double homeAdvantage,
                     double strengthWeight,
                     int defaultIterations)
    : season_(std::move(season)),
      schedulePath_(schedulePath),
      homeAdvantage_(homeAdvantage),
      strengthWeight_(strengthWeight),
      defaultIterations_(defaultIterations) {}

WebServer::Response WebServer::handleForTests(const std::string& method,
                                const std::string& rawPath,
                                const std::string& body) {
    int statusCode = 200;
    std::string contentType = "text/plain; charset=utf-8";
    const std::string responseBody = handleRequest(method, rawPath, body, statusCode, contentType);
    return {statusCode, contentType, responseBody};
}

void WebServer::run(int port) {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        throw std::runtime_error("Failed to create server socket");
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(serverFd);
        throw std::runtime_error("Failed to set socket options");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const std::string err = std::strerror(errno);
        close(serverFd);
        throw std::runtime_error("Failed to bind web server: " + err);
    }

    if (listen(serverFd, 32) < 0) {
        close(serverFd);
        throw std::runtime_error("Failed to listen on web server socket");
    }

    std::cout << "Web server running at http://127.0.0.1:" << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    while (true) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) {
            continue;
        }

        try {
            const std::string request = readFullRequest(clientFd);
            std::stringstream reqStream(request);
            std::string requestLine;
            std::getline(reqStream, requestLine);
            if (!requestLine.empty() && requestLine.back() == '\r') {
                requestLine.pop_back();
            }

            std::stringstream lineStream(requestLine);
            std::string method;
            std::string rawPath;
            std::string version;
            lineStream >> method >> rawPath >> version;

            int statusCode = 200;
            std::string contentType = "text/plain; charset=utf-8";

            std::string body;
            const size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                body = request.substr(bodyPos + 4);
            }

            const std::string responseBody =
                handleRequest(method, rawPath, body, statusCode, contentType);
            const std::string response =
                buildHttpResponse(statusCode, contentType, responseBody);

            send(clientFd, response.c_str(), response.size(), 0);
        } catch (const std::exception& e) {
            const std::string response = buildHttpResponse(
                500,
                "text/plain; charset=utf-8",
                std::string("Internal server error: ") + e.what());
            send(clientFd, response.c_str(), response.size(), 0);
        }

        close(clientFd);
    }
}

std::string WebServer::handleRequest(const std::string& method,
                                     const std::string& rawPath,
                                     const std::string& body,
                                     int& statusCode,
                                     std::string& contentType) {
    const std::string path = getPathOnly(rawPath);

    if (method == "GET" && (path == "/" || path == "/standings")) {
        contentType = "text/html; charset=utf-8";
        return renderStandingsHtml();
    }
    if (method == "GET" && path == "/simulation") {
        contentType = "text/html; charset=utf-8";
        return renderSimulationHtml(parseIterations(rawPath, defaultIterations_));
    }
    if (method == "GET" && path == "/impact") {
        contentType = "text/html; charset=utf-8";
        return renderImpactHtml(parseIterations(rawPath, defaultIterations_));
    }
    if (method == "GET" && path == "/api/standings") {
        contentType = "application/json; charset=utf-8";
        return standingsJson();
    }
    if (method == "GET" && path == "/api/simulation") {
        contentType = "application/json; charset=utf-8";
        return simulationJson(parseIterations(rawPath, defaultIterations_));
    }
    if (method == "GET" && path == "/api/impact") {
        contentType = "application/json; charset=utf-8";
        return impactJson(parseIterations(rawPath, defaultIterations_));
    }
    if (path == "/api/update-result") {
        if (method != "POST") {
            statusCode = 405;
            contentType = "application/json; charset=utf-8";
            return "{\"error\":\"POST required\"}";
        }

        std::string error;
        if (!applyResultUpdate(body, error)) {
            statusCode = 400;
            contentType = "application/json; charset=utf-8";
            return std::string("{\"ok\":false,\"error\":\"") + jsonEscape(error) + "\"}";
        }

        contentType = "application/json; charset=utf-8";
        return "{\"ok\":true}";
    }

    statusCode = 404;
    contentType = "text/plain; charset=utf-8";
    return "Not found";
}

static std::string buildDashboardHtml() {
    return R"rawhtml(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>nfl3 // Playoff Simulator & Tracker</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=Outfit:wght@400;500;600;700;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg-primary: #0b0f19;
      --bg-secondary: #161d30;
      --bg-surface: rgba(22, 29, 48, 0.65);
      --bg-surface-opaque: #161d30;
      --border-color: rgba(255, 255, 255, 0.08);
      --text-primary: #f8fafc;
      --text-secondary: #94a3b8;
      --accent-color: #6366f1;
      --accent-gradient: linear-gradient(135deg, #6366f1, #8b5cf6);
      --accent-glow: 0 0 15px rgba(99, 102, 241, 0.4);
      --success-color: #10b981;
      --success-bg: rgba(16, 185, 129, 0.15);
      --danger-color: #f43f5e;
      --danger-bg: rgba(244, 63, 94, 0.15);
      --warning-color: #f59e0b;
      --card-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.35);
      --font-display: 'Outfit', sans-serif;
      --font-body: 'Inter', sans-serif;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }

    body {
      background-color: var(--bg-primary);
      background-image: radial-gradient(circle at 80% 20%, rgba(99, 102, 241, 0.15) 0%, transparent 50%),
                        radial-gradient(circle at 10% 80%, rgba(139, 92, 246, 0.1) 0%, transparent 40%);
      background-attachment: fixed;
      color: var(--text-primary);
      font-family: var(--font-body);
      min-height: 100vh;
      line-height: 1.5;
    }

    header {
      background: rgba(11, 15, 25, 0.7);
      backdrop-filter: blur(12px);
      border-bottom: 1px solid var(--border-color);
      position: sticky;
      top: 0;
      z-index: 100;
      padding: 1rem 2rem;
    }

    .header-container {
      max-width: 1200px;
      margin: 0 auto;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 1rem;
    }

    .logo-area {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      text-decoration: none;
    }

    .logo-text {
      font-family: var(--font-display);
      font-weight: 800;
      font-size: 1.5rem;
      background: linear-gradient(135deg, #a5b4fc, #c084fc);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      letter-spacing: -0.05em;
    }

    .logo-sub {
      font-family: var(--font-display);
      font-weight: 500;
      font-size: 0.75rem;
      text-transform: uppercase;
      letter-spacing: 0.2em;
      color: var(--text-secondary);
      border-left: 1px solid var(--border-color);
      padding-left: 0.5rem;
    }

    nav {
      display: flex;
      gap: 0.5rem;
    }

    .nav-btn {
      background: transparent;
      border: 1px solid transparent;
      color: var(--text-secondary);
      font-family: var(--font-display);
      font-weight: 500;
      padding: 0.5rem 1rem;
      border-radius: 8px;
      cursor: pointer;
      text-decoration: none;
      transition: all 0.2s ease-in-out;
      font-size: 0.95rem;
    }

    .nav-btn:hover {
      color: var(--text-primary);
      background: rgba(255, 255, 255, 0.05);
    }

    .nav-btn.active {
      color: var(--text-primary);
      background: rgba(99, 102, 241, 0.2);
      border-color: rgba(99, 102, 241, 0.4);
      box-shadow: 0 0 12px rgba(99, 102, 241, 0.15);
    }

    main {
      max-width: 1200px;
      margin: 2rem auto;
      padding: 0 1.5rem 4rem 1.5rem;
    }

    .view-section {
      display: none;
      animation: fadeIn 0.3s ease-in-out forwards;
    }

    .view-section.active {
      display: block;
    }

    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(8px); }
      to { opacity: 1; transform: translateY(0); }
    }

    /* Dashboard Header */
    .dashboard-header {
      margin-bottom: 2rem;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 1.5rem;
    }

    .page-title {
      font-family: var(--font-display);
      font-size: 2rem;
      font-weight: 700;
      letter-spacing: -0.02em;
    }

    .page-desc {
      color: var(--text-secondary);
      font-size: 0.95rem;
      margin-top: 0.25rem;
    }

    /* Controls & Inputs */
    .control-panel {
      background: var(--bg-surface);
      backdrop-filter: blur(16px);
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 1rem 1.5rem;
      display: flex;
      align-items: center;
      gap: 1.5rem;
      flex-wrap: wrap;
      margin-bottom: 2rem;
      box-shadow: var(--card-shadow);
    }

    .search-wrapper {
      position: relative;
      flex-grow: 1;
      max-width: 400px;
    }

    .form-input {
      width: 100%;
      background: rgba(0, 0, 0, 0.25);
      border: 1px solid var(--border-color);
      color: var(--text-primary);
      padding: 0.6rem 1rem;
      border-radius: 8px;
      font-family: var(--font-body);
      font-size: 0.9rem;
      transition: all 0.2s ease;
    }

    .form-input:focus {
      outline: none;
      border-color: var(--accent-color);
      box-shadow: 0 0 8px rgba(99, 102, 241, 0.3);
    }

    .btn {
      font-family: var(--font-display);
      font-weight: 600;
      font-size: 0.9rem;
      padding: 0.6rem 1.2rem;
      border-radius: 8px;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      gap: 0.5rem;
      border: 1px solid transparent;
    }

    .btn-primary {
      background: var(--accent-gradient);
      color: white;
      box-shadow: var(--accent-glow);
    }

    .btn-primary:hover {
      opacity: 0.9;
      transform: translateY(-1px);
      box-shadow: 0 0 20px rgba(99, 102, 241, 0.6);
    }

    .btn-secondary {
      background: rgba(255, 255, 255, 0.05);
      color: var(--text-primary);
      border-color: var(--border-color);
    }

    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.1);
      border-color: rgba(255, 255, 255, 0.2);
    }

    /* Grids & Cards */
    .division-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 1.5rem;
      margin-bottom: 2rem;
    }

    .card {
      background: var(--bg-surface);
      backdrop-filter: blur(16px);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 1.25rem;
      box-shadow: var(--card-shadow);
      transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    }

    .card:hover {
      transform: translateY(-4px);
      border-color: rgba(99, 102, 241, 0.25);
      box-shadow: 0 12px 40px 0 rgba(0, 0, 0, 0.5);
    }

    .card-title {
      font-family: var(--font-display);
      font-size: 1.15rem;
      font-weight: 700;
      margin-bottom: 1rem;
      border-bottom: 1px solid var(--border-color);
      padding-bottom: 0.5rem;
      letter-spacing: -0.01em;
      color: #cbd5e1;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    /* Table Styles */
    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.9rem;
    }

    th, td {
      padding: 0.5rem 0.75rem;
      text-align: left;
      border-bottom: 1px solid rgba(255, 255, 255, 0.04);
    }

    th {
      font-family: var(--font-display);
      font-weight: 600;
      color: var(--text-secondary);
      font-size: 0.8rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }

    td {
      font-weight: 500;
    }

    .team-abbr-badge {
      font-family: var(--font-display);
      font-weight: 700;
      padding: 0.15rem 0.4rem;
      border-radius: 4px;
      font-size: 0.85rem;
      background: rgba(255,255,255,0.06);
    }

    tr:hover td {
      color: white;
    }

    .playoff-row-highlight {
      background: rgba(16, 185, 129, 0.04);
    }

    /* Progress bar */
    .progress-container {
      display: flex;
      align-items: center;
      gap: 0.5rem;
    }

    .progress-bar-bg {
      width: 70px;
      height: 6px;
      background: rgba(255, 255, 255, 0.08);
      border-radius: 4px;
      overflow: hidden;
    }

    .progress-bar-fill {
      height: 100%;
      border-radius: 4px;
      transition: width 0.8s ease-out;
    }

    .prog-emerald {
      background: linear-gradient(90deg, #10b981, #34d399);
    }

    .prog-indigo {
      background: linear-gradient(90deg, #6366f1, #818cf8);
    }

    .prog-rose {
      background: linear-gradient(90deg, #f43f5e, #fda4af);
    }

    /* Modal Styling */
    .modal-backdrop {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(5, 7, 13, 0.8);
      backdrop-filter: blur(8px);
      z-index: 200;
      display: none;
      align-items: center;
      justify-content: center;
      padding: 1rem;
    }

    .modal-content {
      background: var(--bg-secondary);
      border: 1px solid var(--border-color);
      border-radius: 20px;
      max-width: 460px;
      width: 100%;
      padding: 2rem;
      box-shadow: 0 20px 50px rgba(0, 0, 0, 0.6);
      animation: modalSlide 0.25s cubic-bezier(0.16, 1, 0.3, 1) forwards;
    }

    @keyframes modalSlide {
      from { transform: scale(0.95) translateY(20px); opacity: 0; }
      to { transform: scale(1) translateY(0); opacity: 1; }
    }

    .modal-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 1.5rem;
    }

    .modal-close {
      background: transparent;
      border: none;
      color: var(--text-secondary);
      font-size: 1.5rem;
      cursor: pointer;
      transition: color 0.2s;
    }

    .modal-close:hover {
      color: white;
    }

    .form-group {
      margin-bottom: 1.25rem;
    }

    .form-label {
      display: block;
      font-family: var(--font-display);
      font-size: 0.85rem;
      font-weight: 600;
      color: var(--text-secondary);
      margin-bottom: 0.4rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }

    /* Toast */
    .toast {
      position: fixed;
      bottom: 2rem;
      right: 2rem;
      background: rgba(16, 185, 129, 0.95);
      color: white;
      padding: 0.75rem 1.5rem;
      border-radius: 10px;
      font-family: var(--font-display);
      font-weight: 600;
      box-shadow: 0 10px 25px rgba(16, 185, 129, 0.4);
      z-index: 300;
      transform: translateY(100px);
      opacity: 0;
      transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
      display: flex;
      align-items: center;
      gap: 0.5rem;
    }

    .toast.show {
      transform: translateY(0);
      opacity: 1;
    }

    /* Simulation specific */
    .sim-slider-group {
      display: flex;
      align-items: center;
      gap: 1rem;
      flex-grow: 1;
    }

    .sim-slider-container {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      flex-grow: 1;
      max-width: 300px;
    }

    .sim-slider {
      flex-grow: 1;
      accent-color: var(--accent-color);
    }

    .sim-val {
      font-family: var(--font-display);
      font-weight: 700;
      font-size: 1rem;
      min-width: 60px;
      text-align: right;
    }

    /* Loading overlay */
    .loading-overlay {
      display: none;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 4rem;
      text-align: center;
      gap: 1rem;
    }

    .spinner {
      width: 48px;
      height: 48px;
      border: 4px solid rgba(99, 102, 241, 0.1);
      border-top-color: var(--accent-color);
      border-radius: 50%;
      animation: spin 1s linear infinite;
      box-shadow: 0 0 15px rgba(99, 102, 241, 0.2);
    }

    @keyframes spin {
      to { transform: rotate(360deg); }
    }

    .loading-text {
      font-family: var(--font-display);
      font-size: 1.2rem;
      font-weight: 600;
      color: var(--text-primary);
    }

    /* Impact matchup cards */
    .matchup-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
      gap: 1.5rem;
    }

    .matchup-card {
      background: var(--bg-surface);
      backdrop-filter: blur(16px);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 1.5rem;
      box-shadow: var(--card-shadow);
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      gap: 1.25rem;
      position: relative;
      overflow: hidden;
    }

    .matchup-card::before {
      content: '';
      position: absolute;
      top: 0;
      left: 0;
      width: 4px;
      height: 100%;
      background: var(--accent-gradient);
      opacity: 0.6;
    }

    .matchup-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-family: var(--font-display);
      font-size: 0.8rem;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      color: var(--text-secondary);
    }

    .matchup-body {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 1rem;
    }

    .matchup-team {
      display: flex;
      flex-direction: column;
      align-items: center;
      flex: 1;
    }

    .team-large-abbr {
      font-family: var(--font-display);
      font-size: 1.75rem;
      font-weight: 800;
      letter-spacing: -0.05em;
    }

    .vs-text {
      font-family: var(--font-display);
      font-size: 0.85rem;
      font-weight: 700;
      color: var(--text-secondary);
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid var(--border-color);
      padding: 0.3rem 0.6rem;
      border-radius: 50%;
    }

    .matchup-footer {
      border-top: 1px solid rgba(255,255,255,0.04);
      padding-top: 1rem;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    .delta-badge {
      display: inline-flex;
      align-items: center;
      gap: 0.25rem;
      font-family: var(--font-display);
      font-weight: 700;
      font-size: 0.8rem;
      padding: 0.25rem 0.5rem;
      border-radius: 6px;
    }

    .delta-positive {
      color: var(--success-color);
      background: var(--success-bg);
    }

    .delta-negative {
      color: var(--danger-color);
      background: var(--danger-bg);
    }

    .delta-neutral {
      color: var(--text-secondary);
      background: rgba(255,255,255,0.05);
    }

    .impact-rating {
      font-family: var(--font-display);
      font-weight: 700;
      font-size: 0.85rem;
      letter-spacing: 0.02em;
    }
  </style>
</head>
<body>

  <header>
    <div class="header-container">
      <a href="/" class="logo-area" onclick="handleLogoClick(event)">
        <span class="logo-text">nfl3</span>
        <span class="logo-sub">analytics</span>
      </a>
      <nav>
        <button id="nav-standings" class="nav-btn" onclick="switchTab('standings')">Standings</button>
        <button id="nav-simulation" class="nav-btn" onclick="switchTab('simulation')">Playoff Sim</button>
        <button id="nav-impact" class="nav-btn" onclick="switchTab('impact')">Game Importance</button>
      </nav>
    </div>
  </header>

  <main>
    <!-- STANDINGS SECTION -->
    <section id="standings-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">NFL Divisional Standings</h1>
          <p class="page-desc">Real-time regular season records, division leaders, and rankings.</p>
        </div>
        <button class="btn btn-primary" onclick="openUpdateModal()">
          Record Score
        </button>
      </div>

      <div class="control-panel">
        <div class="search-wrapper">
          <input type="text" id="teamSearch" class="form-input" placeholder="Search by team or division..." oninput="filterTeams()">
        </div>
      </div>

      <div id="standings-container" class="division-grid">
        <!-- Division Cards loaded dynamically -->
      </div>
    </section>

    <!-- SIMULATION SECTION -->
    <section id="simulation-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Monte Carlo Playoff Simulator</h1>
          <p class="page-desc">Simulates remaining matchups to calculate playoff probability percentages.</p>
        </div>
      </div>

      <div class="control-panel">
        <div class="sim-slider-group">
          <span class="form-label" style="margin-bottom:0">Simulations:</span>
          <div class="sim-slider-container">
            <input type="range" id="simSlider" class="sim-slider" min="1000" max="100000" step="1000" value="50000" oninput="syncSliderVal(this.value)">
            <span id="simVal" class="sim-val">50,000</span>
          </div>
        </div>
        <button class="btn btn-primary" onclick="runSimulation()">
          Run Simulation
        </button>
      </div>

      <div id="sim-loading" class="loading-overlay">
        <div class="spinner"></div>
        <p class="loading-text">Running Monte Carlo Engine...</p>
        <p style="color:var(--text-secondary);font-size:0.85rem">Simulating all remaining unplayed matchups</p>
      </div>

      <div id="sim-results-card" class="card" style="display:none">
        <div class="card-title">
          <span>Simulation Results</span>
          <span id="sim-metadata" style="font-size:0.8rem;font-weight:500;color:var(--text-secondary)"></span>
        </div>

        <div class="segmented-control" style="display:inline-flex;background:rgba(255,255,255,0.03);border:1px solid var(--border-color);border-radius:8px;padding:0.25rem;gap:0.25rem;margin:1rem 0;">
          <button id="toggle-seeding" class="nav-btn active" onclick="setSimView('seeding')" style="font-size:0.8rem;padding:0.35rem 0.75rem">
            Regular Seeding Odds
          </button>
          <button id="toggle-postseason" class="nav-btn" onclick="setSimView('postseason')" style="font-size:0.8rem;padding:0.35rem 0.75rem">
            Postseason Tournament Odds
          </button>
        </div>

        <table id="sim-table">
          <thead>
            <tr>
              <th>Team</th>
              <th>Playoff %</th>
              <th>Division Leader %</th>
              <th>Wild Card %</th>
            </tr>
          </thead>
          <tbody id="sim-tbody">
            <!-- Simulated probabilities loaded dynamically -->
          </tbody>
        </table>
      </div>
    </section>

    <!-- IMPACT SECTION -->
    <section id="impact-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Matchup Leverage & Importance</h1>
          <p class="page-desc">Calculates exactly how the outcome of next week's games shifts each team's playoff odds.</p>
        </div>
      </div>

      <div id="impact-loading" class="loading-overlay">
        <div class="spinner"></div>
        <p class="loading-text">Analyzing Playoff Leverage...</p>
      </div>

      <div id="impact-results-container" class="matchup-grid">
        <!-- Matchup leverage cards loaded dynamically -->
      </div>
    </section>
  </main>

  <!-- Modal -->
  <div id="updateModal" class="modal-backdrop" onclick="handleBackdropClick(event)">
    <div class="modal-content">
      <div class="modal-header">
        <h3 class="page-title" style="font-size:1.25rem">Record Game Outcome</h3>
        <button class="modal-close" onclick="closeUpdateModal()">&times;</button>
      </div>
      <form id="updateForm" onsubmit="handleFormSubmit(event)">
        <div class="form-group">
          <label class="form-label" for="weekInput">Week</label>
          <input type="number" id="weekInput" name="week" class="form-input" min="1" max="18" placeholder="e.g. 1" required>
        </div>
        <div class="form-group" style="display:grid;grid-template-columns:1fr 1fr;gap:1rem">
          <div>
            <label class="form-label" for="homeTeamInput">Home Team</label>
            <input type="text" id="homeTeamInput" name="home_team" class="form-input" placeholder="e.g. KC" required style="text-transform:uppercase">
          </div>
          <div>
            <label class="form-label" for="awayTeamInput">Away Team</label>
            <input type="text" id="awayTeamInput" name="away_team" class="form-input" placeholder="e.g. DEN" required style="text-transform:uppercase">
          </div>
        </div>
        <div class="form-group" style="display:grid;grid-template-columns:1fr 1fr;gap:1rem">
          <div>
            <label class="form-label" for="homeScoreInput">Home Score</label>
            <input type="number" id="homeScoreInput" name="home_score" class="form-input" min="0" placeholder="e.g. 24" required>
          </div>
          <div>
            <label class="form-label" for="awayScoreInput">Away Score</label>
            <input type="number" id="awayScoreInput" name="away_score" class="form-input" min="0" placeholder="e.g. 17" required>
          </div>
        </div>
        <div style="display:flex;justify-content:flex-end;gap:0.75rem;margin-top:1.5rem">
          <button type="button" class="btn btn-secondary" onclick="closeUpdateModal()">Cancel</button>
          <button type="submit" class="btn btn-primary">Save Result</button>
        </div>
      </form>
    </div>
  </div>

  <div id="toast" class="toast">
    <span style="font-size:1.2rem">&#x2714;</span>
    <span id="toast-message">Game score successfully recorded!</span>
  </div>

  <script>
    // Global active states
    let currentTab = "standings";

    document.addEventListener("DOMContentLoaded", () => {
      // Sync numerical slider value formatted
      syncSliderVal(document.getElementById("simSlider").value);

      // Determine initial tab from pathname or default
      const path = window.location.pathname;
      if (path === "/simulation") {
        currentTab = "simulation";
      } else if (path === "/impact") {
        currentTab = "impact";
      } else {
        currentTab = "standings";
      }

      switchTab(currentTab, true);
    });

    function syncSliderVal(val) {
      document.getElementById("simVal").textContent = parseInt(val).toLocaleString();
    }

    // Handles tab switching dynamically
    function switchTab(tabName, isInitial = false) {
      currentTab = tabName;

      // Update URL path seamlessly
      if (!isInitial) {
        window.history.pushState(null, '', '/' + (tabName === 'standings' ? 'standings' : tabName));
      }

      // Update Nav buttons
      document.querySelectorAll(".nav-btn").forEach(btn => btn.classList.remove("active"));
      const activeNav = document.getElementById("nav-" + tabName);
      if (activeNav) activeNav.classList.add("active");

      // Update visible sections
      document.querySelectorAll(".view-section").forEach(sec => sec.classList.remove("active"));
      const activeSec = document.getElementById(tabName + "-section");
      if (activeSec) activeSec.classList.add("active");

      // Load data
      loadData(tabName);
    }

    function handleLogoClick(e) {
      e.preventDefault();
      switchTab('standings');
    }

    window.onpopstate = () => {
      const path = window.location.pathname;
      let targetTab = "standings";
      if (path === "/simulation") targetTab = "simulation";
      else if (path === "/impact") targetTab = "impact";
      switchTab(targetTab, true);
    };

    // Client-side API fetch
    function loadData(tabName) {
      if (tabName === "standings") {
        fetchStandings();
      } else if (tabName === "simulation") {
        const simResults = document.getElementById("sim-results-card");
        if (simResults.style.display === "none") {
          fetchSimulation(50000); // Trigger a default run
        }
      } else if (tabName === "impact") {
        fetchImpact();
      }
    }

    function fetchStandings() {
      const container = document.getElementById("standings-container");
      container.innerHTML = `<div style="grid-column:1/-1;text-align:center;padding:4rem;"><div class="spinner" style="margin:0 auto 1rem auto"></div>Loading standings...</div>`;
      
      fetch("/api/standings")
        .then(res => res.json())
        .then(data => {
          container.innerHTML = "";
          data.divisions.forEach(div => {
            const card = document.createElement("div");
            card.className = "card";
            card.dataset.division = div.name.toLowerCase();

            let tableRows = "";
            div.teams.forEach((team, idx) => {
              const isLeader = idx === 0;
              const winPct = (team.win_pct * 100).toFixed(1) + "%";
              tableRows += `
                <tr class="${isLeader ? 'playoff-row-highlight' : ''}">
                  <td>
                    <span class="team-abbr-badge">${team.abbr}</span>
                  </td>
                  <td style="text-align:center">${team.wins}</td>
                  <td style="text-align:center">${team.losses}</td>
                  <td style="text-align:center">${team.ties}</td>
                  <td style="text-align:right;font-weight:600">${winPct}</td>
                </tr>
              `;
            });

            card.innerHTML = `
              <h3 class="card-title">
                <span>${div.name}</span>
                <span style="font-size:0.75rem;color:var(--success-color);font-weight:600">${div.teams[0].abbr} Leader</span>
              </h3>
              <table>
                <thead>
                  <tr>
                    <th>Team</th>
                    <th style="text-align:center">W</th>
                    <th style="text-align:center">L</th>
                    <th style="text-align:center">T</th>
                    <th style="text-align:right">Win %</th>
                  </tr>
                </thead>
                <tbody>
                  ${tableRows}
                </tbody>
              </table>
            `;
            container.appendChild(card);
          });
        })
        .catch(err => {
          container.innerHTML = `<div style="grid-column:1/-1;color:var(--danger-color);padding:2rem;">Failed to load standings data: ${err}</div>`;
        });
    }

    function runSimulation() {
      const sliderVal = document.getElementById("simSlider").value;
      fetchSimulation(sliderVal);
    }

    let currentSimView = "seeding";
    let lastSimData = null;

    function setSimView(view) {
      currentSimView = view;
      document.getElementById("toggle-seeding").classList.toggle("active", view === "seeding");
      document.getElementById("toggle-postseason").classList.toggle("active", view === "postseason");
      if (lastSimData) {
        renderSimulationRows();
      }
    }

    function renderSimulationRows() {
      const data = lastSimData;
      const tbody = document.getElementById("sim-tbody");
      const thead = document.querySelector("#sim-table thead tr");
      tbody.innerHTML = "";

      if (currentSimView === "seeding") {
        thead.innerHTML = `
          <th>Team</th>
          <th>Playoff %</th>
          <th>Division Leader %</th>
          <th>Wild Card %</th>
        `;

        const teams = data.teams.sort((a, b) => b.playoff - a.playoff);
        teams.forEach(team => {
          const playoffPctStr = (team.playoff * 100).toFixed(1) + "%";
          const divisionPctStr = (team.division * 100).toFixed(1) + "%";
          const wildcardPctStr = (team.wildcard * 100).toFixed(1) + "%";

          let progressClass = "prog-indigo";
          if (team.playoff >= 0.7) progressClass = "prog-emerald";
          else if (team.playoff <= 0.3) progressClass = "prog-rose";

          const row = document.createElement("tr");
          row.innerHTML = `
            <td>
              <span class="team-abbr-badge" style="font-size:0.9rem">${team.abbr}</span>
            </td>
            <td>
              <div class="progress-container">
                <div class="progress-bar-bg">
                  <div class="progress-bar-fill ${progressClass}" style="width: ${team.playoff * 100}%"></div>
                </div>
                <span style="font-weight:700;min-width:45px">${playoffPctStr}</span>
              </div>
            </td>
            <td>${divisionPctStr}</td>
            <td>${wildcardPctStr}</td>
          `;
          tbody.appendChild(row);
        });
      } else {
        thead.innerHTML = `
          <th>Team</th>
          <th>Lombardi Trophy Odds %</th>
          <th>Divisional Rd %</th>
          <th>Conf. Champ Rd %</th>
          <th>Super Bowl App %</th>
        `;

        const teams = data.teams.sort((a, b) => b.win_superbowl - a.win_superbowl);
        teams.forEach(team => {
          const winSuperbowlPctStr = (team.win_superbowl * 100).toFixed(1) + "%";
          const divisionalPctStr = (team.divisional * 100).toFixed(1) + "%";
          const confChampionshipPctStr = (team.conf_championship * 100).toFixed(1) + "%";
          const superbowlAppPctStr = (team.superbowl * 100).toFixed(1) + "%";

          let progressClass = "prog-indigo";
          if (team.win_superbowl >= 0.10) progressClass = "prog-emerald";
          else if (team.win_superbowl <= 0.01) progressClass = "prog-rose";

          const row = document.createElement("tr");
          row.innerHTML = `
            <td>
              <span class="team-abbr-badge" style="font-size:0.9rem">${team.abbr}</span>
            </td>
            <td>
              <div class="progress-container">
                <div class="progress-bar-bg">
                  <div class="progress-bar-fill ${progressClass}" style="width: ${team.win_superbowl * 100}%"></div>
                </div>
                <span style="font-weight:700;min-width:45px">${winSuperbowlPctStr}</span>
              </div>
            </td>
            <td>${divisionalPctStr}</td>
            <td>${confChampionshipPctStr}</td>
            <td>${superbowlAppPctStr}</td>
          `;
          tbody.appendChild(row);
        });
      }
    }

    function fetchSimulation(iterations) {
      const loading = document.getElementById("sim-loading");
      const resultsCard = document.getElementById("sim-results-card");
      
      loading.style.display = "flex";
      resultsCard.style.display = "none";

      fetch(`/api/simulation?iterations=${iterations}`)
        .then(res => res.json())
        .then(data => {
          loading.style.display = "none";
          resultsCard.style.display = "block";
          document.getElementById("sim-metadata").textContent = `${parseInt(data.iterations).toLocaleString()} runs completed`;

          lastSimData = data;
          renderSimulationRows();
        })
        .catch(err => {
          loading.style.display = "none";
          resultsCard.style.display = "block";
          document.getElementById("sim-tbody").innerHTML = `<tr><td colspan="5" style="color:var(--danger-color);text-align:center">Error running simulation: ${err}</td></tr>`;
        });
    }

    function fetchImpact() {
      const loading = document.getElementById("impact-loading");
      const container = document.getElementById("impact-results-container");

      loading.style.display = "flex";
      container.innerHTML = "";

      fetch("/api/impact?iterations=10000") // Run impact with moderate iterations for speed
        .then(res => res.json())
        .then(data => {
          loading.style.display = "none";
          if (!data.games || data.games.length === 0) {
            container.innerHTML = `<div style="grid-column:1/-1;text-align:center;padding:4rem;color:var(--text-secondary)">No unplayed games found in current week.</div>`;
            return;
          }

          data.games.forEach(game => {
            const card = document.createElement("div");
            card.className = "matchup-card";

            const hDelta = game.home_delta;
            const aDelta = game.away_delta;
            
            const hSign = hDelta >= 0 ? "+" : "";
            const aSign = aDelta >= 0 ? "+" : "";

            const hClass = hDelta > 0.01 ? "delta-positive" : (hDelta < -0.01 ? "delta-negative" : "delta-neutral");
            const aClass = aDelta > 0.01 ? "delta-positive" : (aDelta < -0.01 ? "delta-negative" : "delta-neutral");

            // Calculate importance rating from sum of absolute deltas
            const importanceScore = Math.abs(hDelta) + Math.abs(aDelta);
            let importanceText = "Low Impact";
            let importanceColor = "color:var(--text-secondary)";
            
            if (importanceScore > 0.15) {
              importanceText = "Critical Path Matchup";
              importanceColor = "color:var(--danger-color);text-shadow: 0 0 10px rgba(244,63,94,0.3)";
            } else if (importanceScore > 0.05) {
              importanceText = "High Importance";
              importanceColor = "color:var(--warning-color)";
            }

            card.innerHTML = `
              <div class="matchup-header">
                <span>NFL Matchup Leverage</span>
                <span class="impact-rating" style="${importanceColor}">${importanceText}</span>
              </div>
              <div class="matchup-body">
                <div class="matchup-team">
                  <span class="team-large-abbr">${game.home}</span>
                  <span style="font-size:0.75rem;color:var(--text-secondary);margin-top:0.25rem">Home</span>
                </div>
                <div class="vs-text">VS</div>
                <div class="matchup-team">
                  <span class="team-large-abbr">${game.away}</span>
                  <span style="font-size:0.75rem;color:var(--text-secondary);margin-top:0.25rem">Away</span>
                </div>
              </div>
              <div class="matchup-footer">
                <div class="progress-container">
                  <span style="font-size:0.75rem;color:var(--text-secondary)">Home Win Delta:</span>
                  <span class="delta-badge ${hClass}">${hSign}${(hDelta * 100).toFixed(1)}%</span>
                </div>
                <div class="progress-container">
                  <span style="font-size:0.75rem;color:var(--text-secondary)">Away Win Delta:</span>
                  <span class="delta-badge ${aClass}">${aSign}${(aDelta * 100).toFixed(1)}%</span>
                </div>
              </div>
            `;
            container.appendChild(card);
          });
        })
        .catch(err => {
          loading.style.display = "none";
          container.innerHTML = `<div style="grid-column:1/-1;color:var(--danger-color);padding:2rem;">Failed to load importance leverage analysis: ${err}</div>`;
        });
    }

    // Modal Control Functions
    function openUpdateModal() {
      document.getElementById("updateModal").style.display = "flex";
      document.getElementById("weekInput").focus();
    }

    function closeUpdateModal() {
      document.getElementById("updateModal").style.display = "none";
      document.getElementById("updateForm").reset();
    }

    function handleBackdropClick(e) {
      if (e.target.id === "updateModal") {
        closeUpdateModal();
      }
    }

    // Client-side record submitting
    function handleFormSubmit(e) {
      e.preventDefault();
      
      const form = document.getElementById("updateForm");
      const formData = new FormData(form);
      const urlEncoded = new URLSearchParams(formData).toString();

      fetch("/api/update-result", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded"
        },
        body: urlEncoded
      })
      .then(res => res.json())
      .then(data => {
        if (data.ok) {
          closeUpdateModal();
          showToast("Game score successfully persisted!");
          // Reload current active tab values dynamically!
          loadData(currentTab);
        } else {
          alert("Error: " + data.error);
        }
      })
      .catch(err => {
        alert("Failed to submit score: " + err);
      });
    }

    function showToast(msg) {
      const toast = document.getElementById("toast");
      document.getElementById("toast-message").textContent = msg;
      toast.classList.add("show");
      
      setTimeout(() => {
        toast.classList.remove("show");
      }, 3500);
    }

    // Dynamic Team Searching
    function filterTeams() {
      const query = document.getElementById("teamSearch").value.toLowerCase();
      const cards = document.querySelectorAll("#standings-container .card");

      cards.forEach(card => {
        const divName = card.dataset.division;
        const rows = card.querySelectorAll("tbody tr");
        let matchesInCard = 0;

        rows.forEach(row => {
          const teamAbbr = row.querySelector(".team-abbr-badge").textContent.toLowerCase();
          if (teamAbbr.includes(query) || divName.includes(query)) {
            row.style.display = "";
            matchesInCard++;
          } else {
            row.style.display = "none";
          }
        });

        // Toggle card visibility depending on if matches were found
        if (matchesInCard > 0 || query === "") {
          card.style.display = "";
        } else {
          card.style.display = "none";
        }
      });
    }
  </script>
</body>
</html>)rawhtml";
}

std::string WebServer::renderStandingsHtml() const {
    return buildDashboardHtml();
}

std::string WebServer::renderSimulationHtml(int iterations) const {
    return buildDashboardHtml();
}

std::string WebServer::renderImpactHtml(int iterations) const {
    return buildDashboardHtml();
}

std::string WebServer::standingsJson() const {
    Season current = season_;
    current.computeStandings();

    std::ostringstream out;
    out << "{\"divisions\":[";
    bool firstDivision = true;

    for (const auto& division : current.getDivisions()) {
        if (!firstDivision) {
            out << ',';
        }
        firstDivision = false;
        out << "{\"name\":\"" << jsonEscape(division) << "\",\"teams\":[";

        const auto teams = current.teamsByDivision(division);
        bool firstTeam = true;
        for (const auto* team : teams) {
            if (!firstTeam) {
                out << ',';
            }
            firstTeam = false;
            out << "{\"abbr\":\"" << jsonEscape(team->abbreviation())
                << "\",\"wins\":" << team->wins()
                << ",\"losses\":" << team->losses()
                << ",\"ties\":" << team->ties()
                << ",\"win_pct\":" << team->winPercentage() << '}';
        }

        out << "]}";
    }

    out << "]}";
    return out.str();
}

std::string WebServer::simulationJson(int iterations) const {
    MonteCarlo mc;
    mc.setModelParameters(homeAdvantage_, strengthWeight_);
    mc.loadHistoricalStrengths(season_, "data/historical");
    Season current = season_;
    current.computeStandings();
    const auto sim = mc.simulate(current, iterations, 12345);

    std::ostringstream out;
    out << "{\"iterations\":" << iterations << ",\"teams\":[";
    bool first = true;
    for (const auto& [abbr, prob] : sim.playoffProbability) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"abbr\":\"" << jsonEscape(abbr)
            << "\",\"playoff\":" << prob
            << ",\"division\":" << sim.divisionWinProbability.at(abbr)
            << ",\"wildcard\":" << sim.wildcardProbability.at(abbr)
            << ",\"divisional\":" << sim.makeDivisionalProbability.at(abbr)
            << ",\"conf_championship\":" << sim.makeConfChampionshipProbability.at(abbr)
            << ",\"superbowl\":" << sim.makeSuperBowlProbability.at(abbr)
            << ",\"win_superbowl\":" << sim.winSuperBowlProbability.at(abbr) << '}';
    }
    out << "]}";
    return out.str();
}

std::string WebServer::impactJson(int iterations) const {
    MonteCarlo mc;
    mc.setModelParameters(homeAdvantage_, strengthWeight_);
    mc.loadHistoricalStrengths(season_, "data/historical");
    Season current = season_;
    current.computeStandings();
    const auto impact = mc.analyzeImpact(current, iterations, 12345);

    std::ostringstream out;
    out << "{\"week\":" << impact.week << ",\"games\":[";
    bool first = true;
    for (const auto& gameImpact : impact.gameImpacts) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"home\":\"" << jsonEscape(gameImpact.homeTeam)
            << "\",\"away\":\"" << jsonEscape(gameImpact.awayTeam)
            << "\",\"home_delta\":" << gameImpact.homeDeltaPlayoffProb
            << ",\"away_delta\":" << gameImpact.awayDeltaPlayoffProb << '}';
    }
    out << "]}";
    return out.str();
}

bool WebServer::applyResultUpdate(const std::string& body, std::string& error) {
    const auto params = parseUrlEncoded(body);

    const auto getParam = [&](const std::string& key) -> std::string {
        const auto it = params.find(key);
        if (it == params.end()) {
            return "";
        }
        return it->second;
    };

    const std::string weekStr = getParam("week");
    const std::string homeTeam = getParam("home_team");
    const std::string awayTeam = getParam("away_team");
    const std::string homeScoreStr = getParam("home_score");
    const std::string awayScoreStr = getParam("away_score");

    if (weekStr.empty() || homeTeam.empty() || awayTeam.empty() ||
        homeScoreStr.empty() || awayScoreStr.empty()) {
        error = "Missing required fields";
        return false;
    }

    int week = 0;
    int homeScore = 0;
    int awayScore = 0;
    try {
        week = std::stoi(weekStr);
        homeScore = std::stoi(homeScoreStr);
        awayScore = std::stoi(awayScoreStr);
    } catch (const std::exception&) {
        error = "week/home_score/away_score must be integers";
        return false;
    }

    std::vector<Game> updatedGames;
    updatedGames.reserve(season_.allGames().size());

    bool replaced = false;
    for (const auto& game : season_.allGames()) {
        if (!replaced &&
            game.week() == week &&
            game.homeTeam() == homeTeam &&
            game.awayTeam() == awayTeam) {
            updatedGames.emplace_back(game.week(), game.date(), game.homeTeam(), game.awayTeam(),
                                      homeScore, awayScore, "final");
            replaced = true;
        } else {
            updatedGames.push_back(game);
        }
    }

    if (!replaced) {
        error = "No matching game found";
        return false;
    }

    season_.replaceGames(updatedGames);
    season_.computeStandings();
    if (!persistSchedule()) {
        error = "Failed to persist schedule CSV";
        return false;
    }

    return true;
}

bool WebServer::persistSchedule() const {
    CsvParser::Table table;
    table.reserve(season_.allGames().size());

    for (const auto& game : season_.allGames()) {
        CsvParser::Row row;
        row["week"] = std::to_string(game.week());
        row["date"] = game.date();
        row["home_team"] = game.homeTeam();
        row["away_team"] = game.awayTeam();
        row["home_score"] = std::to_string(game.homeScore());
        row["away_score"] = std::to_string(game.awayScore());
        row["status"] = game.status();
        table.push_back(row);
    }

    CsvParser::write(schedulePath_,
                     {"week", "date", "home_team", "away_team", "home_score", "away_score", "status"},
                     table);
    return true;
}

std::string WebServer::jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    for (char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }

    return out;
}

std::string WebServer::htmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    for (char c : value) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
                break;
        }
    }

    return out;
}

std::string WebServer::urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            const char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out += decoded;
            i += 2;
        } else if (value[i] == '+') {
            out += ' ';
        } else {
            out += value[i];
        }
    }

    return out;
}

int WebServer::parseIterations(const std::string& rawPath, int fallback) {
    const size_t queryPos = rawPath.find('?');
    if (queryPos == std::string::npos) {
        return fallback;
    }

    const std::string query = rawPath.substr(queryPos + 1);
    std::stringstream ss(query);
    std::string part;
    while (std::getline(ss, part, '&')) {
        const size_t eq = part.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = part.substr(0, eq);
        const std::string value = part.substr(eq + 1);
        if (key == "iterations") {
            try {
                const int parsed = std::stoi(value);
                if (parsed > 0) {
                    return parsed;
                }
            } catch (const std::exception&) {
                return fallback;
            }
        }
    }

    return fallback;
}

std::string WebServer::getPathOnly(const std::string& rawPath) {
    const size_t queryPos = rawPath.find('?');
    if (queryPos == std::string::npos) {
        return rawPath;
    }
    return rawPath.substr(0, queryPos);
}

std::string WebServer::buildHttpResponse(int statusCode,
                                         const std::string& contentType,
                                         const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << ' ' << statusText(statusCode) << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}
