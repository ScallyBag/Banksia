/*
 This file is part of Banksia, distributed under MIT license.
 
 Copyright (c) 2019 Nguyen Hong Pham
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */


#include <fstream>
#include <iomanip> // for setprecision
#include <cmath>
#include <algorithm>
#include <random>

#include "tourmng.h"

#include "../3rdparty/json/json.h"

using namespace banksia;

bool MatchRecord::isValid() const
{
    return !playernames[0].empty() && !playernames[1].empty();
}

std::string MatchRecord::toString() const
{
    std::ostringstream stringStream;
    stringStream << "names: " << playernames[0] << ", " << playernames[1]
    << ", status: " << static_cast<int>(state)
    << ", round: " << round;
    return stringStream.str();
}

bool MatchRecord::load(const Json::Value& obj)
{
    auto array = obj["players"];
    playernames[0] = array[0].asString();
    playernames[1] = array[1].asString();

    if (obj.isMember("startFen")) {
        startFen = obj["startFen"].asString();
    }
    
    startMoves.clear();
    if (obj.isMember("startMoves")) {
        auto array = obj["startMoves"];
        for (int i = 0; i < int(array.size()); i++){
            auto k = array[i].asInt();
            Move m(k & 0xff, k >> 8 & 0xff, static_cast<PieceType>(k >> 16 & 0xff));
            startMoves.push_back(m);
        }
    }

    auto s = obj["result"].asString();
    resultType = string2ResultType(s);

    state = resultType == ResultType::noresult ? MatchState::none : MatchState::completed;

    gameIdx = obj["gameIdx"].asInt();
    round = obj["round"].asInt();
    pairId = obj["pairId"].asInt();
    return true;
}

Json::Value MatchRecord::saveToJson() const
{
    Json::Value obj;
    
    Json::Value players;
    players.append(playernames[0]);
    players.append(playernames[1]);
    obj["players"] = players;

    if (!startFen.empty()) {
        obj["startFen"] = startFen;
    }
    
    if (!startMoves.empty()) {
        Json::Value moves;
        for(auto && m : startMoves) {
            auto k = m.dest | m.from << 8 | static_cast<int>(m.promotion) << 16;
            moves.append(k);
        }
        
        obj["startMoves"] = moves;
    }
    
    obj["result"] = resultType2String(resultType);
    obj["gameIdx"] = gameIdx;
    obj["round"] = round;
    obj["pairId"] = pairId;
    return obj;
}

//////////////////////////////
bool TourPlayer::isValid() const
{
    return !name.empty()
    && gameCnt >= 0 && winCnt >= 0 && drawCnt>= 0 && lossCnt>= 0
    && gameCnt == winCnt + drawCnt + lossCnt;
}

std::string TourPlayer::toString() const
{
    std::ostringstream stringStream;
    stringStream << name << "#games: "<< gameCnt << ", wdl: " << winCnt << ", " << drawCnt << ", " << lossCnt;
    return stringStream.str();
}

bool TourPlayer::smaller(const TourPlayer& other) const {
    return winCnt < other.winCnt
    || (winCnt == other.winCnt && (lossCnt > other.lossCnt || (lossCnt == other.lossCnt && drawCnt < other.drawCnt)));
}


//////////////////////////////
TourMng::TourMng()
{
}

TourMng::~TourMng()
{
}

static const char* tourTypeNames[] = {
    "roundrobin", "knockout", nullptr
};

