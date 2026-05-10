#include "catch.hpp"

#include "util/CsvParser.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string shellQuote(const std::string& value) {
    // Safe single-quote escaping for POSIX shells.
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::filesystem::path makeTempDir() {
    const auto base = std::filesystem::temp_directory_path();
    const auto dir = base / ("nfl3_cli_integration_" + std::to_string(std::rand()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}

void copyFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}

int runBinaryInWorkspace(const std::filesystem::path& workspace,
                         const std::string& binaryPath,
                         const std::string& args) {
    const std::string cmd =
        "cd " + shellQuote(workspace.string()) +
        " && " + shellQuote(binaryPath) + " " + args +
        " >/dev/null 2>&1";
    return std::system(cmd.c_str());
}

} // namespace

TEST_CASE("CLI integration: load-schedule and backfit-model", "[integration][cli]") {
    const std::filesystem::path sourceRoot = NFL3_SOURCE_DIR;
    const std::filesystem::path binaryPath = sourceRoot / "build" / "nfl3";

    REQUIRE(std::filesystem::exists(binaryPath));

    const auto tempDir = makeTempDir();
    const auto dataDir = tempDir / "data";
    const auto historicalDir = dataDir / "historical";

    std::filesystem::create_directories(historicalDir);

    copyFile(sourceRoot / "data" / "teams.csv", dataDir / "teams.csv");
    copyFile(sourceRoot / "data" / "historical" / "2024.csv", historicalDir / "2024.csv");
    copyFile(sourceRoot / "data" / "historical" / "2025.csv", historicalDir / "2025.csv");

    const auto customSchedule = tempDir / "custom_schedule.csv";
    writeTextFile(customSchedule,
                  "week,date,home_team,away_team,home_score,away_score,status\n"
                  "1,2026-09-10,KC,DEN,-1,-1,scheduled\n");

    SECTION("load-schedule writes canonical data/schedule.csv") {
        const int rc = runBinaryInWorkspace(
            tempDir,
            binaryPath.string(),
            "load-schedule " + shellQuote(customSchedule.string()));
        REQUIRE(rc == 0);

        const auto rows = CsvParser::parse((dataDir / "schedule.csv").string());
        REQUIRE(rows.size() == 1);
        REQUIRE(rows[0].at("week") == "1");
        REQUIRE(rows[0].at("home_team") == "KC");
        REQUIRE(rows[0].at("away_team") == "DEN");
        REQUIRE(rows[0].at("status") == "scheduled");
    }

    SECTION("backfit-model writes model_coefficients.csv") {
        const int rc = runBinaryInWorkspace(
            tempDir,
            binaryPath.string(),
            "backfit-model 2025");
        REQUIRE(rc == 0);

        const auto coeffRows = CsvParser::parse((dataDir / "model_coefficients.csv").string());
        REQUIRE_FALSE(coeffRows.empty());

        bool sawHomeAdvantage = false;
        bool sawStrengthWeight = false;
        bool sawSampleSize = false;

        for (const auto& row : coeffRows) {
            const auto& key = row.at("coefficient");
            const auto& value = row.at("value");
            if (key == "home_advantage") {
                sawHomeAdvantage = true;
                const double parsed = std::stod(value);
                REQUIRE(parsed >= 0.0);
                REQUIRE(parsed <= 1.0);
            } else if (key == "strength_weight") {
                sawStrengthWeight = true;
                REQUIRE(std::stod(value) >= 0.0);
            } else if (key == "sample_size") {
                sawSampleSize = true;
                REQUIRE(std::stoi(value) > 0);
            }
        }

        REQUIRE(sawHomeAdvantage);
        REQUIRE(sawStrengthWeight);
        REQUIRE(sawSampleSize);
    }

    SECTION("load-schedule fails when required columns are missing") {
        const auto invalidSchedule = tempDir / "invalid_schedule.csv";
        writeTextFile(invalidSchedule,
                      "week,date,home_team,away_team,home_score,away_score\n"
                      "1,2026-09-10,KC,DEN,-1,-1\n");

        const int rc = runBinaryInWorkspace(
            tempDir,
            binaryPath.string(),
            "load-schedule " + shellQuote(invalidSchedule.string()));

        REQUIRE(rc != 0);

        // Ensure command did not write canonical schedule on failure.
        REQUIRE_FALSE(std::filesystem::exists(dataDir / "schedule.csv"));
    }

    std::filesystem::remove_all(tempDir);
}
