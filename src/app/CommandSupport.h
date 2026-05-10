#ifndef COMMAND_SUPPORT_H
#define COMMAND_SUPPORT_H

#include "model/MonteCarlo.h"
#include "model/Season.h"
#include <string>

namespace nfl3 {

struct FittedModel {
    double homeAdvantage = 0.57;
    double strengthWeight = 0.15;
    double slopePrevWinPct = 0.0;
    int sampleSize = 0;
};

Season loadSeasonFromCsvFiles(const std::string& teamsPath,
                              const std::string& schedulePath);

void copyScheduleToPath(const std::string& sourcePath,
                        const std::string& destinationPath);

FittedModel fitModelFromHistoricalYear(int targetYear,
                                       const std::string& teamsPath,
                                       const std::string& historicalDir);

void persistFittedModel(const FittedModel& model,
                        int targetYear,
                        const std::string& outputPath);

bool loadFittedModel(const std::string& path, FittedModel& model);

void applyModelToMonteCarlo(MonteCarlo& mc, const FittedModel& model);

} // namespace nfl3

#endif // COMMAND_SUPPORT_H