// To create json file
void TourMng::fixJson(Json::Value& d, const std::string& path)
{
    // Base
    auto s = "base";
    Json::Value v;
    if (!d.isMember(s)) {
        v = d[s];
    }

    if (!v.isMember("type")) {
        v["type"] = tourTypeNames[0];
    }
    s = "games per pair";
    if (!v.isMember(s)) {
        v[s] = 2;
    }

    if (!v.isMember("ponder")) {
        v["ponder"] = false;
    }
    
    s = "shuffle players";
    if (!v.isMember(s)) {
        v[s] = false;
    }
    
    s = "resumable";
    if (!v.isMember(s)) {
        v[s] = true;
    }
    
    if (!v.isMember("event")) {
        v["event"] = "Computer event";
    }
    if (!v.isMember("site")) {
        v["site"] = "Somewhere on Earth";
    }
    
    if (!v.isMember("concurrency")) {
        v["concurrency"] = 2;
    }
    
    s = "tips";
    if (!v.isMember(s)) {
        v[s] = "type: " + std::string(tourTypeNames[0]) + ", " + std::string(tourTypeNames[1]) + "; event, site for PGN header; shuffle: random players for roundrobin";
    }

    d["base"] = v;

    s = "time control";
    if (!d.isMember(s)) {
        Json::Value v;
        v["mode"] = "standard";
        v["moves"] = 40;
        v["time"] = double(5.5);
        v["increment"] = double(0.5);
        v["margin"] = double(0.8);
        v["tips"] = "unit's second; mode: standard, infinite, depth, movetime; margin: an extra time before checking if over time";
        d[s] = v;
    }

    s = "opening books";
    if (!d.isMember(s)) {
        Json::Value v;
        for(int i = 0; i < 3; i++) {
            auto bookType = static_cast<BookType>(i);
            
            Json::Value b;
            b["mode"] = false;
            b["type"] = BookMng::bookType2String(bookType);
            b["path"] = "";
            
            if (bookType == BookType::polygot) {
                b["maxply"] = 12;
                b["top100"] = 20;
                b["tips"] = "maxply: ply to play; top100: percents of top moves (for a given position) to select ranndomly an opening move, 0 is always the best";
            }
            v.append(b);
        }
        d[s] = v;
    }
    
    
    { // logs
        Json::Value a;
        
        if (d.isMember("logs")) {
            a = d["logs"];
        }

        s = "pgn";
        if (!a.isMember(s)) {
            Json::Value v;
            v["mode"] = true;
            v["path"] = path + folderSlash + "games.pgn";
            a[s] = v;
        }
        
        s = "result";
        if (!a.isMember(s)) {
            Json::Value v;
            v["mode"] = true;
            v["path"] = path + folderSlash + "resultlog.txt";
            a[s] = v;
        }
        
        s = "engine";
        if (!a.isMember(s)) {
            Json::Value v;
            v["mode"] = true;
            v["show time"] = true;
            v["path"] = path + folderSlash + "enginelog.txt";
            a[s] = v;
        }

        d["logs"] = a;
    }
}

bool TourMng::parseJsonAfterLoading(Json::Value& d)
{
    //
    // Most inportance
    //
    if (d.isMember("base")) {
        auto v = d["base"];

        auto s = v["type"].asString();
        for(int t = 0; tourTypeNames[t]; t++) {
            if (tourTypeNames[t] == s) {
                type = static_cast<TourType>(t);
                break;
            }
        }
        
        
        s = "resumable";
        resumable = v.isMember(s) ? v[s].asBool() : true;
        
        s = "games per pair";
        if (v.isMember(s)) {
            gameperpair = std::max(1, v[s].asInt());
        }
        
        s = "shuffle players";
        shufflePlayers = v.isMember(s) && v[s].asBool();
        
        s = "ponder";
        ponderMode = v.isMember(s) && v[s].asBool();
        
        s = "event";
        if (v.isMember(s)) {
            eventName = v[s].asString();
        }
        s = "site";
        if (v.isMember(s)) {
            siteName = v[s].asString();
        }
        
        s = "concurrency";
        if (v.isMember(s)) {
            gameConcurrency = std::max(1, v[s].asInt());
        }
    }
    
    // Engine configurations
    std::string enginConfigJsonPath = "./engines.json";
    bool enginConfigUpdate = false;
    auto s = "engine configurations";
    if (d.isMember(s)) {
        auto v = d[s];
        enginConfigUpdate = v["update"].isBool() && v["update"].asBool();
        enginConfigJsonPath = v["path"].asString();
    }
    
    if (enginConfigJsonPath.empty() || !ConfigMng::instance->loadFromJsonFile(enginConfigJsonPath) || ConfigMng::instance->empty()) {
        std::cerr << "Error: missing parametter \"" << s << "\" or the file is not existed" << std::endl;
        return false;
    }
    
    // Participants
    participantList.clear();
    if (d.isMember("players")) {
        const Json::Value& array = d["players"];
        for (int i = 0; i < int(array.size()); i++) {
            auto str = array[i].isString() ? array[i].asString() : "";
            if (!str.empty()) {
                if (ConfigMng::instance->isNameExistent(str)) {
                    participantList.push_back(str);
                } else {
                    std::cerr << "Error: player " << str << " (in \"players\") is not existent in engine configurations." << std::endl;
                }
            }
        }
    }
    
    // time control
    auto ok = false;
    s = "time control";
    if (d.isMember(s)) {
        auto obj = d[s];
        ok = timeController.load(obj) && timeController.isValid();
    }

    if (!ok) {
        std::cerr << "Error: missing parametter \"" << s << "\" or corrupted data" << std::endl;
        return false;
    }
    
    if (participantList.empty()) {
        std::cerr << "Warning: missing parametter \"players\". Will use all players in configure instead." << std::endl;
        participantList = ConfigMng::instance->nameList();
    }
    
    if (participantList.size() < 2) {
        std::cerr << "Error: number of players in parametter \"players\" is not enough for a tournament!" << std::endl;
        return false;
    }
    
    if (type == TourType::none) {
        std::cerr << "Error: missing parametter \"type\" or it is incorrect (should be \"roundrobin\", \"knockout\")!" << std::endl;
        return false;
    }
    
    //
    // Less inportance
    //
    s = "opening books";
    if (d.isMember(s)) {
        auto obj = d[s];
        bookMng.load(obj);
    }

    s = "logs";
    if (d.isMember(s)) {
        auto a = d[s];
        s = "pgn";
        if (a.isMember(s)) {
            auto v = a[s];
            pgnPathMode = v["mode"].asBool();
            pgnPath = v["path"].asString();
        }
        
        s = "result";
        if (a.isMember(s)) {
            auto v = a[s];
            logResultMode = v["mode"].asBool();
            logResultPath = v["path"].asString();
        }
        
        s = "engine";
        if (a.isMember(s)) {
            auto v = a[s];
            logEngineInOutMode = v["mode"].asBool();
            logEngineInOutShowTime = v.isMember("show time") && v["show time"].asBool();
            logEngineInOutPath = v["path"].asString();
        }
    }

    return true;
}


