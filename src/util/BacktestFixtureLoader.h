#ifndef BACKTEST_FIXTURE_LOADER_H
#define BACKTEST_FIXTURE_LOADER_H

#include "model/Season.h"
#include <string>
#include <vector>

struct BacktestSeasonFixture {
    int seasonYear;
    Season season;
};

class BacktestFixtureLoader {
public:
    static Season loadSeasonFromCsv(const std::string& teamsPath,
                                    const std::string& gamesPath);

    static std::vector<BacktestSeasonFixture> loadSeasons(
        const std::string& teamsPath,
        const std::string& historicalDir,
        const std::vector<int>& seasonYears);

    static std::vector<BacktestSeasonFixture> loadPriorSeasons(
        const std::string& teamsPath,
        const std::string& historicalDir,
        int targetSeason,
        int lookbackYears);
};

#endif // BACKTEST_FIXTURE_LOADER_H
