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

std::string WebServer::renderStandingsHtml() const {
    Season current = season_;
    current.computeStandings();

    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\">"
        << "<title>nfl3 standings</title>"
        << "<style>body{font-family:monospace;max-width:980px;margin:2rem auto;padding:0 1rem;}"
        << "table{border-collapse:collapse;margin-bottom:1.5rem;width:100%;}"
        << "th,td{border:1px solid #bbb;padding:6px 8px;text-align:left;}"
        << "th{background:#f3f3f3;}"
        << "nav a{margin-right:1rem;}"
        << "form input{margin-right:0.5rem;}"
        << "</style></head><body>"
        << "<h1>nfl3 standings</h1>"
        << "<nav><a href=\"/standings\">standings</a><a href=\"/simulation\">simulation</a><a href=\"/impact\">impact</a></nav>";

    for (const auto& division : current.getDivisions()) {
        const auto teams = current.teamsByDivision(division);
        out << "<h2>" << htmlEscape(division) << "</h2>"
            << "<table><tr><th>Team</th><th>W</th><th>L</th><th>T</th><th>Win%</th></tr>";
        for (const auto* team : teams) {
            out << "<tr><td>" << htmlEscape(team->abbreviation())
                << "</td><td>" << team->wins()
                << "</td><td>" << team->losses()
                << "</td><td>" << team->ties()
                << "</td><td>" << team->winPercentage() << "</td></tr>";
        }
        out << "</table>";
    }

    out << "<h2>Update Result</h2>"
        << "<form method=\"post\" action=\"/api/update-result\">"
        << "<input name=\"week\" placeholder=\"week\" required>"
        << "<input name=\"home_team\" placeholder=\"home\" required>"
        << "<input name=\"away_team\" placeholder=\"away\" required>"
        << "<input name=\"home_score\" placeholder=\"home score\" required>"
        << "<input name=\"away_score\" placeholder=\"away score\" required>"
        << "<button type=\"submit\">save</button></form>";

    out << "</body></html>";
    return out.str();
}

std::string WebServer::renderSimulationHtml(int iterations) const {
    MonteCarlo mc;
    mc.setModelParameters(homeAdvantage_, strengthWeight_);
    Season current = season_;
    current.computeStandings();
    const auto sim = mc.simulate(current, iterations, 12345);

    std::vector<std::pair<std::string, double>> ordered(sim.playoffProbability.begin(),
                                                         sim.playoffProbability.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\">"
        << "<title>nfl3 simulation</title>"
        << "<style>body{font-family:monospace;max-width:980px;margin:2rem auto;padding:0 1rem;}"
        << "table{border-collapse:collapse;width:100%;}"
        << "th,td{border:1px solid #bbb;padding:6px 8px;text-align:left;}"
        << "th{background:#f3f3f3;}"
        << "nav a{margin-right:1rem;}"
        << "</style></head><body>"
        << "<h1>Simulation (" << iterations << " iterations)</h1>"
        << "<nav><a href=\"/standings\">standings</a><a href=\"/simulation\">simulation</a><a href=\"/impact\">impact</a></nav>"
        << "<table><tr><th>Team</th><th>Playoff %</th><th>Division %</th><th>Wildcard %</th></tr>";

    for (const auto& [abbr, playoffProb] : ordered) {
        out << "<tr><td>" << htmlEscape(abbr)
            << "</td><td>" << playoffProb * 100.0
            << "</td><td>" << sim.divisionWinProbability.at(abbr) * 100.0
            << "</td><td>" << sim.wildcardProbability.at(abbr) * 100.0
            << "</td></tr>";
    }

    out << "</table></body></html>";
    return out.str();
}

std::string WebServer::renderImpactHtml(int iterations) const {
    MonteCarlo mc;
    mc.setModelParameters(homeAdvantage_, strengthWeight_);
    Season current = season_;
    current.computeStandings();
    const auto impact = mc.analyzeImpact(current, iterations, 12345);

    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\">"
        << "<title>nfl3 impact</title>"
        << "<style>body{font-family:monospace;max-width:980px;margin:2rem auto;padding:0 1rem;}"
        << "table{border-collapse:collapse;width:100%;}"
        << "th,td{border:1px solid #bbb;padding:6px 8px;text-align:left;}"
        << "th{background:#f3f3f3;}"
        << "nav a{margin-right:1rem;}"
        << "</style></head><body>"
        << "<h1>Impact (week " << impact.week << ", " << iterations << " iterations)</h1>"
        << "<nav><a href=\"/standings\">standings</a><a href=\"/simulation\">simulation</a><a href=\"/impact\">impact</a></nav>"
        << "<table><tr><th>Home</th><th>Away</th><th>Home Delta %</th><th>Away Delta %</th></tr>";

    for (const auto& gameImpact : impact.gameImpacts) {
        out << "<tr><td>" << htmlEscape(gameImpact.homeTeam)
            << "</td><td>" << htmlEscape(gameImpact.awayTeam)
            << "</td><td>" << gameImpact.homeDeltaPlayoffProb * 100.0
            << "</td><td>" << gameImpact.awayDeltaPlayoffProb * 100.0
            << "</td></tr>";
    }

    out << "</table></body></html>";
    return out.str();
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
            << ",\"wildcard\":" << sim.wildcardProbability.at(abbr) << '}';
    }
    out << "]}";
    return out.str();
}

std::string WebServer::impactJson(int iterations) const {
    MonteCarlo mc;
    mc.setModelParameters(homeAdvantage_, strengthWeight_);
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