void TourMng::tickWork()
{
    playerMng.tick();
    
    std::vector<Game*> stoppedGameList;
    
    for(auto && game : gameList) {
        game->tick();
        auto st = game->getState();
        switch (st) {
            case GameState::stopped:
                game->setState(GameState::ending);
                matchCompleted(game);
                break;
                
            case GameState::ended:
                stoppedGameList.push_back(game);
                break;
            default:
                break;
        }
    }
    
    for(auto && game : stoppedGameList) {
        for(int sd = 0; sd < 2; sd++) {
            auto side = static_cast<Side>(sd);
            auto player = game->getPlayer(side);
            if (player) {
                auto player2 = game->deattachPlayer(side); assert(player == player2);
                playerMng.returnPlayer(player2);
            }
        }
        
        auto it = std::find(gameList.begin(), gameList.end(), game);
        if (it != gameList.end()) {
            gameList.erase(it);
        }
        delete game;
    }
    
    if (state == TourState::playing) {
        playMatches();
    }
}

static std::string bool2OnOffString(bool b)
{
    return b ? "on" : "off";
}

void TourMng::showPathInfo(const std::string& name, const std::string& path, bool mode)
{
    std::cout << " " << name << ": " << (path.empty() ? "<empty>" : path) << ", " << bool2OnOffString(mode) << std::endl;
}

void TourMng::startTournament()
{
    startTime = time(nullptr);
    
    std::string info =
    "type: " + std::string(tourTypeNames[static_cast<int>(type)])
    + ", timer: " + timeController.toString()
    + ", players: " + std::to_string(participantList.size())
    + ", matches: " + std::to_string(matchRecordList.size())
    + ", concurrency: " + std::to_string(gameConcurrency)
    + ", ponder: " + bool2OnOffString(ponderMode)
    + ", book: " + bool2OnOffString(!bookMng.isEmpty());
    
    matchLog(info);
    
    showPathInfo("pgn", pgnPath, pgnPathMode);
    showPathInfo("result", logResultPath, logResultMode);
    showPathInfo("engines", logEngineInOutPath, logEngineInOutMode);
    std::cout << std::endl;
    
    // tickWork will start the matches
    state = TourState::playing;

    mainTimerId = timer.add(std::chrono::milliseconds(500), [=](CppTime::timer_id) { tick(); }, std::chrono::milliseconds(500));
}

