#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "model/Season.h"
#include <string>

class WebServer {
public:
    struct Response {
        int statusCode;
        std::string contentType;
        std::string body;
    };

    WebServer(Season season,
              const std::string& schedulePath,
              double homeAdvantage,
              double strengthWeight,
              int defaultIterations = 100000);

    void run(int port);

    // Test helper: dispatch request directly without sockets.
    Response handleForTests(const std::string& method,
                            const std::string& rawPath,
                            const std::string& body = "");

private:
    Season season_;
    std::string schedulePath_;
    double homeAdvantage_;
    double strengthWeight_;
    int defaultIterations_;

    std::string handleRequest(const std::string& method,
                              const std::string& rawPath,
                              const std::string& body,
                              int& statusCode,
                              std::string& contentType);

    std::string renderStandingsHtml() const;
    std::string renderSimulationHtml(int iterations) const;
    std::string renderImpactHtml(int iterations) const;

    std::string standingsJson() const;
    std::string simulationJson(int iterations) const;
    std::string impactJson(int iterations) const;

    bool applyResultUpdate(const std::string& body, std::string& error);
    bool persistSchedule() const;

    static std::string jsonEscape(const std::string& value);
    static std::string htmlEscape(const std::string& value);
    static std::string urlDecode(const std::string& value);
    static int parseIterations(const std::string& rawPath, int fallback);
    static std::string getPathOnly(const std::string& rawPath);
    static std::string buildHttpResponse(int statusCode,
                                         const std::string& contentType,
                                         const std::string& body);
};

#endif // WEB_SERVER_H
