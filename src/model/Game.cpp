#include "Game.h"

Game::Game(int week, const std::string& date, const std::string& homeTeam,
           const std::string& awayTeam, int homeScore, int awayScore,
           const std::string& status)
    : week_(week), date_(date), homeTeam_(homeTeam), awayTeam_(awayTeam),
      homeScore_(homeScore), awayScore_(awayScore), status_(status) {}

bool Game::homeTeamWon() const {
    return homeScore_ > awayScore_;
}

bool Game::awayTeamWon() const {
    return awayScore_ > homeScore_;
}

bool Game::isTie() const {
    return homeScore_ == awayScore_;
}