void TourMng::finishTournament()
{
    state = TourState::done;
    auto elapsed_secs = previousElapsed + static_cast<int>(time(nullptr) - startTime);
    
    if (!matchRecordList.empty()) {
        auto str = createTournamentStats();
        matchLog(str);
    }
    
    auto str = "Tournamemt finished! Elapsed: " + formatPeriod(elapsed_secs);
    matchLog(str);
    
    removeMatchRecordFile();
    
    // WARNING: exit the app here after completed the tournament
    shutdown();
    exit(0);
}

std::string TourMng::createTournamentStats()
{
    std::map<std::string, TourPlayer> resultMap;
    
    for(auto && m : matchRecordList) {
        if (m.resultType == ResultType::noresult) { // hm
            continue;
        }
        
        for(int sd = 0; sd < 2; sd++) {
            auto name = m.playernames[sd];
            if (name.empty()) { // lucky players (in knockout) won without opponents
                continue;
            }
            TourPlayer r;
            auto it = resultMap.find(name);
            if (it == resultMap.end()) {
                r.name = name;
            } else {
                r = it->second; assert(r.name == name);
            }
            
            r.gameCnt++;
            switch (m.resultType) {
                case ResultType::win:
                    if (sd == W) r.winCnt++; else r.lossCnt++;
                    break;
                case ResultType::draw:
                    r.drawCnt++;
                    break;
                case ResultType::loss:
                    if (sd == B) r.winCnt++; else r.lossCnt++;
                    break;
                default:
                    assert(false);
                    break;
            }
            resultMap[name] = r;
        }
    }
    
    auto maxNameLen = 0;
    std::vector<TourPlayer> resultList;
    for (auto && s : resultMap) {
        resultList.push_back(s.second);
        maxNameLen = std::max(maxNameLen, int(s.second.name.length()));
    }
    
    std::sort(resultList.begin(), resultList.end(), [](const TourPlayer& lhs, const TourPlayer& rhs)
              {
                  return rhs.smaller(lhs); // return lhs.smaller(rhs);
              });
    
    
    std::stringstream stringStream;
    
    
    auto separateLineSz = maxNameLen + 50;
    for(int i = 0; i < separateLineSz; i++) {
        stringStream << "-";
    }
    stringStream << std::endl;

    stringStream << "  #  "
    << std::left << std::setw(maxNameLen + 1) << "name"
    << "games     wins    draws   losses   score" << std::endl;
    
    for(int i = 0; i < resultList.size(); i++) {
        auto r = resultList.at(i);
        
        auto d = double(std::max(1, r.gameCnt));
        double win = double(r.winCnt * 100) / d, draw = double(r.drawCnt * 100) / d, loss = double(r.lossCnt * 100) / d;
        
        double score = double(r.winCnt) + double(r.drawCnt) / 2;
        //        auto errorMagins = calcErrorMargins(r.winCnt, r.drawCnt, r.lossCnt);
        stringStream
        << std::right << std::setw(3) << (i + 1) << ". "
        << std::left << std::setw(maxNameLen + 1) << r.name
        << std::right << std::setw(5) << r.gameCnt
        // << (errorMagins > 0 ? "+" : "") << errorMagins << " "
        << std::fixed << std::setprecision(1)
        << std::right << std::setw(8) << win << std::left << std::setw(0) << "%"
        << std::right << std::setw(8) << draw << std::left << std::setw(0) << "%"
        << std::right << std::setw(8) << loss << std::left << std::setw(0) << "%"
        << std::right << std::setw(8) << score << std::left << std::setw(0)
        << std::endl;
    }
    
    for(int i = 0; i < separateLineSz; i++) {
        stringStream << "-";
    }
    stringStream << std::endl << std::endl;

    return stringStream.str();
}

void TourMng::playMatches()
{
    if (matchRecordList.empty()) {
        return finishTournament();
    }
    
    if (gameList.size() >= gameConcurrency) {
        return;
    }
    
    for(auto && m : matchRecordList) {
        if (m.state != MatchState::none) {
            continue;
        }
        
        createMatch(m);
        assert(m.state != MatchState::none);
        
        if (gameList.size() >= gameConcurrency) {
            break;
        }
    }
    
    if (gameList.empty() && !createNextRoundMatches()) {
        return finishTournament();
    }
}

