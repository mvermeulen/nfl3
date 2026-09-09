#include "WebServer.h"

#include "model/MonteCarlo.h"
#include "util/CsvParser.h"
#include "app/CommandSupport.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

static std::string buildDashboardHtml();

WebServer::WebServer(Season season,
                     const std::string& schedulePath,
                     double homeAdvantage,
                     double strengthWeight,
                     int defaultIterations)
    : season_(std::move(season)),
      schedulePath_(schedulePath),
      homeAdvantage_(homeAdvantage),
      strengthWeight_(strengthWeight),
      defaultIterations_(defaultIterations) {
    std::string historyPath = "data/probability_history.csv";
    std::ifstream f(historyPath.c_str());
    if (!f.good()) {
        std::cout << "Probability history file not found. Building it on startup..." << std::endl;
        rebuildProbabilityHistory(10000); // 10k is fast for startup baseline
    } else {
        std::cout << "Using existing probability history file." << std::endl;
    }
}

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
    if (method == "GET" && path == "/sandbox") {
        contentType = "text/html; charset=utf-8";
        return renderSimulationHtml(defaultIterations_);
    }
    if (method == "GET" && path == "/games") {
        contentType = "text/html; charset=utf-8";
        return buildDashboardHtml();
    }
    if (method == "GET" && (path == "/teams" || path.rfind("/teams/", 0) == 0)) {
        contentType = "text/html; charset=utf-8";
        return buildDashboardHtml();
    }
    if (method == "GET" && path == "/history") {
        contentType = "text/html; charset=utf-8";
        return buildDashboardHtml();
    }
    if (method == "GET" && path == "/api/standings") {
        contentType = "application/json; charset=utf-8";
        return standingsJson();
    }
    if (method == "GET" && path == "/api/games") {
        contentType = "application/json; charset=utf-8";
        std::ostringstream out;
        out << "{\"games\":[";
        bool first = true;
        for (const auto& game : season_.allGames()) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << "{\"week\":" << game.week()
                << ",\"date\":\"" << jsonEscape(game.date())
                << "\",\"home_team\":\"" << jsonEscape(game.homeTeam())
                << "\",\"away_team\":\"" << jsonEscape(game.awayTeam())
                << "\",\"home_score\":" << game.homeScore()
                << ",\"away_score\":" << game.awayScore()
                << ",\"status\":\"" << jsonEscape(game.status()) << "\"}";
        }
        out << "]}";
        return out.str();
    }
    if (path == "/api/simulation") {
        if (method != "GET" && method != "POST") {
            statusCode = 405;
            contentType = "application/json; charset=utf-8";
            return "{\"error\":\"GET or POST required\"}";
        }
        
        contentType = "application/json; charset=utf-8";
        int iterations = defaultIterations_;
        std::string locksStr = "";
        
        if (method == "POST") {
            const auto params = parseUrlEncoded(body);
            auto it = params.find("iterations");
            if (it != params.end()) {
                try {
                    iterations = std::stoi(it->second);
                } catch (...) {}
            }
            it = params.find("locks");
            if (it != params.end()) {
                locksStr = it->second;
            }
        } else {
            iterations = parseIterations(rawPath, defaultIterations_);
            const size_t queryPos = rawPath.find('?');
            if (queryPos != std::string::npos) {
                const std::string query = rawPath.substr(queryPos + 1);
                std::stringstream ss(query);
                std::string part;
                while (std::getline(ss, part, '&')) {
                    const size_t eq = part.find('=');
                    if (eq != std::string::npos) {
                        const std::string key = part.substr(0, eq);
                        const std::string value = part.substr(eq + 1);
                        if (key == "locks") {
                            locksStr = urlDecodeLocal(value);
                        }
                    }
                }
            }
        }
        
        return simulationJson(iterations, locksStr);
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
    if (path == "/api/fetch-live") {
        if (method != "POST") {
            statusCode = 405;
            contentType = "application/json; charset=utf-8";
            return "{\"error\":\"POST required\"}";
        }

        std::string pythonCmd = "python3";
        if (std::filesystem::exists(".venv/bin/python")) {
            pythonCmd = ".venv/bin/python";
        }
        std::string scriptPath = "scripts/fetch_live_scores.py";
        std::string fullCmd = pythonCmd + " " + scriptPath + " --all --schedule " + schedulePath_;

        int status = std::system(fullCmd.c_str());
        if (status != 0) {
            statusCode = 500;
            contentType = "application/json; charset=utf-8";
            return "{\"ok\":false,\"error\":\"Score ingestion failed\"}";
        }

        // Reload season schedule and standings
        try {
            season_ = nfl3::loadSeasonFromCsvFiles("data/teams.csv", schedulePath_);
            season_.computeStandings();
        } catch (const std::exception& e) {
            statusCode = 500;
            contentType = "application/json; charset=utf-8";
            return std::string("{\"ok\":false,\"error\":\"Failed to reload season standings: ") + jsonEscape(e.what()) + "\"}";
        }

        contentType = "application/json; charset=utf-8";
        return "{\"ok\":true,\"message\":\"Successfully synchronized live scores.\"}";
    }
    if (method == "GET" && path == "/api/probability-history") {
        contentType = "application/json; charset=utf-8";
        try {
            auto table = CsvParser::parse("data/probability_history.csv");
            std::ostringstream out;
            out << "[";
            bool first = true;
            for (const auto& row : table) {
                if (!first) out << ",";
                first = false;
                out << "{\"week\":" << row.at("week")
                    << ",\"team\":\"" << jsonEscape(row.at("team")) << "\""
                    << ",\"playoff_prob\":" << row.at("playoff_prob")
                    << ",\"superbowl_prob\":" << row.at("superbowl_prob") << "}";
            }
            out << "]";
            return out.str();
        } catch (const std::exception& e) {
            statusCode = 500;
            contentType = "application/json; charset=utf-8";
            return "{\"error\":\"Failed to load probability history: " + std::string(e.what()) + "\"}";
        }
    }
    if (path == "/api/rebuild-probability-history") {
        if (method != "POST") {
            statusCode = 405;
            contentType = "application/json; charset=utf-8";
            return "{\"error\":\"POST required\"}";
        }
        contentType = "application/json; charset=utf-8";
        try {
            rebuildProbabilityHistory(100000);
            return "{\"ok\":true}";
        } catch (const std::exception& e) {
            statusCode = 500;
            contentType = "application/json; charset=utf-8";
            return "{\"ok\":false,\"error\":\"Failed to rebuild probability history: " + std::string(e.what()) + "\"}";
        }
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

    @keyframes spin {
      from { transform: rotate(0deg); }
      to { transform: rotate(360deg); }
    }
    .spinning {
      animation: spin 1s linear infinite !important;
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

    a.team-abbr-badge,
    a.team-large-abbr,
    a.week-link {
      color: inherit;
      text-decoration: none;
      cursor: pointer;
      transition: background 0.15s ease, color 0.15s ease;
    }

    a.team-abbr-badge:hover {
      background: rgba(99, 102, 241, 0.22);
      color: #c7d2fe;
    }

    a.team-large-abbr:hover,
    a.week-link:hover {
      color: #a5b4fc;
    }

    .week-link {
      font-weight: 700;
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

    /* What-If Sandbox Specific Styles */
    .sandbox-grid {
      display: grid;
      grid-template-columns: 1fr 1.15fr;
      gap: 1.5rem;
      align-items: start;
    }
    @media (max-width: 900px) {
      .sandbox-grid {
        grid-template-columns: 1fr;
      }
    }
    .sandbox-games-list {
      display: flex;
      flex-direction: column;
      gap: 1rem;
      max-height: 70vh;
      overflow-y: auto;
      padding-right: 0.5rem;
      margin-top: 0.5rem;
    }
    .sandbox-game-card {
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 1rem;
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
      transition: all 0.2s ease;
    }
    .sandbox-game-card:hover {
      border-color: rgba(99, 102, 241, 0.3);
      background: rgba(255, 255, 255, 0.04);
    }
    .sandbox-game-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 0.8rem;
      color: var(--text-secondary);
      font-family: var(--font-display);
      font-weight: 600;
    }
    .sandbox-team-row {
      display: flex;
      gap: 0.75rem;
    }
    .sandbox-team-btn {
      flex: 1;
      background: rgba(0, 0, 0, 0.25);
      border: 1px solid var(--border-color);
      color: var(--text-secondary);
      padding: 0.6rem 0.8rem;
      border-radius: 8px;
      font-family: var(--font-display);
      font-weight: 700;
      cursor: pointer;
      transition: all 0.2s ease;
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 0.9rem;
    }
    .sandbox-team-btn:hover:not(:disabled) {
      border-color: var(--accent-color);
      color: var(--text-primary);
      background: rgba(99, 102, 241, 0.08);
    }
    .sandbox-team-btn.active {
      background: var(--accent-gradient);
      border-color: transparent;
      color: white;
      box-shadow: var(--accent-glow);
    }
    .sandbox-team-btn:disabled {
      opacity: 0.6;
      cursor: not-allowed;
    }
    .sandbox-header-controls {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 1.25rem;
      flex-wrap: wrap;
      gap: 0.75rem;
    }
    .sandbox-badge {
      background: rgba(99, 102, 241, 0.15);
      color: #a5b4fc;
      padding: 0.35rem 0.75rem;
      border-radius: 6px;
      font-size: 0.85rem;
      font-family: var(--font-display);
      font-weight: 600;
      border: 1px solid rgba(99, 102, 241, 0.3);
    }
  </style>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
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
        <button id="nav-games" class="nav-btn" onclick="switchTab('games')">Games</button>
        <button id="nav-teams" class="nav-btn" onclick="teamsSelectedAbbr = null; switchTab('teams')">Teams</button>
        <button id="nav-simulation" class="nav-btn" onclick="switchTab('simulation')">Playoff Sim</button>
        <button id="nav-impact" class="nav-btn" onclick="switchTab('impact')">Game Importance</button>
        <button id="nav-sandbox" class="nav-btn" onclick="switchTab('sandbox')">What-If Sandbox</button>
        <button id="nav-history" class="nav-btn" onclick="switchTab('history')">Contender History</button>
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

      <div class="control-panel" style="display:flex;gap:1rem;align-items:center;flex-wrap:wrap;margin-bottom:1.5rem">
        <div class="search-wrapper" style="flex:1;min-width:250px">
          <input type="text" id="teamSearch" class="form-input" placeholder="Search by team or division..." oninput="filterTeams()">
        </div>
        <button id="sync-scores-btn" class="btn btn-secondary" onclick="syncLiveScores()" style="background:rgba(99,102,241,0.06);border:1px solid rgba(99,102,241,0.25);color:#a5b4fc">
          <svg id="sync-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" style="transition:transform 0.5s ease"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
          <span id="sync-btn-text">Sync Live Scores</span>
        </button>
      </div>

      <div id="standings-container" class="division-grid">
        <!-- Division Cards loaded dynamically -->
      </div>
    </section>

    <!-- GAMES SECTION -->
    <section id="games-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Schedule &amp; Results</h1>
          <p class="page-desc">Browse each week's matchups, scores, and status.</p>
        </div>
      </div>

      <div class="control-panel" style="display:flex;gap:1rem;align-items:center;flex-wrap:wrap;margin-bottom:1.5rem">
        <div style="display:flex;align-items:center;gap:0.75rem">
          <span class="form-label" style="margin-bottom:0">Week:</span>
          <select id="gamesWeekSelect" class="form-input" style="width:auto;padding:0.4rem 2rem 0.4rem 1rem" onchange="changeGamesWeek(this.value)">
            <!-- Dynamically populated weeks -->
          </select>
        </div>
      </div>

      <div class="card">
        <table id="games-table">
          <thead>
            <tr>
              <th>Date</th>
              <th>Away</th>
              <th>Home</th>
              <th>Score</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody id="games-tbody">
            <!-- Dynamically populated rows -->
          </tbody>
        </table>
      </div>
    </section>

    <!-- TEAMS SECTION -->
    <section id="teams-section" class="view-section">
      <div id="teams-list-view">
        <div class="dashboard-header">
          <div>
            <h1 class="page-title">Teams</h1>
            <p class="page-desc">All 32 franchises, grouped by conference and division. Select a team for its full schedule.</p>
          </div>
        </div>

        <div id="teams-list-container" class="division-grid">
          <!-- Dynamically populated team cards -->
        </div>
      </div>

      <div id="team-detail-view" style="display:none">
        <div class="dashboard-header">
          <div>
            <h1 id="team-detail-title" class="page-title">Team</h1>
            <p id="team-detail-desc" class="page-desc"></p>
          </div>
          <button class="btn btn-secondary" onclick="teamsSelectedAbbr = null; switchTab('teams')">
            &larr; All Teams
          </button>
        </div>

        <div class="card" style="margin-bottom:1.5rem">
          <h3 class="card-title" style="margin-bottom:1rem">Record</h3>
          <table>
            <thead>
              <tr>
                <th>W</th>
                <th>L</th>
                <th>T</th>
                <th>Win %</th>
              </tr>
            </thead>
            <tbody id="team-detail-record">
              <!-- Dynamically populated -->
            </tbody>
          </table>
        </div>

        <div class="card">
          <h3 class="card-title">
            <span>Full Schedule</span>
            <button id="team-importance-btn" class="btn btn-secondary" style="font-size:0.8rem;padding:0.4rem 0.8rem" onclick="calculateTeamImportance()">
              Calculate Importance
            </button>
          </h3>
          <table>
            <thead>
              <tr>
                <th>Week</th>
                <th>Date</th>
                <th>Opponent</th>
                <th>Score</th>
                <th>Status</th>
                <th>Importance</th>
              </tr>
            </thead>
            <tbody id="team-detail-schedule">
              <!-- Dynamically populated -->
            </tbody>
          </table>
        </div>
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

    <!-- SANDBOX SECTION -->
    <section id="sandbox-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">What-If Playoff Sandbox</h1>
          <p class="page-desc">Toggle hypothetical winners for upcoming games to instantly calculate simulated odds changes.</p>
        </div>
        <button class="btn btn-secondary" onclick="clearSandboxPicks()" style="background:rgba(244,63,94,0.06);border:1px solid rgba(244,63,94,0.25);color:#fda4af">
          Clear All Picks
        </button>
      </div>

      <div class="sandbox-grid">
        <!-- Left column: Game list by week -->
        <div class="card">
          <div class="sandbox-header-controls">
            <div style="display:flex;align-items:center;gap:0.75rem">
              <span class="form-label" style="margin-bottom:0">Week:</span>
              <select id="sandboxWeekSelect" class="form-input" style="width:auto;padding:0.4rem 2rem 0.4rem 1rem" onchange="changeSandboxWeek(this.value)">
                <!-- Dynamically populated weeks -->
              </select>
            </div>
            <span id="sandboxPickCount" class="sandbox-badge">0 Picks Locked</span>
          </div>

          <div id="sandbox-games-container" class="sandbox-games-list">
            <!-- Game cards with winner toggle buttons -->
          </div>
        </div>

        <!-- Right column: Standings/Odds panel -->
        <div class="card" style="position:relative">
          <!-- Spinner overlay inside card -->
          <div id="sandbox-spinner" class="loading-overlay" style="position:absolute;top:0;left:0;width:100%;height:100%;background:rgba(22,29,48,0.85);backdrop-filter:blur(4px);z-index:10;border-radius:16px;display:none;justify-content:center;align-items:center">
            <div style="text-align:center">
              <div class="spinner" style="margin:0 auto 1rem auto"></div>
              <p class="loading-text" style="font-size:1rem">Recalculating Playoff Standings...</p>
            </div>
          </div>

          <div class="card-title">
            <span>Simulated Odds Standings</span>
            <span style="font-size:0.8rem;font-weight:500;color:var(--text-secondary)">2,000 Monte Carlo Iterations</span>
          </div>

          <div class="segmented-control" style="display:inline-flex;background:rgba(255,255,255,0.03);border:1px solid var(--border-color);border-radius:8px;padding:0.25rem;gap:0.25rem;margin:1rem 0;">
            <button id="sandbox-toggle-seeding" class="nav-btn active" onclick="setSandboxView('seeding')" style="font-size:0.8rem;padding:0.35rem 0.75rem">
              Regular Seeding Odds
            </button>
            <button id="sandbox-toggle-postseason" class="nav-btn" onclick="setSandboxView('postseason')" style="font-size:0.8rem;padding:0.35rem 0.75rem">
              Postseason Tournament Odds
            </button>
          </div>

          <table id="sandbox-table">
            <thead>
              <!-- Headers will change dynamically -->
            </thead>
            <tbody id="sandbox-tbody">
              <!-- Dynamically populated rows with odds changes -->
            </tbody>
          </table>
        </div>
      </div>
    </section>

    <!-- HISTORY SECTION -->
    <section id="history-section" class="view-section">
      <div class="dashboard-header">
        <div>
          <h1 class="page-title">Contender Probability History</h1>
          <p class="page-desc">Track how each team's probability of reaching the playoffs or winning the Super Bowl shifts week-by-week.</p>
        </div>
        <div style="display: flex; gap: 1rem; align-items: center; flex-wrap: wrap;">
          <div id="history-rebuild-status" style="font-size: 0.9rem; color: var(--text-secondary);"></div>
          <button id="btn-rebuild-history" class="btn btn-primary" onclick="rebuildHistoryData()">
            <span id="rebuild-btn-text">Rebuild History (100k Sims)</span>
          </button>
        </div>
      </div>
      
      <div class="control-panel" style="display: flex; gap: 1.5rem; align-items: center; justify-content: space-between; flex-wrap: wrap; margin-bottom: 1.5rem;">
        <div class="segmented-control" style="display:inline-flex; background:rgba(255,255,255,0.03); border:1px solid var(--border-color); border-radius:8px; padding:0.25rem; gap:0.25rem;">
          <button id="metric-playoffs-btn" class="nav-btn active" onclick="switchHistoryMetric('playoff')" style="font-size:0.85rem; padding:0.4rem 0.85rem">
            Playoff Odds
          </button>
          <button id="metric-superbowl-btn" class="nav-btn" onclick="switchHistoryMetric('superbowl')" style="font-size:0.85rem; padding:0.4rem 0.85rem">
            Super Bowl Odds
          </button>
        </div>
        
        <div style="display: flex; gap: 0.5rem; flex-wrap: wrap;">
          <button class="btn btn-secondary" style="padding: 0.4rem 0.8rem; font-size: 0.8rem;" onclick="selectQuickHistoryGroup('top5')">Reset to Top 5</button>
          <button class="btn btn-secondary" style="padding: 0.4rem 0.8rem; font-size: 0.8rem;" onclick="selectQuickHistoryGroup('afc')">Select AFC</button>
          <button class="btn btn-secondary" style="padding: 0.4rem 0.8rem; font-size: 0.8rem;" onclick="selectQuickHistoryGroup('nfc')">Select NFC</button>
          <button class="btn btn-secondary" style="padding: 0.4rem 0.8rem; font-size: 0.8rem;" onclick="selectQuickHistoryGroup('all')">Select All</button>
          <button class="btn btn-secondary" style="padding: 0.4rem 0.8rem; font-size: 0.8rem;" onclick="selectQuickHistoryGroup('none')">Clear</button>
        </div>
      </div>

      <div class="card" style="padding: 1.5rem; margin-bottom: 2rem;">
        <div style="position: relative; height: 450px; width: 100%; margin-bottom: 1.5rem;">
          <canvas id="history-chart"></canvas>
        </div>
      </div>

      <div class="card" style="padding: 1.5rem;">
        <h3 class="card-title" style="margin-bottom: 1rem;">Filter Teams</h3>
        <div id="history-teams-container" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 1.5rem;">
          <!-- Will be dynamically populated with conference/division sections -->
        </div>
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

    // Games tab state variables
    let gamesData = [];
    let gamesSelectedWeek = null; // null = not yet determined (defaults to first unplayed week)
    let gamesWeekExplicit = false; // true once a week is chosen deliberately (dropdown or a team-page link)

    // Teams tab state variables
    let teamsListData = null;
    let teamsSelectedAbbr = null; // set for a /teams/<ABBR> detail view; null for the team list

    // Sandbox state variables
    let sandboxGames = [];
    let sandboxBaseOdds = null;
    let sandboxLocks = {}; // Key: "week:home:away", Value: "home" | "away"
    let sandboxTabMode = "seeding"; // seeding or postseason
    let sandboxSelectedWeek = 1;

    // History state variables
    let historyChart = null;
    let historyRawData = [];
    let historyMetric = "playoff"; // "playoff" | "superbowl"
    let historySelectedTeams = new Set();

    document.addEventListener("DOMContentLoaded", () => {
      // Sync numerical slider value formatted
      syncSliderVal(document.getElementById("simSlider").value);

      currentTab = resolveRouteFromLocation();
      switchTab(currentTab, true);
    });

    function syncSliderVal(val) {
      document.getElementById("simVal").textContent = parseInt(val).toLocaleString();
    }

    // Reads window.location (path + query) and applies it to the relevant tab
    // state, returning which tab it resolves to. Shared by the initial load
    // and back/forward navigation.
    function resolveRouteFromLocation() {
      const path = window.location.pathname;

      if (path === "/games") {
        const weekParam = new URLSearchParams(window.location.search).get("week");
        if (weekParam !== null) {
          const parsedWeek = parseInt(weekParam, 10);
          if (!isNaN(parsedWeek)) {
            gamesSelectedWeek = parsedWeek;
            gamesWeekExplicit = true;
          }
        }
        return "games";
      }
      if (path === "/teams") {
        teamsSelectedAbbr = null;
        return "teams";
      }
      if (path.indexOf("/teams/") === 0) {
        teamsSelectedAbbr = decodeURIComponent(path.substring("/teams/".length));
        return "teams";
      }
      if (path === "/simulation") return "simulation";
      if (path === "/impact") return "impact";
      if (path === "/sandbox") return "sandbox";
      if (path === "/history") return "history";
      return "standings";
    }

    // Handles tab switching dynamically
    function switchTab(tabName, isInitial = false) {
      currentTab = tabName;

      // Update URL path seamlessly
      if (!isInitial) {
        let url = '/' + (tabName === 'standings' ? 'standings' : tabName);
        if (tabName === 'teams' && teamsSelectedAbbr) {
          url = '/teams/' + teamsSelectedAbbr;
        } else if (tabName === 'games' && gamesWeekExplicit && gamesSelectedWeek) {
          url = '/games?week=' + gamesSelectedWeek;
        }
        window.history.pushState(null, '', url);
      }

      activateSection(tabName);
      loadData(tabName);
    }

    // Shows the nav button + view-section for a tab without touching the URL.
    function activateSection(tabName) {
      document.querySelectorAll(".nav-btn").forEach(btn => btn.classList.remove("active"));
      const activeNav = document.getElementById("nav-" + tabName);
      if (activeNav) activeNav.classList.add("active");

      document.querySelectorAll(".view-section").forEach(sec => sec.classList.remove("active"));
      const activeSec = document.getElementById(tabName + "-section");
      if (activeSec) activeSec.classList.add("active");
    }

    function handleLogoClick(e) {
      e.preventDefault();
      switchTab('standings');
    }

    window.onpopstate = () => {
      switchTab(resolveRouteFromLocation(), true);
    };

    // Navigates to a team's detail page (used by team-name links throughout the app).
    function navigateToTeam(abbr) {
      teamsSelectedAbbr = abbr;
      switchTab('teams');
    }

    // Navigates to the Games page pre-filtered to a specific week (used by
    // week-number links on a team's schedule).
    function navigateToGamesWeek(week) {
      gamesSelectedWeek = week;
      gamesWeekExplicit = true;
      switchTab('games');
    }

    // Renders a team abbreviation as a link to its team page.
    function teamLink(abbr, cssClass = "team-abbr-badge", style = "") {
      const styleAttr = style ? ` style="${style}"` : "";
      return `<a href="/teams/${abbr}" class="${cssClass}"${styleAttr} onclick="event.preventDefault(); navigateToTeam('${abbr}')">${abbr}</a>`;
    }

    // Client-side API fetch
    function loadData(tabName) {
      if (tabName === "standings") {
        fetchStandings();
      } else if (tabName === "games") {
        fetchGames();
      } else if (tabName === "teams") {
        fetchTeamsTab();
      } else if (tabName === "simulation") {
        const simResults = document.getElementById("sim-results-card");
        if (simResults.style.display === "none") {
          fetchSimulation(50000); // Trigger a default run
        }
      } else if (tabName === "impact") {
        fetchImpact();
      } else if (tabName === "sandbox") {
        fetchSandbox();
      } else if (tabName === "history") {
        fetchHistory();
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
                    ${teamLink(team.abbr)}
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
              ${teamLink(team.abbr, "team-abbr-badge", "font-size:0.9rem")}
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
              ${teamLink(team.abbr, "team-abbr-badge", "font-size:0.9rem")}
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
                  ${teamLink(game.home, "team-large-abbr")}
                  <span style="font-size:0.75rem;color:var(--text-secondary);margin-top:0.25rem">Home</span>
                </div>
                <div class="vs-text">VS</div>
                <div class="matchup-team">
                  ${teamLink(game.away, "team-large-abbr")}
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

    function syncLiveScores() {
      const btn = document.getElementById("sync-scores-btn");
      const icon = document.getElementById("sync-icon");
      const btnText = document.getElementById("sync-btn-text");

      btn.disabled = true;
      icon.classList.add("spinning");
      btnText.textContent = "Syncing...";

      fetch("/api/fetch-live", { method: "POST" })
        .then(res => res.json())
        .then(data => {
          btn.disabled = false;
          icon.classList.remove("spinning");
          btnText.textContent = "Sync Live Scores";
          if (data.ok) {
            showToast("Standings synchronized with live NFL scores!");
            loadData(currentTab);
          } else {
            alert("Sync failed: " + data.error);
          }
        })
        .catch(err => {
          btn.disabled = false;
          icon.classList.remove("spinning");
          btnText.textContent = "Sync Live Scores";
          alert("Connection error: " + err);
        });
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

    // What-If Scenario Sandbox Logic
    let lastSandboxSimData = null;

    function fetchGames() {
      if (gamesData.length > 0) {
        document.getElementById("gamesWeekSelect").value = gamesSelectedWeek;
        renderGamesTable();
        return;
      }

      document.getElementById("games-tbody").innerHTML = `
        <tr><td colspan="5" style="text-align:center;padding:4rem;">
          <div class="spinner" style="margin:0 auto 1rem auto"></div>
          Loading schedule...
        </td></tr>
      `;

      fetch("/api/games")
        .then(res => res.json())
        .then(data => {
          gamesData = data.games;

          const weekSelect = document.getElementById("gamesWeekSelect");
          weekSelect.innerHTML = "";
          const uniqueWeeks = [...new Set(gamesData.map(g => g.week))].sort((a, b) => a - b);
          uniqueWeeks.forEach(w => {
            const opt = document.createElement("option");
            opt.value = w;
            opt.textContent = "Week " + w;
            weekSelect.appendChild(opt);
          });

          if (!gamesWeekExplicit || gamesSelectedWeek === null || !uniqueWeeks.includes(gamesSelectedWeek)) {
            const firstUnplayedGame = gamesData.find(g => g.status !== "final" && g.status !== "in_progress");
            gamesSelectedWeek = firstUnplayedGame ? firstUnplayedGame.week : (uniqueWeeks[0] || 1);
          }
          weekSelect.value = gamesSelectedWeek;

          renderGamesTable();
        })
        .catch(err => {
          document.getElementById("games-tbody").innerHTML = `
            <tr><td colspan="5" style="color:var(--danger-color);text-align:center">Failed to load schedule: ${err}</td></tr>
          `;
        });
    }

    function changeGamesWeek(week) {
      gamesSelectedWeek = parseInt(week);
      gamesWeekExplicit = true;
      renderGamesTable();
    }

    function renderGamesTable() {
      const tbody = document.getElementById("games-tbody");
      tbody.innerHTML = "";

      const games = gamesData
        .filter(g => g.week === gamesSelectedWeek)
        .sort((a, b) => a.date.localeCompare(b.date));

      if (games.length === 0) {
        tbody.innerHTML = `<tr><td colspan="5" style="text-align:center;padding:2rem;color:var(--text-secondary)">No games in this week.</td></tr>`;
        return;
      }

      games.forEach(game => {
        const played = game.status === "final" || game.status === "in_progress";
        const score = played ? `${game.away_score} - ${game.home_score}` : "-";
        const statusColor = game.status === "final" ? "var(--success-color)"
          : game.status === "in_progress" ? "var(--warning-color)"
          : "var(--text-secondary)";

        const row = document.createElement("tr");
        row.innerHTML = `
          <td>${game.date}</td>
          <td>${teamLink(game.away_team)}</td>
          <td>${teamLink(game.home_team)}</td>
          <td>${score}</td>
          <td style="color:${statusColor};font-weight:600;text-transform:capitalize">${game.status}</td>
        `;
        tbody.appendChild(row);
      });
    }

    function fetchTeamsTab() {
      if (teamsSelectedAbbr) {
        showTeamDetail(teamsSelectedAbbr);
      } else {
        showTeamsList();
      }
    }

    function showTeamsList() {
      document.getElementById("teams-list-view").style.display = "block";
      document.getElementById("team-detail-view").style.display = "none";

      if (teamsListData) {
        renderTeamsList();
        return;
      }

      const container = document.getElementById("teams-list-container");
      container.innerHTML = `<div style="grid-column:1/-1;text-align:center;padding:4rem;"><div class="spinner" style="margin:0 auto 1rem auto"></div>Loading teams...</div>`;

      fetch("/api/standings")
        .then(res => res.json())
        .then(data => {
          teamsListData = data;
          renderTeamsList();
        })
        .catch(err => {
          container.innerHTML = `<div style="grid-column:1/-1;color:var(--danger-color);padding:2rem;">Failed to load teams: ${err}</div>`;
        });
    }

    function renderTeamsList() {
      const container = document.getElementById("teams-list-container");
      container.innerHTML = "";

      teamsListData.divisions.forEach(div => {
        const card = document.createElement("div");
        card.className = "card";

        let rows = "";
        div.teams.forEach(team => {
          rows += `
            <tr>
              <td>${teamLink(team.abbr)}</td>
              <td>${team.full_name}</td>
              <td style="text-align:right;color:var(--text-secondary)">${team.wins}-${team.losses}${team.ties ? '-' + team.ties : ''}</td>
            </tr>
          `;
        });

        card.innerHTML = `
          <h3 class="card-title" style="margin-bottom:1rem"><span>${div.name}</span></h3>
          <table>
            <tbody>${rows}</tbody>
          </table>
        `;
        container.appendChild(card);
      });
    }

    function showTeamDetail(abbr) {
      document.getElementById("teams-list-view").style.display = "none";
      document.getElementById("team-detail-view").style.display = "block";

      document.getElementById("team-detail-title").textContent = abbr;
      document.getElementById("team-detail-desc").textContent = "";
      document.getElementById("team-detail-record").innerHTML = `<tr><td colspan="4" style="text-align:center;padding:2rem;"><div class="spinner" style="margin:0 auto"></div></td></tr>`;
      document.getElementById("team-detail-schedule").innerHTML = `<tr><td colspan="6" style="text-align:center;padding:2rem;"><div class="spinner" style="margin:0 auto"></div></td></tr>`;

      const importanceBtn = document.getElementById("team-importance-btn");
      importanceBtn.disabled = false;
      importanceBtn.textContent = "Calculate Importance";

      Promise.all([
        fetch("/api/standings").then(res => res.json()),
        fetch("/api/games").then(res => res.json())
      ]).then(([standings, gamesResp]) => {
        let teamMeta = null;
        let divisionName = "";
        standings.divisions.forEach(div => {
          div.teams.forEach(t => {
            if (t.abbr === abbr) {
              teamMeta = t;
              divisionName = div.name;
            }
          });
        });

        if (!teamMeta) {
          document.getElementById("team-detail-title").textContent = "Team not found";
          document.getElementById("team-detail-record").innerHTML = "";
          document.getElementById("team-detail-schedule").innerHTML = `<tr><td colspan="6" style="text-align:center;color:var(--danger-color);padding:2rem;">Unknown team "${abbr}".</td></tr>`;
          return;
        }

        document.getElementById("team-detail-title").textContent = teamMeta.full_name;
        document.getElementById("team-detail-desc").textContent = `${divisionName} — ${abbr}`;

        const winPctStr = (teamMeta.win_pct * 100).toFixed(1) + "%";
        document.getElementById("team-detail-record").innerHTML = `
          <tr>
            <td>${teamMeta.wins}</td>
            <td>${teamMeta.losses}</td>
            <td>${teamMeta.ties}</td>
            <td>${winPctStr}</td>
          </tr>
        `;

        const teamGames = gamesResp.games
          .filter(g => g.home_team === abbr || g.away_team === abbr)
          .sort((a, b) => a.week - b.week);

        const scheduleTbody = document.getElementById("team-detail-schedule");
        scheduleTbody.innerHTML = "";

        if (teamGames.length === 0) {
          scheduleTbody.innerHTML = `<tr><td colspan="6" style="text-align:center;padding:2rem;color:var(--text-secondary)">No games scheduled.</td></tr>`;
          return;
        }

        teamGames.forEach(game => {
          const isHome = game.home_team === abbr;
          const opponent = isHome ? game.away_team : game.home_team;
          const played = game.status === "final" || game.status === "in_progress";
          const score = played ? `${game.away_score} - ${game.home_score}` : "-";
          const statusColor = game.status === "final" ? "var(--success-color)"
            : game.status === "in_progress" ? "var(--warning-color)"
            : "var(--text-secondary)";

          const row = document.createElement("tr");
          row.dataset.week = game.week;
          row.dataset.home = game.home_team;
          row.dataset.away = game.away_team;
          row.dataset.played = played;
          row.innerHTML = `
            <td><a class="week-link" href="/games?week=${game.week}" onclick="event.preventDefault(); navigateToGamesWeek(${game.week})">${game.week}</a></td>
            <td>${game.date}</td>
            <td>${isHome ? 'vs' : '@'} ${teamLink(opponent)}</td>
            <td>${score}</td>
            <td style="color:${statusColor};font-weight:600;text-transform:capitalize">${game.status}</td>
            <td class="importance-cell" style="color:var(--text-secondary)">${played ? '—' : ''}</td>
          `;
          scheduleTbody.appendChild(row);
        });
      }).catch(err => {
        document.getElementById("team-detail-schedule").innerHTML = `<tr><td colspan="6" style="color:var(--danger-color);text-align:center">Failed to load team schedule: ${err}</td></tr>`;
      });
    }

    // Runs two locked simulations per unplayed game on a team's schedule (forcing
    // that team to win, then to lose) and shows the swing in the team's own
    // playoff probability as an "Importance" score. Deliberately button-gated:
    // it's 2 Monte Carlo runs per remaining game, run one at a time against the
    // single-threaded backend, so it can take a few seconds for a long schedule.
    const TEAM_IMPORTANCE_ITERATIONS = 3000;

    async function calculateTeamImportance() {
      const abbr = teamsSelectedAbbr;
      if (!abbr) return;

      const btn = document.getElementById("team-importance-btn");
      const rows = [...document.querySelectorAll("#team-detail-schedule tr")]
        .filter(row => row.dataset.played === "false");

      if (rows.length === 0) {
        btn.textContent = "No Upcoming Games";
        btn.disabled = true;
        return;
      }

      btn.disabled = true;

      for (let i = 0; i < rows.length; i++) {
        const row = rows[i];
        btn.textContent = `Calculating ${i + 1}/${rows.length}...`;

        const cell = row.querySelector(".importance-cell");
        cell.innerHTML = `<span style="color:var(--text-secondary)">...</span>`;

        const week = row.dataset.week;
        const home = row.dataset.home;
        const away = row.dataset.away;
        const teamIsHome = home === abbr;

        try {
          const [ifWin, ifLose] = await Promise.all([
            fetchLockedPlayoffProb(week, home, away, teamIsHome ? "home" : "away", abbr),
            fetchLockedPlayoffProb(week, home, away, teamIsHome ? "away" : "home", abbr)
          ]);

          const swing = Math.abs(ifWin - ifLose) * 100;
          let color = "var(--text-secondary)";
          if (swing >= 15) color = "var(--danger-color)";
          else if (swing >= 5) color = "var(--warning-color)";

          cell.innerHTML = `<span style="color:${color};font-weight:700">${swing.toFixed(1)}%</span>`;
        } catch (err) {
          cell.innerHTML = `<span style="color:var(--danger-color)">Error</span>`;
        }
      }

      btn.textContent = "Recalculate Importance";
      btn.disabled = false;
    }

    // Runs one locked simulation and returns a single team's resulting playoff probability.
    function fetchLockedPlayoffProb(week, home, away, winner, targetAbbr) {
      const locks = `${week}:${home}:${away}:${winner}`;
      const url = `/api/simulation?iterations=${TEAM_IMPORTANCE_ITERATIONS}&locks=${encodeURIComponent(locks)}`;
      return fetch(url)
        .then(res => res.json())
        .then(data => {
          const team = data.teams.find(t => t.abbr === targetAbbr);
          return team ? team.playoff : 0;
        });
    }

    function fetchSandbox() {
      if (sandboxGames.length > 0 && sandboxBaseOdds) {
        // Already loaded, just render
        renderSandboxGames();
        if (lastSandboxSimData) {
          renderSandboxTable(lastSandboxSimData);
        } else {
          runSandboxSimulation();
        }
        return;
      }

      // Show loader in game list and table
      document.getElementById("sandbox-games-container").innerHTML = `
        <div style="text-align:center;padding:4rem;">
          <div class="spinner" style="margin:0 auto 1rem auto"></div>
          Loading schedule...
        </div>
      `;
      document.getElementById("sandbox-tbody").innerHTML = `
        <tr><td colspan="5" style="text-align:center;padding:4rem;">
          <div class="spinner" style="margin:0 auto 1rem auto"></div>
          Calculating baseline...
        </td></tr>
      `;

      // Fetch games
      fetch("/api/games")
        .then(res => res.json())
        .then(gamesData => {
          sandboxGames = gamesData.games;

          // Fetch baseline simulation
          fetch("/api/simulation?iterations=2000")
            .then(res => res.json())
            .then(simData => {
              // Store base odds as map by team abbreviation for O(1) lookup
              sandboxBaseOdds = {};
              simData.teams.forEach(t => {
                sandboxBaseOdds[t.abbr] = t;
              });

              // Populate week select dropdown
              const weekSelect = document.getElementById("sandboxWeekSelect");
              weekSelect.innerHTML = "";
              const uniqueWeeks = [...new Set(sandboxGames.map(g => g.week))].sort((a,b) => a - b);
              uniqueWeeks.forEach(w => {
                const opt = document.createElement("option");
                opt.value = w;
                opt.textContent = "Week " + w;
                weekSelect.appendChild(opt);
              });

              // Determine initial week: first week with an unplayed game
              const firstUnplayedGame = sandboxGames.find(g => g.status === "scheduled");
              if (firstUnplayedGame) {
                sandboxSelectedWeek = firstUnplayedGame.week;
              } else {
                sandboxSelectedWeek = 1;
              }
              weekSelect.value = sandboxSelectedWeek;

              renderSandboxGames();
              renderSandboxTable(simData);
            })
            .catch(err => {
              document.getElementById("sandbox-tbody").innerHTML = `
                <tr><td colspan="5" style="color:var(--danger-color);text-align:center">Error calculating baseline simulation: ${err}</td></tr>
              `;
            });
        })
        .catch(err => {
          document.getElementById("sandbox-games-container").innerHTML = `
            <div style="color:var(--danger-color);text-align:center;padding:2rem;">Failed to load sandbox schedule: ${err}</div>
          `;
        });
    }

    function changeSandboxWeek(week) {
      sandboxSelectedWeek = parseInt(week);
      renderSandboxGames();
    }

    function renderSandboxGames() {
      const container = document.getElementById("sandbox-games-container");
      container.innerHTML = "";

      const games = sandboxGames.filter(g => g.week === sandboxSelectedWeek);
      if (games.length === 0) {
        container.innerHTML = `<div style="text-align:center;padding:2rem;color:var(--text-secondary)">No games in this week.</div>`;
        return;
      }

      games.forEach(game => {
        const key = `${game.week}:${game.home_team}:${game.away_team}`;
        const lockChoice = sandboxLocks[key];

        const card = document.createElement("div");
        card.className = "sandbox-game-card";

        const isPlayed = game.status === "final" || game.status === "in_progress";

        let headerStatus = "";
        if (isPlayed) {
          headerStatus = `<span style="color:var(--success-color);font-weight:600">Final: ${game.home_score} - ${game.away_score}</span>`;
        } else {
          headerStatus = `<span style="color:var(--text-secondary)">${game.date || 'Scheduled'}</span>`;
        }

        card.innerHTML = `
          <div class="sandbox-game-header">
            <span>Matchup</span>
            ${headerStatus}
          </div>
          <div class="sandbox-team-row">
            <button class="sandbox-team-btn ${lockChoice === 'home' ? 'active' : ''}" 
              ${isPlayed ? 'disabled' : ''} 
              onclick="toggleSandboxPick(${game.week}, '${game.home_team}', '${game.away_team}', 'home')">
              <span>${game.home_team}</span>
              ${isPlayed && game.home_score >= game.away_score ? '<span style="font-size:0.75rem">&#x2714;</span>' : ''}
            </button>
            <button class="sandbox-team-btn ${lockChoice === 'away' ? 'active' : ''}" 
              ${isPlayed ? 'disabled' : ''} 
              onclick="toggleSandboxPick(${game.week}, '${game.home_team}', '${game.away_team}', 'away')">
              <span>${game.away_team}</span>
              ${isPlayed && game.away_score >= game.home_score ? '<span style="font-size:0.75rem">&#x2714;</span>' : ''}
            </button>
          </div>
        `;
        container.appendChild(card);
      });
    }

    function toggleSandboxPick(week, home, away, choice) {
      const key = `${week}:${home}:${away}`;
      if (sandboxLocks[key] === choice) {
        delete sandboxLocks[key];
      } else {
        sandboxLocks[key] = choice;
      }

      // Update badge
      const count = Object.keys(sandboxLocks).length;
      document.getElementById("sandboxPickCount").textContent = `${count} Pick${count !== 1 ? 's' : ''} Locked`;

      renderSandboxGames();
      runSandboxSimulation();
    }

    function clearSandboxPicks() {
      sandboxLocks = {};
      document.getElementById("sandboxPickCount").textContent = "0 Picks Locked";
      renderSandboxGames();
      runSandboxSimulation();
    }

    function runSandboxSimulation() {
      const spinner = document.getElementById("sandbox-spinner");
      spinner.style.display = "flex";

      const locks = [];
      for (const [key, winner] of Object.entries(sandboxLocks)) {
        locks.push(`${key}:${winner}`);
      }
      const locksStr = locks.join(",");

      const params = new URLSearchParams();
      params.append("iterations", "2000");
      params.append("locks", locksStr);

      fetch("/api/simulation", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded"
        },
        body: params.toString()
      })
      .then(res => res.json())
      .then(data => {
        spinner.style.display = "none";
        renderSandboxTable(data);
      })
      .catch(err => {
        spinner.style.display = "none";
        alert("Sandbox simulation error: " + err);
      });
    }

    function setSandboxView(view) {
      sandboxTabMode = view;
      document.getElementById("sandbox-toggle-seeding").classList.toggle("active", view === "seeding");
      document.getElementById("sandbox-toggle-postseason").classList.toggle("active", view === "postseason");
      if (lastSandboxSimData) {
        renderSandboxTable(lastSandboxSimData);
      }
    }

    function renderSandboxTable(simData) {
      lastSandboxSimData = simData;
      const tbody = document.getElementById("sandbox-tbody");
      const table = document.getElementById("sandbox-table");
      tbody.innerHTML = "";

      // Setup Headers
      let headersHTML = "";
      if (sandboxTabMode === "seeding") {
        headersHTML = `
          <tr>
            <th>Team</th>
            <th>Playoff % (Delta)</th>
            <th>Div. Leader %</th>
            <th>Wild Card %</th>
          </tr>
        `;
      } else {
        headersHTML = `
          <tr>
            <th>Team</th>
            <th>Lombardi Odds % (Delta)</th>
            <th>Div. Rd %</th>
            <th>Conf. Champ Rd %</th>
            <th>Super Bowl App %</th>
          </tr>
        `;
      }
      table.querySelector("thead").innerHTML = headersHTML;

      // Sort teams
      let sortedTeams = [];
      if (sandboxTabMode === "seeding") {
        sortedTeams = [...simData.teams].sort((a,b) => b.playoff - a.playoff);
      } else {
        sortedTeams = [...simData.teams].sort((a,b) => b.win_superbowl - a.win_superbowl);
      }

      sortedTeams.forEach(team => {
        const base = sandboxBaseOdds ? sandboxBaseOdds[team.abbr] : null;
        
        let targetVal = 0;
        let baseVal = 0;
        let diff = 0;

        if (sandboxTabMode === "seeding") {
          targetVal = team.playoff;
          baseVal = base ? base.playoff : targetVal;
        } else {
          targetVal = team.win_superbowl;
          baseVal = base ? base.win_superbowl : targetVal;
        }
        diff = targetVal - baseVal;

        // Build delta badge
        let deltaBadgeHTML = "";
        // Threshold of 0.05% to avoid random simulation noise showing up as green/red (+0.0%)
        if (Math.abs(diff) >= 0.0005) {
          const sign = diff > 0 ? "+" : "";
          const badgeClass = diff > 0 ? "delta-positive" : "delta-negative";
          deltaBadgeHTML = `<span class="delta-badge ${badgeClass}" style="margin-left: 0.5rem; font-size: 0.75rem; padding: 0.15rem 0.35rem">${sign}${(diff * 100).toFixed(1)}%</span>`;
        }

        const pctStr = (targetVal * 100).toFixed(1) + "%";

        const row = document.createElement("tr");

        if (sandboxTabMode === "seeding") {
          const divLeaderPct = (team.division * 100).toFixed(1) + "%";
          const wildCardPct = (team.wildcard * 100).toFixed(1) + "%";

          let progressClass = "prog-indigo";
          if (team.playoff >= 0.7) progressClass = "prog-emerald";
          else if (team.playoff <= 0.3) progressClass = "prog-rose";

          row.innerHTML = `
            <td>${teamLink(team.abbr)}</td>
            <td>
              <div class="progress-container">
                <div class="progress-bar-bg" style="width: 55px">
                  <div class="progress-bar-fill ${progressClass}" style="width: ${team.playoff * 100}%"></div>
                </div>
                <span style="font-weight:700; min-width: 40px">${pctStr}</span>
                ${deltaBadgeHTML}
              </div>
            </td>
            <td>${divLeaderPct}</td>
            <td>${wildCardPct}</td>
          `;
        } else {
          const winSuperbowlPctStr = (team.win_superbowl * 100).toFixed(1) + "%";
          const divisionalPctStr = (team.divisional * 100).toFixed(1) + "%";
          const confChampionshipPctStr = (team.conf_championship * 100).toFixed(1) + "%";
          const superbowlAppPctStr = (team.superbowl * 100).toFixed(1) + "%";

          let progressClass = "prog-indigo";
          if (team.win_superbowl >= 0.10) progressClass = "prog-emerald";
          else if (team.win_superbowl <= 0.01) progressClass = "prog-rose";

          row.innerHTML = `
            <td>${teamLink(team.abbr)}</td>
            <td>
              <div class="progress-container">
                <div class="progress-bar-bg" style="width: 55px">
                  <div class="progress-bar-fill ${progressClass}" style="width: ${team.win_superbowl * 100}%"></div>
                </div>
                <span style="font-weight:700; min-width: 40px">${pctStr}</span>
                ${deltaBadgeHTML}
              </div>
            </td>
            <td>${divisionalPctStr}</td>
            <td>${confChampionshipPctStr}</td>
            <td>${superbowlAppPctStr}</td>
          `;
        }

        tbody.appendChild(row);
      });
    }

    const teamColors = {
      "ARI": "#97233F",
      "ATL": "#A71930",
      "BAL": "#241773",
      "BUF": "#00338D",
      "CAR": "#0085CA",
      "CHI": "#0B162A",
      "CIN": "#FB4F14",
      "CLE": "#311D00",
      "DAL": "#003566",
      "DEN": "#FB4F14",
      "DET": "#0076B6",
      "GB": "#203731",
      "HOU": "#03202F",
      "IND": "#002C5F",
      "JAX": "#006778",
      "KC": "#E31837",
      "LV": "#707070",
      "LAC": "#0080C6",
      "LA": "#003594",
      "MIA": "#008E97",
      "MIN": "#4F2683",
      "NE": "#002244",
      "NO": "#D3BC8D",
      "NYG": "#0B2265",
      "NYJ": "#125740",
      "PHI": "#004C54",
      "PIT": "#FFB612",
      "SF": "#AA0000",
      "SEA": "#002244",
      "TB": "#D50A0A",
      "TEN": "#4B92DB",
      "WAS": "#5A1414"
    };

    function fetchHistory() {
      const statusDiv = document.getElementById("history-rebuild-status");
      statusDiv.textContent = "Loading history data...";
      
      Promise.all([
        fetch("/api/probability-history").then(res => res.json()),
        fetch("/api/standings").then(res => res.json())
      ])
      .then(([historyData, standingsData]) => {
        statusDiv.textContent = "";
        historyRawData = historyData;
        
        const teamsContainer = document.getElementById("history-teams-container");
        if (teamsContainer.children.length === 0) {
          renderHistoryFilters(standingsData);
          selectQuickHistoryGroup("top5");
        } else {
          updateHistoryChart();
        }
      })
      .catch(err => {
        statusDiv.textContent = "Error loading history: " + err;
      });
    }

    function renderHistoryFilters(standingsData) {
      const container = document.getElementById("history-teams-container");
      container.innerHTML = "";
      
      standingsData.divisions.forEach(div => {
        const divCol = document.createElement("div");
        divCol.style.display = "flex";
        divCol.style.flexDirection = "column";
        divCol.style.gap = "0.5rem";
        
        const divTitle = document.createElement("h4");
        divTitle.style.fontFamily = "var(--font-display)";
        divTitle.style.fontSize = "0.95rem";
        divTitle.style.fontWeight = "700";
        divTitle.style.color = "#cbd5e1";
        divTitle.style.borderBottom = "1px solid var(--border-color)";
        divTitle.style.paddingBottom = "0.25rem";
        divTitle.textContent = div.name;
        divCol.appendChild(divTitle);
        
        const teamList = document.createElement("div");
        teamList.style.display = "flex";
        teamList.style.flexWrap = "wrap";
        teamList.style.gap = "0.4rem";
        
        div.teams.forEach(team => {
          const btn = document.createElement("button");
          btn.id = "history-toggle-" + team.abbr;
          btn.className = "btn btn-secondary";
          btn.style.padding = "0.3rem 0.6rem";
          btn.style.fontSize = "0.8rem";
          btn.style.fontWeight = "700";
          btn.style.fontFamily = "var(--font-display)";
          btn.style.background = "rgba(255,255,255,0.02)";
          btn.style.borderColor = "var(--border-color)";
          btn.style.color = "var(--text-secondary)";
          btn.textContent = team.abbr;
          
          btn.onclick = () => {
            toggleHistoryTeam(team.abbr);
          };
          
          teamList.appendChild(btn);
        });
        
        divCol.appendChild(teamList);
        container.appendChild(divCol);
      });
    }

    function toggleHistoryTeam(abbr) {
      if (historySelectedTeams.has(abbr)) {
        historySelectedTeams.delete(abbr);
      } else {
        historySelectedTeams.add(abbr);
      }
      updateHistoryTeamButtons();
      updateHistoryChart();
    }

    function updateHistoryTeamButtons() {
      document.querySelectorAll("[id^='history-toggle-']").forEach(btn => {
        const abbr = btn.id.replace("history-toggle-", "");
        if (historySelectedTeams.has(abbr)) {
          const color = teamColors[abbr] || "#6366f1";
          btn.style.background = color + "20";
          btn.style.borderColor = color;
          btn.style.color = "white";
        } else {
          btn.style.background = "rgba(255,255,255,0.02)";
          btn.style.borderColor = "var(--border-color)";
          btn.style.color = "var(--text-secondary)";
        }
      });
    }

    function selectQuickHistoryGroup(group) {
      if (group === "all") {
        historySelectedTeams = new Set(Object.keys(teamColors));
      } else if (group === "none") {
        historySelectedTeams.clear();
      } else if (group === "afc" || group === "nfc") {
        historySelectedTeams.clear();
        document.querySelectorAll("[id^='history-toggle-']").forEach(btn => {
          const abbr = btn.id.replace("history-toggle-", "");
          const divHeaderElement = btn.closest("div").previousSibling;
          if (divHeaderElement && divHeaderElement.textContent) {
            const divHeader = divHeaderElement.textContent;
            if (divHeader.startsWith(group.toUpperCase())) {
              historySelectedTeams.add(abbr);
            }
          }
        });
      } else if (group === "top5") {
        historySelectedTeams.clear();
        let maxW = 0;
        historyRawData.forEach(d => {
          if (d.week > maxW) maxW = d.week;
        });
        
        const latestPoints = historyRawData.filter(d => d.week === maxW);
        const sorted = latestPoints.sort((a, b) => {
          const valA = historyMetric === "playoff" ? a.playoff_prob : a.superbowl_prob;
          const valB = historyMetric === "playoff" ? b.playoff_prob : b.superbowl_prob;
          return valB - valA;
        });
        
        for (let i = 0; i < 5 && i < sorted.length; ++i) {
          historySelectedTeams.add(sorted[i].team);
        }
      }
      
      updateHistoryTeamButtons();
      updateHistoryChart();
    }

    function switchHistoryMetric(metric) {
      historyMetric = metric;
      document.getElementById("metric-playoffs-btn").classList.toggle("active", metric === "playoff");
      document.getElementById("metric-superbowl-btn").classList.toggle("active", metric === "superbowl");
      updateHistoryChart();
    }

    function updateHistoryChart() {
      const ctx = document.getElementById("history-chart").getContext("2d");
      
      const uniqueWeeks = [...new Set(historyRawData.map(d => d.week))].sort((a, b) => a - b);
      const xLabels = uniqueWeeks.map(w => w === 0 ? "Pre-Season" : "Week " + w);
      
      const datasets = [];
      
      historySelectedTeams.forEach(abbr => {
        const teamPoints = historyRawData.filter(d => d.team === abbr);
        teamPoints.sort((a, b) => a.week - b.week);
        
        const dataValues = uniqueWeeks.map(w => {
          const point = teamPoints.find(p => p.week === w);
          if (!point) return 0;
          return historyMetric === "playoff" ? point.playoff_prob * 100 : point.superbowl_prob * 100;
        });
        
        const color = teamColors[abbr] || "#6366f1";
        
        datasets.push({
          label: abbr,
          data: dataValues,
          borderColor: color,
          backgroundColor: color + "10",
          borderWidth: 3,
          pointBackgroundColor: color,
          pointBorderColor: "#161d30",
          pointRadius: 4,
          pointHoverRadius: 6,
          tension: 0.2,
          fill: false
        });
      });
      
      let yMax = historyMetric === "playoff" ? 100 : 30;
      let maxValInSelected = 0;
      datasets.forEach(ds => {
        ds.data.forEach(val => {
          if (val > maxValInSelected) maxValInSelected = val;
        });
      });
      if (maxValInSelected > 0) {
        yMax = Math.min(100, Math.ceil(maxValInSelected / 10) * 10 + 5);
      }
      if (historyMetric === "playoff") {
        yMax = 100;
      }
      
      if (historyChart) {
        historyChart.data.labels = xLabels;
        historyChart.data.datasets = datasets;
        historyChart.options.scales.y.max = yMax;
        historyChart.update();
      } else {
        historyChart = new Chart(ctx, {
          type: "line",
          data: {
            labels: xLabels,
            datasets: datasets
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
              legend: {
                display: true,
                position: "top",
                labels: {
                  color: "#94a3b8",
                  font: {
                    family: "Outfit",
                    size: 12,
                    weight: "600"
                  },
                  boxWidth: 15,
                  padding: 15
                }
              },
              tooltip: {
                mode: "index",
                intersect: false,
                backgroundColor: "#161d30",
                titleColor: "#f8fafc",
                bodyColor: "#94a3b8",
                borderColor: "rgba(255,255,255,0.08)",
                borderWidth: 1,
                callbacks: {
                  label: function(context) {
                    return ` ${context.dataset.label}: ${context.parsed.y.toFixed(1)}%`;
                  }
                }
              }
            },
            scales: {
              x: {
                grid: {
                  color: "rgba(255, 255, 255, 0.04)"
                },
                ticks: {
                  color: "#94a3b8",
                  font: {
                    family: "Inter",
                    size: 11
                  }
                }
              },
              y: {
                min: 0,
                max: yMax,
                grid: {
                  color: "rgba(255, 255, 255, 0.04)"
                },
                ticks: {
                  color: "#94a3b8",
                  font: {
                    family: "Inter",
                    size: 11
                  },
                  callback: function(value) {
                    return value + "%";
                  }
                }
              }
            }
          }
        });
      }
    }

    function rebuildHistoryData() {
      const btn = document.getElementById("btn-rebuild-history");
      const btnText = document.getElementById("rebuild-btn-text");
      const statusDiv = document.getElementById("history-rebuild-status");
      
      btn.disabled = true;
      btnText.textContent = "Simulating & Rebuilding...";
      statusDiv.textContent = "Running 100k simulations per completed week... This takes ~15-30 seconds.";
      
      fetch("/api/rebuild-probability-history", { method: "POST" })
        .then(res => res.json())
        .then(data => {
          btn.disabled = false;
          btnText.textContent = "Rebuild History (100k Sims)";
          if (data.ok) {
            statusDiv.textContent = "History successfully rebuilt!";
            fetchHistory();
          } else {
            statusDiv.textContent = "Rebuild failed: " + data.error;
          }
        })
        .catch(err => {
          btn.disabled = false;
          btnText.textContent = "Rebuild History (100k Sims)";
          statusDiv.textContent = "Connection error: " + err;
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
                << "\",\"full_name\":\"" << jsonEscape(team->fullName())
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

std::string WebServer::simulationJson(int iterations, const std::string& locksStr) const {
    MonteCarlo mc;
    mc.setModelParameters(homeAdvantage_, strengthWeight_);
    mc.loadHistoricalStrengths(season_, "data/historical");
    Season current = season_;

    if (!locksStr.empty()) {
        std::vector<Game> updatedGames = current.allGames();
        std::stringstream ss(locksStr);
        std::string lockItem;
        while (std::getline(ss, lockItem, ',')) {
            if (lockItem.empty()) continue;
            std::stringstream itemSs(lockItem);
            std::string weekStr, home, away, winner;
            if (std::getline(itemSs, weekStr, ':') &&
                std::getline(itemSs, home, ':') &&
                std::getline(itemSs, away, ':') &&
                std::getline(itemSs, winner, ':')) {
                try {
                    int w = std::stoi(weekStr);
                    for (auto& game : updatedGames) {
                        if (game.week() == w && game.homeTeam() == home && game.awayTeam() == away) {
                            int homeScore = (winner == "home") ? 1 : 0;
                            int awayScore = (winner == "away") ? 1 : 0;
                            game = Game(w, game.date(), home, away, homeScore, awayScore, "final");
                        }
                    }
                } catch (...) {}
            }
        }
        current.replaceGames(updatedGames);
    }

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

void WebServer::rebuildProbabilityHistory(int iterations) {
    // Determine the maximum week that has any completed game
    int maxCompletedWeek = 0;
    for (const auto& game : season_.allGames()) {
        if (game.isFinal() && game.week() > maxCompletedWeek) {
            maxCompletedWeek = game.week();
        }
    }

    std::string historyPath = "data/probability_history.csv";
    std::filesystem::path p(historyPath);
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream outFile(historyPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open probability history file for writing: " << historyPath << std::endl;
        return;
    }

    outFile << "week,team,playoff_prob,superbowl_prob\n";

    // Loop through weeks from 0 to maxCompletedWeek
    for (int w = 0; w <= maxCompletedWeek; ++w) {
        // Construct a Season object where we only keep game results for weeks <= w
        Season simSeason = season_;
        std::vector<Game> simGames = simSeason.allGames();
        for (auto& game : simGames) {
            if (game.week() > w || !game.isFinal()) {
                game = Game(game.week(), game.date(), game.homeTeam(), game.awayTeam(), 0, 0, "scheduled");
            }
        }
        simSeason.replaceGames(simGames);
        simSeason.computeStandings();

        // Run Monte Carlo simulation on this historical state snapshot
        MonteCarlo mc;
        mc.setModelParameters(homeAdvantage_, strengthWeight_);
        mc.loadHistoricalStrengths(simSeason, "data/historical");
        
        auto results = mc.simulate(simSeason, iterations, 12345);

        for (const auto& [abbr, _] : simSeason.allTeams()) {
            double playoffProb = results.playoffProbability.count(abbr) ? results.playoffProbability.at(abbr) : 0.0;
            double superbowlProb = results.winSuperBowlProbability.count(abbr) ? results.winSuperBowlProbability.at(abbr) : 0.0;
            outFile << w << "," << abbr << "," 
                    << std::fixed << std::setprecision(6) << playoffProb << "," 
                    << std::fixed << std::setprecision(6) << superbowlProb << "\n";
        }
    }
    outFile.close();
    std::cout << "Successfully rebuilt probability history up to week " << maxCompletedWeek << " using " << iterations << " iterations." << std::endl;
}
