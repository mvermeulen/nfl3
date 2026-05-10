#include "catch.hpp"

#include "app/CommandSupport.h"
#include "output/WebServer.h"
#include "util/CsvParser.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <string>

namespace {

struct CalibrationBaseline {
    int seasonYear;
    double maxBrier;
    double maxLogLoss;
};

constexpr std::array<CalibrationBaseline, 7> kCalibrationBaselines = {{
    {2019, 0.2428, 0.6806},
    {2020, 0.2374, 0.6691},
    {2021, 0.2323, 0.6584},
    {2022, 0.2465, 0.6897},
    {2023, 0.2454, 0.6838},
    {2024, 0.2447, 0.6822},
    {2025, 0.2462, 0.6871},
}};

std::filesystem::path makeTempDir() {
    const auto base = std::filesystem::temp_directory_path();
    const auto dir = base / ("nfl3_test_" + std::to_string(std::rand()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}

} // namespace

TEST_CASE("CommandSupport load-schedule copy validates and writes canonical CSV", "[commands]") {
    const auto tempDir = makeTempDir();
    const auto source = tempDir / "input_schedule.csv";
    const auto dest = tempDir / "copied_schedule.csv";

    writeTextFile(source,
                  "week,date,home_team,away_team,home_score,away_score,status\n"
                  "1,2026-09-10,KC,DEN,-1,-1,scheduled\n");

    nfl3::copyScheduleToPath(source.string(), dest.string());

    const auto rows = CsvParser::parse(dest.string());
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].at("home_team") == "KC");
    REQUIRE(rows[0].at("away_team") == "DEN");
    REQUIRE(rows[0].at("status") == "scheduled");

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("CommandSupport backfit model persistence round-trip", "[commands][backfit]") {
    const std::string teamsPath = std::string(NFL3_SOURCE_DIR) + "/data/teams.csv";
    const std::string historicalDir = std::string(NFL3_SOURCE_DIR) + "/data/historical";

    const auto model = nfl3::fitModelFromHistoricalYear(2025, teamsPath, historicalDir);

    REQUIRE(model.sampleSize > 0);
    REQUIRE(model.homeAdvantage >= 0.0);
    REQUIRE(model.homeAdvantage <= 1.0);
    REQUIRE(model.strengthWeight >= 0.0);

    const auto tempDir = makeTempDir();
    const auto coeffPath = tempDir / "model_coefficients.csv";

    nfl3::persistFittedModel(model, 2025, coeffPath.string());

    nfl3::FittedModel loaded;
    REQUIRE(nfl3::loadFittedModel(coeffPath.string(), loaded));

    REQUIRE(loaded.sampleSize == model.sampleSize);
    REQUIRE(loaded.homeAdvantage == Approx(model.homeAdvantage).margin(1e-9));
    REQUIRE(loaded.strengthWeight == Approx(model.strengthWeight).margin(1e-9));
    REQUIRE(loaded.slopePrevWinPct == Approx(model.slopePrevWinPct).margin(1e-9));
    REQUIRE(loaded.pointDifferentialWeight == Approx(model.pointDifferentialWeight).margin(1e-9));
    REQUIRE(loaded.slopePrevPointDifferential == Approx(model.slopePrevPointDifferential).margin(1e-9));
    REQUIRE(loaded.brierScore == Approx(model.brierScore).margin(1e-9));
    REQUIRE(loaded.logLoss == Approx(model.logLoss).margin(1e-9));
    REQUIRE(loaded.brierScore >= 0.0);
    REQUIRE(loaded.logLoss >= 0.0);

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("CommandSupport calibration report spans a year range", "[commands][calibration]") {
    const std::string teamsPath = std::string(NFL3_SOURCE_DIR) + "/data/teams.csv";
    const std::string historicalDir = std::string(NFL3_SOURCE_DIR) + "/data/historical";

    const auto report = nfl3::buildCalibrationReport(2019, 2025, teamsPath, historicalDir);

    REQUIRE(report.size() == kCalibrationBaselines.size());

    for (size_t i = 0; i < report.size(); ++i) {
        const auto& row = report[i];
        const auto& baseline = kCalibrationBaselines[i];

        REQUIRE(row.seasonYear == baseline.seasonYear);
        REQUIRE(row.model.sampleSize > 0);
        REQUIRE(row.model.homeAdvantage >= 0.0);
        REQUIRE(row.model.homeAdvantage <= 1.0);
        REQUIRE(std::isfinite(row.model.pointDifferentialWeight));
        REQUIRE(row.model.brierScore >= 0.0);
        REQUIRE(row.model.logLoss >= 0.0);
        REQUIRE(row.model.brierScore <= baseline.maxBrier);
        REQUIRE(row.model.logLoss <= baseline.maxLogLoss);
    }
}

TEST_CASE("WebServer API endpoints return expected payloads", "[web]") {
    const std::string teamsPath = std::string(NFL3_SOURCE_DIR) + "/data/teams.csv";
    const std::string schedulePath = std::string(NFL3_SOURCE_DIR) + "/data/schedule.csv";

    const auto tempDir = makeTempDir();
    const auto tempSchedule = tempDir / "schedule.csv";
    std::filesystem::copy_file(schedulePath, tempSchedule, std::filesystem::copy_options::overwrite_existing);

    Season season = nfl3::loadSeasonFromCsvFiles(teamsPath, tempSchedule.string());
    WebServer server(season, tempSchedule.string(), 0.57, 0.15, 50);

    SECTION("GET /api/standings") {
        const auto response = server.handleForTests("GET", "/api/standings");
        REQUIRE(response.statusCode == 200);
        REQUIRE(response.contentType.find("application/json") != std::string::npos);
        REQUIRE(response.body.find("\"divisions\"") != std::string::npos);
    }

    SECTION("GET /api/simulation with iterations") {
        const auto response = server.handleForTests("GET", "/api/simulation?iterations=12");
        REQUIRE(response.statusCode == 200);
        REQUIRE(response.body.find("\"iterations\":12") != std::string::npos);
    }

    SECTION("POST /api/update-result persists updated score") {
        const auto response = server.handleForTests(
            "POST",
            "/api/update-result",
            "week=1&home_team=KC&away_team=LAC&home_score=30&away_score=10");

        REQUIRE(response.statusCode == 200);
        REQUIRE(response.body.find("\"ok\":true") != std::string::npos);

        const auto rows = CsvParser::parse(tempSchedule.string());
        bool found = false;
        for (const auto& row : rows) {
            if (row.at("week") == "1" && row.at("home_team") == "KC" && row.at("away_team") == "LAC") {
                found = true;
                REQUIRE(row.at("home_score") == "30");
                REQUIRE(row.at("away_score") == "10");
                REQUIRE(row.at("status") == "final");
            }
        }
        REQUIRE(found);
    }

    SECTION("Unknown endpoint returns 404") {
        const auto response = server.handleForTests("GET", "/api/not-real");
        REQUIRE(response.statusCode == 404);
    }

    std::filesystem::remove_all(tempDir);
}