void TourMng::addMatchRecord(MatchRecord& record)
{
    record.pairId = std::rand();
    for(int i = 0; i < gameperpair; i++) {
        addMatchRecord_simple(record);
        record.swapPlayers();
    }
}

void TourMng::addMatchRecord_simple(MatchRecord& record)
{
    record.gameIdx = int(matchRecordList.size());
    bookMng.getRandomBook(record.startFen, record.startMoves);
    matchRecordList.push_back(record);
}

bool TourMng::createNextRoundMatches()
{
    switch (type) {
        case TourType::roundrobin:
            return false;
            
        case TourType::knockout:
            return createNextKnockoutMatchList();

        default:
            break;
    }
    return false;
}

// This function used to break the tie between a pair of players in knockout
// It is not a tie if one has more win or more white games
void TourMng::checkToExtendMatches(int gIdx)
{
    if (type != TourType::knockout || gIdx < 0) {
        return;
    }
    
    for(auto && r : matchRecordList) {
        if (r.gameIdx == gIdx) {
            TourPlayerPair playerPair;
            playerPair.pair[0].name = r.playernames[0];
            playerPair.pair[1].name = r.playernames[1];
            auto pairId = r.pairId;

            for(auto && rcd : matchRecordList) {
                if (rcd.pairId != pairId) {
                    continue;
                }
                
                // some matches are not completed -> no extend
                if (rcd.state != MatchState::completed) {
                    return;
                }
                if (rcd.resultType != ResultType::win && rcd.resultType != ResultType::loss) {
                    continue;
                }
                auto winnerName = rcd.playernames[(rcd.resultType == ResultType::win ? W : B)];
                playerPair.pair[playerPair.pair[W].name == winnerName ? W : B].winCnt++;
                
                auto whiteIdx = playerPair.pair[W].name == rcd.playernames[W] ? W : B;
                playerPair.pair[whiteIdx].whiteCnt++;
            }
            
            // It is a tie if two players have same wins and same times to play white
            if (playerPair.pair[0].winCnt == playerPair.pair[1].winCnt && playerPair.pair[0].whiteCnt == playerPair.pair[1].whiteCnt) {
                MatchRecord record = r;
                record.resultType = ResultType::noresult;
                record.state = MatchState::none;
                addMatchRecord_simple(record);
                
                auto str = "* Tied! Add one more game for " + record.playernames[W] + " vs " + record.playernames[B];
                matchLog(str);
            }
            break;
        }
    }
}

int TourMng::getLastRound() const
{
    int lastRound = 0;
    for(auto && r : matchRecordList) {
        lastRound = std::max(lastRound, r.round);
    }
    return lastRound;
}

std::vector<TourPlayer> TourMng::getKnockoutWinnerList()
{
    std::vector<TourPlayer> winList;
    auto lastRound = getLastRound();
    
    std::set<std::string> lostSet;
    std::vector<std::string> nameList;
    std::map<int, TourPlayerPair> pairMap;

    for(auto && r : matchRecordList) {
        if (r.round != lastRound) {
            continue;
        }
        
        assert(r.state == MatchState::completed);
        TourPlayerPair thePair;
        auto it = pairMap.find(r.pairId);
        if (it != pairMap.end()) thePair = it->second;
        else {
            thePair.pair[0].name = r.playernames[0];
            thePair.pair[1].name = r.playernames[1];
        }
        
        if (r.resultType == ResultType::win || r.resultType == ResultType::loss) {
            auto idxW = thePair.pair[W].name == r.playernames[W] ? W : B;
            auto winIdx = r.resultType == ResultType::win ? idxW : (1 - idxW);
            thePair.pair[winIdx].winCnt++;
        }
        auto whiteSd = thePair.pair[W].name == r.playernames[W] ? W : B;
        thePair.pair[whiteSd].whiteCnt++;
        pairMap[r.pairId] = thePair;
    }
    
    for(auto && p : pairMap) {
        auto thePair = p.second;
        assert(thePair.pair[0].winCnt != thePair.pair[1].winCnt || thePair.pair[0].whiteCnt != thePair.pair[1].whiteCnt);
        auto winIdx = W;
        if (thePair.pair[B].winCnt > thePair.pair[W].winCnt ||
            (thePair.pair[B].winCnt == thePair.pair[W].winCnt && thePair.pair[B].whiteCnt < thePair.pair[W].whiteCnt)) {
            winIdx = B;
        }
        winList.push_back(thePair.pair[winIdx]);
    }
    
    return winList;
}


bool TourMng::createNextKnockoutMatchList()
{
    auto winList = getKnockoutWinnerList();
    return createKnockoutMatchList(winList, getLastRound() + 1);
}

bool TourMng::createKnockoutMatchList(const std::vector<std::string>& nameList)
{
    std::vector<TourPlayer> vec;
    for(auto && name : nameList) {
        TourPlayer tourPlayer;
        tourPlayer.name = name;
        vec.push_back(tourPlayer);
    }

    return createKnockoutMatchList(vec, 0);
}

bool TourMng::createKnockoutMatchList(std::vector<TourPlayer> playerVec, int round)
{
    if (playerVec.size() < 2) {
        if (playerVec.size() == 1) {
            auto str = "\n* The winner is " + playerVec.front().name;
            matchLog(str);
        }
        return false;
    }
    // odd players, one won't have opponent and he is lucky to set win
    if (playerVec.size() & 1) {
        
        std::set<std::string> luckSet;
        for(auto && r : matchRecordList) {
            if (r.playernames[0].empty() || r.playernames[1].empty()) {
                auto idx = r.playernames[0].empty() ? 1 : 0;
                luckSet.insert(r.playernames[idx]);
            }
        }
        
        TourPlayer luckPlayer;
        for (int i = 0; i < 10; i++) {
            auto k = std::rand() % playerVec.size();
            if (luckSet.find(playerVec.at(k).name) == luckSet.end()) {
                luckPlayer = playerVec.at(k);
                
                auto it = playerVec.begin();
                std::advance(it, k);
                playerVec.erase(it);
                break;
            }
        }
        
        if (playerVec.size() & 1) {
            luckPlayer = playerVec.front();
            playerVec.erase(playerVec.begin());
        }
        
        // the odd and the lucky player wins all games in the round
        MatchRecord record(luckPlayer.name, "", false);
        record.round = round;
        record.state = MatchState::completed;
        record.resultType = ResultType::win; // win
        record.pairId = std::rand();
        addMatchRecord_simple(record);
        
        auto str = "\n* Player " + luckPlayer.name + " is an odd (no opponent in " + std::to_string(playerVec.size()) + " players) and set won for round " + std::to_string(round + 1);
        matchLog(str);
    }
    
    std::sort(playerVec.begin(), playerVec.end(), [](const TourPlayer& lhs, const TourPlayer& rhs)
              {
                  return lhs.elo > rhs.elo;
              });
    
    auto n = playerVec.size() / 2;
    
    auto addCnt = 0;
    for(int i = 0; i < n; i++) {
        auto name0 = playerVec.at(i).name;
        auto name1 = playerVec.at(i + n).name;
        
        // random swap to avoid name0 player plays all white side
        MatchRecord record(name0, name1, rand() & 1);
        record.round = round;
        addMatchRecord(record);
        addCnt++;
    }

    auto str = "\nKnockout round: " + std::to_string(round + 1) + ", pairs: " + std::to_string(n) + ", matches: " + std::to_string(uncompletedMatches());

    matchLog(str);
    return addCnt > 0;
}

void TourMng::reset()
{
    matchRecordList.clear();
    previousElapsed = 0;
}

bool TourMng::createMatchList()
{
    return createMatchList(participantList, type);
}

bool TourMng::createMatchList(std::vector<std::string> nameList, TourType tourType)
{
    reset();
    
    if (nameList.size() < 2) {
        std::cerr << "Error: not enough players (" << nameList.size() << ") and/or unknown tournament type" << std::endl;
        return false;
    }
    
    if (shufflePlayers) {
        auto rng = std::default_random_engine {};
        std::shuffle(std::begin(nameList), std::end(nameList), rng);
    }
    
    std::string missingName;
    auto err = false;
    switch (tourType) {
        case TourType::roundrobin:
        {
            for(int i = 0; i < nameList.size() - 1 && !err; i++) {
                auto name0 = nameList.at(i);
                if (!ConfigMng::instance->isNameExistent(name0)) {
                    err = true; missingName = name0;
                    break;
                }
                for(int j = i + 1; j < nameList.size(); j++) {
                    auto name1 = nameList.at(j);
                    if (!ConfigMng::instance->isNameExistent(name1)) {
                        err = true; missingName = name1;
                        break;
                    }
                    
                    // random swap to avoid name0 player plays all white side
                    MatchRecord record(name0, name1, rand() & 1);
                    record.round = 1;
                    addMatchRecord(record);
                }
            }
            
            if (err) {
                
            }
            break;
        }
        case TourType::knockout:
        {
            createKnockoutMatchList(nameList);
            break;
        }
            
        default:
            break;
    }
    
    if (err) {
        std::cerr << "Error: missing engine configuration for name (case sensitive): " << missingName << std::endl;
        return false;
    }
    
    saveMatchRecords();
    return true;
}

void TourMng::createMatch(MatchRecord& record)
{
    if (!record.isValid() ||
        !createMatch(record.gameIdx, record.playernames[W], record.playernames[B], record.startFen, record.startMoves)) {
        std::cerr << "Error: match record invalid or missing players " << record.toString() << std::endl;
        record.state = MatchState::error;
        return;
    }
    
    record.state = MatchState::playing;
}

bool TourMng::createMatch(int gameIdx, const std::string& whiteName, const std::string& blackName,
                          const std::string& startFen, const std::vector<Move>& startMoves)
{
    Engine* engines[2];
    engines[W] = playerMng.createEngine(whiteName);
    engines[B] = playerMng.createEngine(blackName);
    
    if (engines[0] && engines[1]) {
        auto game = new Game(engines[W], engines[B], timeController, ponderMode);
        game->setStartup(gameIdx, startFen, startMoves);
        
        if (addGame(game)) {
            game->setMessageLogger([=](const std::string& name, const std::string& line, LogType logType) {
                engineLog(gameIdx, name, line, logType);
            });
            game->kickStart();
            
            std::string infoString = std::to_string(gameIdx + 1) + ". " + game->getGameTitleString();
            
            printText(infoString);
            engineLog(gameIdx, getAppName(), "\n" + infoString + "\n", LogType::system);
            
            return true;
        }
        delete game;
    }
    
    for(int sd = 0; sd < 2; sd++) {
        playerMng.returnPlayer(engines[sd]);
    }
    return false;
}

// TODO: make it work!
int TourMng::calcErrorMargins(int w, int d, int l)
{
    return 0;
}

void TourMng::matchCompleted(Game* game)
{
    if (game == nullptr) return;
    
    auto gIdx = game->getIdx();
    if (gIdx >= 0 && gIdx < matchRecordList.size()) {
        auto record = &matchRecordList[gIdx];
        assert(record->state == MatchState::playing);
        record->state = MatchState::completed;
        record->resultType = game->board.result.result;
        assert(matchRecordList[gIdx].resultType == game->board.result.result);
        
        if (pgnPathMode && !pgnPath.empty()) {
            auto pgnString = game->toPgn(eventName, siteName, record->round, record->gameIdx);
            append2TextFile(pgnPath, pgnString);
        }
    }
    
    if (logResultMode || banksiaVerbose) { // log
        auto wplayer = game->getPlayer(Side::white), bplayer = game->getPlayer(Side::black);
        if (wplayer && bplayer) {
            std::string infoString = std::to_string(gIdx + 1) + ") " + game->getGameTitleString() + ", #" + std::to_string(game->board.histList.size()) + ", " + game->board.result.toString();
            
            matchLog(infoString);
            // Add extra info to help understanding log
            engineLog(game->getIdx(), getAppName(), infoString, LogType::system);
        }
    }
    
    checkToExtendMatches(gIdx);
    
    saveMatchRecords();
}

void TourMng::setupTimeController(TimeControlMode mode, int val, double t0, double t1, double t2)
{
    timeController.setup(mode, val, t0, t1, t2);
}

bool TourMng::addGame(Game* game)
{
    if (game == nullptr) return false;
    gameList.push_back(game);
    return true;
}

void TourMng::setEngineLogMode(bool enabled)
{
    logEngineInOutMode = enabled;
}

void TourMng::setEngineLogPath(const std::string& path)
{
    logEngineInOutPath = path;
}

void TourMng::matchLog(const std::string& infoString)
{
    printText(infoString);
    
    if (logResultMode && !logResultPath.empty()) {
        std::lock_guard<std::mutex> dolock(matchMutex);
        append2TextFile(logResultPath, infoString);
    }
}

void TourMng::showEgineInOutToScreen(bool enabled)
{
    logScreenEngineInOutMode = enabled;
}

void TourMng::engineLog(int gameIdx, const std::string& name, const std::string& line, LogType logType)
{
    if (line.empty() || !logEngineInOutMode || logEngineInOutPath.empty()) return;
    
    std::ostringstream stringStream;
    
    if (gameIdx >= 0 && gameConcurrency > 1) {
        stringStream << (gameIdx + 1) << ".";
    }
    
    if (logEngineInOutShowTime) {
        auto tm = localtime_xp(std::time(0));
        stringStream << std::put_time(&tm, "%H:%M:%S ");
    }
    stringStream << name << (logType == LogType::toEngine ? "< " : "> ") << line;
    
    auto str = stringStream.str();
    
    if (logScreenEngineInOutMode) {
        printText(str);
    }
    
    std::lock_guard<std::mutex> dolock(logMutex);
    append2TextFile(logEngineInOutPath, str);
}

void TourMng::append2TextFile(const std::string& path, const std::string& str)
{
    std::ofstream ofs(path, std::ios_base::out | std::ios_base::app);
    ofs << str << std::endl;
    ofs.close();
}


void TourMng::shutdown()
{
    timer.remove(mainTimerId);
    playerMng.shutdown();
}

int TourMng::uncompletedMatches()
{
    auto cnt = 0;
    for(auto && r : matchRecordList) {
        if (r.state == MatchState::none) {
            cnt++;
        }
    }
    return cnt;
}

const std::string matchPath = "./playing.json";

void TourMng::removeMatchRecordFile()
{
    std::remove(matchPath.c_str());
}

void TourMng::saveMatchRecords()
{
    if (!resumable) {
        return;
    }
    
    Json::Value d;

    d["type"] = tourTypeNames[static_cast<int>(type)];

    d["timeControl"] = timeController.saveToJson();

    Json::Value a;
    for(auto && r : matchRecordList) {
        a.append(r.saveToJson());
    }
    d["recordList"] = a;
    d["elapsed"] = static_cast<int>(time(nullptr) - startTime);

    JsonSavable::saveToJsonFile(matchPath, d);
}

bool TourMng::loadMatchRecords(bool autoYesReply)
{
    Json::Value d;
    if (!resumable || !loadFromJsonFile(matchPath, d, false)) {
        return false;
    }
    
    auto uncompletedCnt = 0;
    std::vector<MatchRecord> recordList;
    auto array = d["recordList"];
    for(int i = 0; i < int(array.size()); i++) {
        auto v = array[i];
        MatchRecord record;
        if (record.load(v)) {
            recordList.push_back(record);
            if (record.state == MatchState::none) {
                uncompletedCnt++;
            }
        }
    }
    
    if (uncompletedCnt == 0) {
        removeMatchRecordFile();
        return false;
    }

    std::cout << "\nThere are " << uncompletedCnt << " (of " << recordList.size() << ") uncompleted matches from previous tournament! Do you want to resume? (y/n)" << std::endl;

    while (!autoYesReply) {
        std::string line;
        std::getline(std::cin, line);
        banksia::trim(line);
        if (line.empty()) {
            continue;
        }

        if (line == "n" || line == "no") {
            removeMatchRecordFile();
            std::cout << "Discarded last tournament!" << std::endl;
            return false;
        }
        if (line == "y" || line == "yes") {
            break;
        }
    }
    
    std::cout << "Tournament resumed!" << std::endl;
    
    matchRecordList = recordList;
    
    auto first = matchRecordList.front();
    
    if (d.isMember("type")) {
        auto s = d["type"].asString();
        for(int t = 0; tourTypeNames[t]; t++) {
            if (tourTypeNames[t] == s) {
                type = static_cast<TourType>(t);
            }
        }
    }

    assert(timeController.isValid());
    
    auto s = "timeControl";
    if (d.isMember(s)) {
        auto obj = d[s];
        auto oldTimeControl = timeController.saveToJson();
        if (!timeController.load(obj) || !timeController.isValid()) {
            timeController.load(oldTimeControl);
        }
    }

    assert(timeController.isValid());
    previousElapsed += d["elapsed"].asInt();

    removeMatchRecordFile();
    
    startTournament();
    return true;
}
