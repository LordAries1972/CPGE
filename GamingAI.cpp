// -------------------------------------------------------------------------------------------------------------
// GamingAI.cpp - Comprehensive Gaming AI Intelligence System Implementation
// Section 1: Basic Structure, Constructor, Destructor and Core Initialization
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "GamingAI.h"
#include "ThreadLockHelper.h"
#include "MathPrecalculation.h"
#include "BuildInfo.h"

#if defined(_MSC_VER)
    #include <intrin.h>
#endif
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <stdexcept>

// External references required by the GamingAI system
extern Debug debug;
extern ThreadManager threadManager;
extern GamePlayer gamePlayer;

#if defined(__USING_JOYSTICKS__)
    extern Joystick js;
#endif

// Suppress specific warnings for optimized assembly-equivalent code
#pragma warning(push)
#pragma warning(disable: 4996)  // Suppress deprecated function warnings for intrinsics
#pragma warning(disable: 4127)  // Suppress conditional expression is constant for template optimizations
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

//==============================================================================
// Constructor and Destructor Implementation
//==============================================================================

// Constructor - Initialize GamingAI system with default values
// Shared session recorder and CPGE-AI-LISTENER implementation. The render thread
// submits fixed-size events; only the existing AI worker touches files or learns.
// A full queue reports a recording fault; commands are rejected unless their
// acceptance can be journalled. No disk IO or model traversal is on a game path.
class GamingAI::GameHistory {
public:
    enum Kind : uint8_t { EngineStart, Begin, Update, End, Reset, Request, Command, Result, EngineStop, BossDefeat, Decision, BoardRequest, BoardSuggestion, BoardMove, Playback, InventoryDirective, InventoryResult, RecoveryRequest, RecoveryResult };
    struct Event {
        Kind kind = Update;
        AIGameEnd reason = AIGameEnd::Reset;
        AIGameFrame frame;
        uint64_t session = 0, sequence = 0, area = 0, order = 0;
        char playerName[96] = {};
        AIBoardStatus board;
        int64_t utc = 0;
        AIGameCommand command = AIGameCommand::None;
        float value = 0;
        bool accepted = false, direct = false;
        std::array<float, 4> policy{};
        uint32_t learnedSessions = 0;
        uint64_t drops = 0;
    };
    struct Session {
        uint64_t id = 0;
        int scene = -1, player = 1, score = 0, wave = 1;
        int64_t start = 0, end = 0;
        double seconds = 0, moving = 0, firing = 0, mx = 0, my = 0, turns = 0;
        uint64_t frames = 0;
        float previousX = 0, previousY = 0;
        bool demo = false, havePosition = false;
        double stuckSeconds = 0; float px = 0, py = 0;
        uint32_t boardMoves = 0, boardCaptures = 0, boardCentres = 0;
        uint32_t bossDefeats = 0, livesLost = 0; int previousLives = -1;
        std::string playerName;
        uint64_t observations = 0, playback = 0, firstOrder = 0, lastOrder = 0, startDrops = 0, writer = 0;
        double idleSeconds = 0, healthLost = 0;
        float previousHealth = -1;
        double dangerSeconds = 0, evasionSeconds = 0;
        std::array<int,3> build {CURRENT_BUILD_VERSION,CURRENT_BUILD_SUBVERSION,CURRENT_BUILD};
        float cursorX=0,cursorY=0,carryMass=-1;
        AIReplayContext replay;               // newest presentation context seen in this session (worker-owned)
        int inventoryCount=-1; bool haveCursor=false,inventoryOpen=false;
        bool traceComplete = true, finished = false;
        // Worker-owned starting facts survive checkpoint/reload for quick review.
        nlohmann::json initialState;
    };
    struct Pending { int scene = -1, player = 1; AIGameCommand command = AIGameCommand::None; float value = 0; uint64_t sequence = 0; };
    static constexpr int64_t Retention = 21LL * 24 * 60 * 60 * 1000;
    static constexpr size_t Capacity = 16384;
    std::array<Event, Capacity> queue;
    size_t head = 0, size = 0;
    std::mutex queueMutex, cacheMutex, lifecycleMutex;
    std::array<Event,256> retryBatch; size_t retryCount = 0, retryOffset = 0;
    uint64_t lastForwarded = 0, lastAppliedEvent = 0;
    std::array<Pending, 64> commands{};
    size_t commandHead = 0, commandSize = 0, awaitingAudit = 0;
    std::atomic<bool> healthy{ true }, ready{ false }, adapt{ true };
    std::atomic<uint64_t> dropped{ 0 }, nextSequence{ 1 }, nextEvent{ 1 };
    const int64_t bootUtc = Now();
    const uint64_t writer = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    uint64_t unverifiedRecordings = 0;
    uint64_t lastExternal = 0;
    bool record = true, recall = true, restartEnabled = true, active = false, wasOver = false;
    bool skipRecall = false, currentDemo = true, resetExpected = false, confirmArmed = false;
    int scene = -1, player = 1, stage = 0;
    float countdown = 0;
    uint64_t session = 0;
    AIGameFrame last;
    std::filesystem::path directory = AssetsDir / "AIHistory";
    std::ofstream journal;
    int64_t journalHour = -1, lastPrune = 0, lastCheckpoint = 0;
    Session working;
    std::vector<Session> sessions;
    std::unordered_map<uint64_t, AIGameStrategy> strategies, demoStrategies;
    // One outstanding recovery job bounds both work and memory. The render
    // thread uses try-locks; it keeps its safe local planner while waiting.
    std::mutex recoveryMutex;
    AIRecoveryJob recoveryJob;
    AIRecoveryPlan recoveryReply;
    bool recoveryBusy = false, recoveryQueued = false, recoveryReady = false;

    static AIRecoveryPlan SolveRecovery(const AIRecoveryJob& job) {
        AIRecoveryPlan result=job.plan;
        float minimum=1.0e30f;
        for (int i=0;i<job.count;++i) {
            const auto& c=job.candidates[i];
            if (c.clear) minimum=(std::min)(minimum,c.danger);
        }
        float best=-1.0e30f;
        for (int i=0;i<job.count;++i) {
            const auto& c=job.candidates[i];
            if (!c.clear || c.danger>minimum+0.001f) continue;
            const float distance=std::hypot(c.x-result.startX,c.y-result.startY);
            if (distance<0.25f) continue;
            // Escape available space first; a failed direct goal must not keep
            // winning over a clear detour. Danger has already been minimized.
            const float score=distance-.15f*std::hypot(job.goalX-c.x,job.goalY-c.y);
            if (score>best) { best=score; result.x=c.x; result.y=c.y; result.found=true; }
        }
        return result;
    }
    void ProcessRecovery() {
        AIRecoveryJob job;
        {
            std::lock_guard<std::mutex> lock(recoveryMutex);
            if (!recoveryQueued) return;
            job=recoveryJob; recoveryQueued=false;
        }
        const auto plan=SolveRecovery(job);
        Event event; event.kind=RecoveryResult; event.sequence=plan.request; event.session=plan.session;
        event.frame.scene=plan.scene; event.frame.player=plan.player; event.frame.demo=true; event.area=plan.revision;
        event.policy={plan.startX,plan.startY,plan.x,plan.y}; event.accepted=plan.found;
        if (!Push(event)) {
            std::lock_guard<std::mutex> lock(recoveryMutex);
            if (recoveryJob.plan.request==plan.request) recoveryBusy=false;
            return;
        }
        std::lock_guard<std::mutex> lock(recoveryMutex);
        if (recoveryBusy && recoveryJob.plan.request==plan.request) { recoveryReply=plan; recoveryReady=true; }
    }
    std::array<AIBoardStatus,3> boardStatus;
    // 2048 fixed slots exceed the 800 boss encounters possible in a 1000-wave run.
    // Open addressing avoids a heap allocation on every boss death.
    std::array<uint64_t,2048> defeated{};
    bool encounterOverflow = false;
    void ClearDefeats() { defeated.fill(0); encounterOverflow=false; }
    int FindDefeat(uint64_t key, bool insert)
    {
        const uint64_t encoded=key+1; if (!encoded) return -1;
        size_t slot=static_cast<size_t>((key^(key>>29))*0x9E3779B97F4A7C15ull)&(defeated.size()-1);
        for (size_t n=0;n<defeated.size();++n)
        {
            if (defeated[slot]==encoded) return 1;
            if (!defeated[slot]) { if (insert) defeated[slot]=encoded; return insert ? 2 : 0; }
            slot=(slot+1)&(defeated.size()-1);
        }
        if (insert) { encounterOverflow=true; healthy.store(false); }
        return -1;
    }

    static int64_t Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
    static uint64_t Key(int s, int p) { return (static_cast<uint64_t>(static_cast<uint32_t>(p)) << 32) | static_cast<uint32_t>(s); }
    static bool Valid(int s, int p) { return s >= 0 && s < 64 && p >= 0 && p < 256; }
    static std::string Name(const Session& s) { return "game-" + std::to_string(s.scene) + "-player-" + std::to_string(s.player) + "-" + std::to_string(s.id); }

    static const char* GameId(int scene) {
        static constexpr const char* ids[] = {"asteroids","guardian","scoptiliptic","scramble","chomper","planet-assault",
            "neon-arena","trench-run","voxel-guard","chomper-3d","marble-madness","jumpman","gyrospace","dungeon",
            "spiral-tunnel","morpheous","dominion","project-x","space-spartans","incoming","stix","tempest","battle-zone",
            "tutankham","decention","burger-master","dungeon-crawl","racing","chess","chess-3d","xiangqi","soldier-on"};
        return scene >= 5 && scene <= 36 ? ids[scene-5] : "engine";
    }
    static nlohmann::json FrameLayout() {
        return {"dt","score","lives","wave","mode","x","y","moveX","moveY","aimX","aimY","buttons","playing",
            "hasPosition","over","complete","floor","area","height","health","armour","weapon","ammo","hostiles","cursorX","cursorY","actions","playback",
            "danger","carryMass","carryCapacity","inventoryCount","inventoryOpen"};
    }
    static nlohmann::json Identity(const char* type, int scene, int player, bool demo) {
        return {{"schema",2},{"format","CPGE.GamingAI"},{"systemId","CPGE2026-Vectors"},{"recordType",type},
            {"gameId",GameId(scene)},{"scene",scene},{"player",player},{"demo",demo},{"source",demo?"demo":"human"},
            {"build",{CURRENT_BUILD_VERSION,CURRENT_BUILD_SUBVERSION,CURRENT_BUILD}},{"coordinateSystem","game-adapter"}};
    }
    static uint64_t Checksum(nlohmann::json value) {
        value.erase("checksum");
        const std::string canonical = value.dump(); uint64_t hash = 14695981039346656037ull;
        for (unsigned char ch : canonical) { hash ^= ch; hash *= 1099511628211ull; }
        return hash; // Corruption detection, not a cryptographic authenticity claim.
    }
    static void Seal(nlohmann::json& value) { value["checksum"] = Checksum(value); }
    static bool Compatible(const nlohmann::json& value, int expectedScene = -1) {
        try {
            const int scene = value.at("scene").get<int>(), player = value.at("player").get<int>();
            if (value.at("schema") != 2 || value.at("format") != "CPGE.GamingAI" || value.at("systemId") != "CPGE2026-Vectors"
                || (expectedScene >= 0 && scene != expectedScene) || value.at("gameId") != GameId(scene)
                || value.at("source") != (value.at("demo").get<bool>() ? "demo" : "human")
                || !value.at("build").is_array() || value.at("build").size() != 3) return false;
            const std::string type = value.at("recordType").get<std::string>();
            const bool engineEvent = type == "event" && scene == -1 && (value.at("kind") == EngineStart || value.at("kind") == EngineStop);
            if (type != "journal-header" && !engineEvent && !Valid(scene,player)) return false;
            if (!value.at("writerId").get<uint64_t>()) return false;
            if ((type == "session" || type == "journal-header") && value.at("frameLayout") != FrameLayout()) return false;
            if (type == "session") {
                const double seconds = value.at("seconds").get<double>();
                if (scene < 5 || scene > 36 || !value.at("id").get<uint64_t>() || value.at("endUtcMs").get<int64_t>() < value.at("startUtcMs").get<int64_t>()
                    || !std::isfinite(seconds) || seconds < 0) return false;
            } else if (type == "event") {
                if (!value.at("eventOrder").get<uint64_t>()) return false;
                if (value.contains("frame") && (!value.at("frame").is_array() || value.at("frame").size() != FrameLayout().size())) return false;
                if (value.contains("frame")) for (const auto& field : value.at("frame"))
                    if ((!field.is_number() && !field.is_boolean()) || (field.is_number_float() && !std::isfinite(field.get<double>()))) return false;
            } else if (type != "journal-header" && type != "playback-manifest") return false;
            return value.at("checksum").get<uint64_t>() == Checksum(value);
        } catch (...) { return false; }
    }

    bool Push(Event event) {
        event.utc = Now();
        event.drops = dropped.load();
        std::lock_guard<std::mutex> lock(queueMutex);
        if (size == Capacity) { healthy.store(false); dropped.fetch_add(1); return false; }
        event.order = nextEvent.fetch_add(1);
        if (!event.sequence) event.sequence = nextSequence.fetch_add(1);
        queue[(head + size) % Capacity] = event; ++size;
        return true;
    }
    Event Make(Kind kind) const { Event e; e.kind = kind; e.session = session; e.frame = last; return e; }
    void Finish(AIGameEnd reason) {
        if (active) { Event e = Make(End); e.reason = reason; Push(e); active = false; }
    }
    void Start(int s, int p, bool demo) {
        Finish(AIGameEnd::Reset);
        scene = s; player = p; currentDemo = demo; wasOver = false;
        { std::lock_guard<std::mutex> lock(recoveryMutex); recoveryBusy=recoveryQueued=recoveryReady=false; }
        last = AIGameFrame(); last.scene = s; last.player = p; last.demo = demo;
        // UTC microseconds plus monotonic local sequence gives named sessions across
        // process restarts without using the platform's locale/time-zone formatting.
        session = (std::max)(session + 1, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
        ClearDefeats();
        confirmArmed = false; active = record; stage = !demo && recall && !skipRecall ? 1 : 0; countdown = 15.0f; skipRecall = false;
        if (active) {
            Event e=Make(Begin);
            const PlayerInfo* info=gamePlayer.GetPlayerInfo(p);
            if (info) {
                size_t bytes=(std::min)(info->playerName.size(),sizeof(e.playerName)-1);
                if (bytes<info->playerName.size()) while (bytes && (static_cast<unsigned char>(info->playerName[bytes])&0xC0)==0x80) --bytes;
                std::memcpy(e.playerName,info->playerName.data(),bytes);
            }
            Push(e);
        }
    }
    AIGameStrategy Strategy(int s, int p, bool demo) {
        AIGameStrategy result; result.ready = ready.load();
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto& table = demo ? demoStrategies : strategies;
        const auto it = table.find(Key(s,p));
        if (it != table.end()) result = it->second;
        // Autonomous demos always consume core advice. The adaptation setting
        // controls human challenge adjustment, not the selected demo controller.
        if (!demo && !adapt.load()) {
            AIGameStrategy neutral; neutral.ready=result.ready; neutral.sessions=result.sessions;
            neutral.recentSessions=result.recentSessions; neutral.recentScore=result.recentScore; result=neutral;
        }
        return result;
    }
    AIGameFlow Advance(int s, bool demo, bool reset, float dt, bool confirm, bool cancel, bool allow, int p) {
        if (!Valid(s,p)) { Finish(AIGameEnd::SceneChange); stage = 0; scene = -1; return {}; }
        if (s != scene) Finish(AIGameEnd::SceneChange);
        resetExpected = s != scene || demo != currentDemo || reset;
        if (resetExpected) Start(s,p,demo);
        AIGameFlow out; out.stage = stage; out.seconds = countdown; out.hold = stage != 0;
        if (!stage) return out;
        if (!allow) { confirmArmed=false; return out; }
        if (!confirm) confirmArmed=true;
        countdown = (std::max)(0.0f, countdown - (std::clamp)(dt,0.0f,0.25f));
        if (cancel || (confirm && confirmArmed) || countdown <= 0) {
            Event decision=Make(Decision); decision.value=static_cast<float>(stage); decision.accepted=!cancel;
            decision.reason=cancel ? AIGameEnd::Abort : AIGameEnd::Reset; Push(decision);
        }
        if (cancel && stage == 2) { Finish(AIGameEnd::Abort); stage = 0; out.cancel = true; out.hold = false; }
        else if (cancel || (confirm && confirmArmed) || countdown <= 0) {
            const int previous = stage; stage = 0; out.hold = false;
            if (previous == 2) { out.restart = true; skipRecall = true; }
        }
        out.seconds = countdown; return out;
    }
    // Newest presentation context per scene (index 0 human, 1 demo). `live` is touched on the game thread under lifecycleMutex; `persisted` is filled by the worker from the
    // replay-context.json file (cacheMutex) so a context survives a restart.
    std::array<std::array<AIReplayContext,2>,64> live{}, persisted{};
    nlohmann::json replayFile = nlohmann::json::object();
    static bool SameReplay(const AIReplayContext& a, const AIReplayContext& b) {
        return a.valid==b.valid && a.mode==b.mode && a.view==b.view && a.weather==b.weather && a.seed==b.seed && std::strncmp(a.track,b.track,sizeof(a.track))==0
            && std::fabs(a.yaw-b.yaw)<5.0f && std::fabs(a.pitch-b.pitch)<5.0f && std::fabs(a.distance-b.distance)<1.0f && std::fabs(a.timeOfDay-b.timeOfDay)<0.05f
            && std::fabs(a.musicSeconds-b.musicSeconds)<10.0f;
    }
    static nlohmann::json ReplayJson(const AIReplayContext& c) {
        char track[sizeof(c.track)+1]={}; std::memcpy(track,c.track,sizeof(c.track));
        return {{"mode",c.mode},{"view",c.view},{"weather",c.weather},{"yaw",c.yaw},{"pitch",c.pitch},{"distance",c.distance},{"timeOfDay",c.timeOfDay},
            {"musicSeconds",c.musicSeconds},{"seed",c.seed},{"track",std::string(track)}};
    }
    static AIReplayContext ReplayFromJson(const nlohmann::json& j) {
        AIReplayContext c; if (!j.is_object()) return c;
        c.mode=j.value("mode",0); c.view=j.value("view",0); c.weather=j.value("weather",0); c.yaw=j.value("yaw",0.0f); c.pitch=j.value("pitch",0.0f); c.distance=j.value("distance",0.0f);
        c.timeOfDay=j.value("timeOfDay",0.0f); c.musicSeconds=j.value("musicSeconds",0.0f); c.seed=j.value("seed",0u);
        const std::string t=j.value("track",std::string()); const size_t n=(std::min)(t.size(),sizeof(c.track)-1); std::memcpy(c.track,t.c_str(),n); c.track[n]=0; c.valid=true; return c;
    }
    void Observe(AIGameFrame frame) {
        last = frame;
        if (frame.replay.valid && frame.scene>=0 && frame.scene<64) live[frame.scene][frame.demo?1:0]=frame.replay;
        if (record && session && active) { Event e = Make(Update); Push(e); }
        if (frame.complete) Finish(AIGameEnd::Complete);
        if (frame.over && !wasOver) {
            Finish(AIGameEnd::GameOver);
            if (!frame.demo && restartEnabled) { stage = 2; countdown = 15.0f; confirmArmed=false; }
        }
        wasOver = frame.over;
    }
    bool Submit(int s, AIGameCommand command, float value, uint64_t sequence, int p) {
        Event e; e.kind = Command; e.frame.scene = s; e.frame.player = p; e.command = command; e.value = value; e.sequence = sequence; e.direct = true;
        const bool valid = Valid(s,p) && std::isfinite(value) && command >= AIGameCommand::Pause && command <= AIGameCommand::Aggression;
        std::lock_guard<std::mutex> lock(queueMutex);
        #if defined(DEFINE_EXT_CMD_SEQUENCE)
            e.accepted = ready.load() && valid && sequence > lastExternal && commandSize + awaitingAudit < commands.size();
        #else
            (void)valid;
            e.accepted = false;
        #endif
        e.utc = Now(); e.order = nextEvent.fetch_add(1);
        if (size == Capacity) { healthy.store(false); dropped.fetch_add(1); return false; }
        queue[(head+size)%Capacity] = e; ++size;
        if (e.accepted) {
            lastExternal = sequence;
            ++awaitingAudit;
        }
        return e.accepted;
    }
    static nlohmann::json Summary(const Session& s, bool completed, int reason) {
        nlohmann::json value = { {"schema",2},{"analysis","unique-analysis"},{"playerName",s.playerName},{"name",Name(s)},{"id",s.id},{"scene",s.scene},{"player",s.player},{"startUtcMs",s.start},{"endUtcMs",s.end},
            {"demo",s.demo},{"finished",completed},{"reason",reason},{"score",s.score},{"wave",s.wave},{"seconds",s.seconds},{"frames",s.frames},
            {"movingSeconds",s.moving},{"firingSeconds",s.firing},{"moveX",s.mx},{"moveY",s.my},{"turns",s.turns},{"bossDefeats",s.bossDefeats},{"stuckSeconds",s.stuckSeconds},{"livesLost",s.livesLost},{"boardMoves",s.boardMoves},{"boardCaptures",s.boardCaptures},{"boardCentres",s.boardCentres} };
        value.update(Identity("session",s.scene,s.player,s.demo));
        value["frameLayout"]=FrameLayout(); value["observations"]=s.observations; value["playback"]=s.playback;
        value["writerId"]=s.writer;
        value["build"]=s.build; value["dangerSeconds"]=s.dangerSeconds; value["evasionSeconds"]=s.evasionSeconds;
        value["firstEventOrder"]=s.firstOrder; value["lastEventOrder"]=s.lastOrder;
        value["idleSeconds"]=s.idleSeconds; value["healthLost"]=s.healthLost;
        value["traceComplete"]=s.traceComplete; value["eligibleForLearning"]=completed && s.traceComplete && s.frames>0;
        value["initialState"]=s.initialState;
        if (s.replay.valid) value["replayContext"]=ReplayJson(s.replay);          // how the session was being presented (mode, camera, world, music + time index)
        Seal(value); return value;
    }
    static Session Decode(const nlohmann::json& j) {
        if (!Compatible(j) || j.at("recordType")!="session") throw std::runtime_error("Incompatible AI recording");
        Session s; s.playerName=j.value("playerName",std::string()); s.id=j.at("id").get<uint64_t>(); s.scene=j.at("scene").get<int>(); s.player=j.value("player",1);
        s.start=j.at("startUtcMs").get<int64_t>(); s.end=j.value("endUtcMs",s.start); s.demo=j.value("demo",false);
        s.score=j.value("score",0); s.wave=j.value("wave",1); s.seconds=j.value("seconds",0.0); s.frames=j.value("frames",uint64_t(0));
        s.moving=j.value("movingSeconds",0.0); s.firing=j.value("firingSeconds",0.0); s.mx=j.value("moveX",0.0); s.my=j.value("moveY",0.0); s.turns=j.value("turns",0.0); s.bossDefeats=j.value("bossDefeats",0u); s.stuckSeconds=j.value("stuckSeconds",0.0); s.livesLost=j.value("livesLost",0u); s.boardMoves=j.value("boardMoves",0u); s.boardCaptures=j.value("boardCaptures",0u); s.boardCentres=j.value("boardCentres",0u);
        s.observations=j.value("observations",uint64_t(0)); s.playback=j.value("playback",uint64_t(0));
        s.firstOrder=j.value("firstEventOrder",uint64_t(0)); s.lastOrder=j.value("lastEventOrder",uint64_t(0));
        s.idleSeconds=j.value("idleSeconds",0.0); s.healthLost=j.value("healthLost",0.0); s.traceComplete=j.value("traceComplete",false); s.writer=j.at("writerId").get<uint64_t>();
        s.build=j.at("build").get<std::array<int,3>>(); s.dangerSeconds=j.value("dangerSeconds",0.0); s.evasionSeconds=j.value("evasionSeconds",0.0);
        s.finished=j.value("finished",false);
        s.initialState=j.value("initialState",nlohmann::json());
        if (j.contains("replayContext")) s.replay=ReplayFromJson(j["replayContext"]);
        return s;
    }
    void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
        // Write a sibling first; preserve the previous snapshot until it is complete.
        auto temp = path; temp += ".tmp";
        { std::ofstream out(temp,std::ios::binary|std::ios::trunc); out << value.dump(2); out.flush(); if (!out) throw std::runtime_error("AI history write failed"); }
        std::error_code ec; auto old=path; old += ".previous";
        if (std::filesystem::exists(path)) { std::filesystem::remove(old,ec); std::filesystem::rename(path,old); }
        std::filesystem::rename(temp,path); std::filesystem::remove(old,ec);
    }
    void Rebuild() {
        const int64_t now=Now();
        std::unordered_map<uint64_t,AIGameStrategy> result, demoResult;
        // Newest fifteen human games per scene/player; demos cannot train the
        // player's challenge profile. Three-day recall is calculated independently.
        std::sort(sessions.begin(),sessions.end(),[](const Session& a,const Session& b){return a.end>b.end;});
        for (const Session& s:sessions) {
            if (!Valid(s.scene,s.player) || s.end < now-Retention || s.frames == 0 || !s.traceComplete || !s.finished) continue;
            auto& r=(s.demo ? demoResult : result)[Key(s.scene,s.player)]; r.ready=true;
            if (s.end >= now-3LL*24*60*60*1000) { ++r.recentSessions; r.recentScore=(std::max)(r.recentScore,s.score); }
            if (r.sessions >= 15 || s.seconds < 1.0) continue;
            const float n=static_cast<float>(r.sessions), inv=1.0f/(n+1.0f);
            const float stuck=(std::clamp)(static_cast<float>(s.stuckSeconds/s.seconds),0.0f,1.0f);
            const float moving=static_cast<float>(s.moving/s.seconds), firing=static_cast<float>(s.firing/s.seconds);
            const float consistency=(std::clamp)(1.0f-static_cast<float>(s.turns/(s.frames+1)),0.0f,1.0f);
            const float startingHealth=s.initialState.is_object() ? s.initialState.value("health",-1.0f) : -1.0f;
            // Health pools differ across games. Starting facts put losses on a
            // percentage basis before they influence avoidance and aggression.
            const float healthScale=startingHealth>0.0f ? 100.0f/startingHealth : 1.0f;
            const float hurtRate=(std::min)(.25f,static_cast<float>(s.healthLost/(s.seconds+1.0))*healthScale*.02f);
            const float lossPressure=(std::min)(.12f,static_cast<float>(60.0*s.livesLost/(s.seconds+1.0))*.04f);
            const float target=(std::clamp)(0.35f+moving*.12f+firing*.18f+(std::min)(.04f,s.bossDefeats*.01f)-lossPressure-hurtRate*.2f,0.3f,0.7f);
            r.aggression=(r.aggression*n+target)*inv;
            r.movementX=(r.movementX*n+static_cast<float>(s.mx/s.seconds))*inv;
            r.movementY=(r.movementY*n+static_cast<float>(s.my/s.seconds))*inv;
            r.predictionSeconds=(r.predictionSeconds*n+consistency*.20f)*inv;
            r.preferredRange=(r.preferredRange*n+12.0f+firing*6.0f+stuck*4.0f)*inv;
            if (s.boardMoves) { r.boardCaptureRate=(r.boardCaptureRate*n+static_cast<float>(s.boardCaptures)/s.boardMoves)*inv; r.boardCentreRate=(r.boardCentreRate*n+static_cast<float>(s.boardCentres)/s.boardMoves)*inv; }
            r.avoidance=(std::clamp)((r.avoidance*n+.45f+moving*.3f+stuck*.2f+hurtRate)*inv,.3f,.98f); ++r.sessions;
        }
        nlohmann::json lookup={{"schema",1},{"listener","CPGE-AI-LISTENER"},{"retentionHours",504},{"recallHours",72},{"lastGames",15},{"bossStartStrength",0.15},{"bossFullStrengthWave",175},{"jumpmanDefaultThreatPressure",0.15},
            {"routes",{{"strategy","RequestGameStrategy(scene,player)"},{"recovery","RequestGameRecovery / PollGameRecovery"},{"initialReview","initial-state.json / lookup.json quickReview"},{"direct","SubmitExternalGameCommand(scene,command,value,sequence,player)"},
                       {"result","RecordGameCommandResult(sequence,applied)"},{"reset","NotifyGameReset()"},{"boardStrategy","RequestBoardStrategy"},{"boardOrder","OrderBoardMoves"},
                       {"boardSuggestion","AcknowledgeBoardSuggestion"},{"boardCommit","RecordBoardMove"},{"boardStatus","GetBoardStatus"},
                       {"planning","PlanNavigationStep"},{"demoGoal","PlanDemoGoal"},{"demoSeparation","PlanDemoSeparation"},{"demoView","DemoViewMode"},{"demoPerformance","ObserveDemoPerformance"},{"demoTimeLimit","DemoTimeLimit"},{"boundary","NavigationStepAllowed"},{"encounter","RecordEncounterDefeat"},{"threatBudget","SelectThreatBudget"}}},
            {"commands",{"pause","resume","restart","aggression"}},{"capabilities",{"session-history","startup-recall","restart-countdown","movement-planning","demo-contact-separation","demo-goal-outcome-feedback","demo-camera-choice","bounded-prediction","blast-falloff","boss-area-no-respawn","1000-floor-campaigns","jumpman-threat-budget","slow-tracked-tanks","supply-map-markers","outdoor-border-keepaway","lava-shore-avoidance","input-and-pointer-telemetry","chess-strategy-exchange","board-suggestion-acknowledgement","legal-move-commit-trace"}},
            {"recordingSchema",2},{"requiredHumanAndDemoRecording",true},{"skippedUnverifiedRecordings",unverifiedRecordings},{"profiles",nlohmann::json::array()},{"demoProfiles",nlohmann::json::array()}};
        for (const auto& entry:result) { const auto& r=entry.second; lookup["profiles"].push_back({{"scene",static_cast<uint32_t>(entry.first)},{"player",entry.first>>32},{"games",r.sessions},{"last72hGames",r.recentSessions},
            {"bestRecentScore",r.recentScore},{"aggression",r.aggression},{"leadSeconds",r.predictionSeconds},{"preferredRange",r.preferredRange},{"avoidance",r.avoidance},{"boardCaptureRate",r.boardCaptureRate},{"boardCentreRate",r.boardCentreRate}}); }
        for (const auto& entry:demoResult) { const auto& r=entry.second; lookup["demoProfiles"].push_back({{"scene",static_cast<uint32_t>(entry.first)},{"player",entry.first>>32},{"practiceGames",r.sessions},{"avoidance",r.avoidance},{"preferredRange",r.preferredRange}}); }
        // The most recent verified run per game/player/source supplies context
        // without making the game thread read journals or traverse old sessions.
        lookup["quickReview"]=nlohmann::json::array();
        std::unordered_set<uint64_t> reviewedHuman, reviewedDemo;
        for (const Session& s:sessions) {
            if (!Valid(s.scene,s.player) || s.end<now-Retention || !s.finished || !s.traceComplete || s.frames==0) continue;
            auto& seen=s.demo ? reviewedDemo : reviewedHuman;
            if (!seen.insert(Key(s.scene,s.player)).second) continue;
            const double duration=(std::max)(s.seconds,0.001);
            lookup["quickReview"].push_back({{"scene",s.scene},{"gameId",GameId(s.scene)},{"player",s.player},{"source",s.demo?"demo":"human"},
                {"session",s.id},{"initialState",s.initialState},{"score",s.score},{"seconds",s.seconds},
                {"movingFraction",s.moving/duration},{"firingFraction",s.firing/duration},{"idleFraction",s.idleSeconds/duration},
                {"healthLost",s.healthLost},{"evasionFraction",s.dangerSeconds>0?s.evasionSeconds/s.dangerSeconds:0.0}});
        }
        lookup["initialStateReview"]=true;
        lookup["demoCoreStrategyRequired"]=true;
        WriteJson(directory/"lookup.json",lookup);
        { std::lock_guard<std::mutex> lock(cacheMutex); strategies.swap(result); demoStrategies.swap(demoResult); }
        ready.store(true);
    }
    void Load() {
        std::filesystem::create_directories(directory/"sessions");
        auto recover = [](const std::filesystem::path& folder) {
            for (const auto& f:std::filesystem::directory_iterator(folder))
            {
                std::string name=f.path().filename().string();
                if (f.path().extension()!=".previous") continue;
                if (name.rfind("game-",0)!=0 && name.rfind("updates-",0)!=0 && name.rfind("active.json",0)!=0
                    && name.rfind("listener-state.json",0)!=0 && name.rfind("lookup.json",0)!=0 && name.rfind("acknowledgement.json",0)!=0 && name.rfind("board-advice.json",0)!=0 && name.rfind("replay-context.json",0)!=0) continue;
                auto target=f.path(); target.replace_extension(); std::error_code ec;
                if (std::filesystem::exists(target)) std::filesystem::remove(f.path(),ec);
                else std::filesystem::rename(f.path(),target,ec);
            }
        };
        recover(directory); recover(directory/"sessions");

        for (const auto& file:std::filesystem::directory_iterator(directory/"sessions")) {
            if (file.path().extension()!=".json" || file.path().filename().string().rfind("game-",0)!=0) continue;
            try { std::ifstream in(file.path()); nlohmann::json j; in>>j; if (!Compatible(j)) { ++unverifiedRecordings; continue; } Session s=Decode(j); if (s.end >= Now()-Retention) sessions.push_back(s); }
            catch (...) { healthy.store(false); }
        }
        // An interrupted process has no end event. Recover its last checkpoint as
        // an aborted session instead of silently treating it as a successful run.
        auto checkpoint=directory/"active.json";
        if (std::filesystem::exists(checkpoint)) {
            try { std::ifstream in(checkpoint); nlohmann::json j; in>>j; auto s=Decode(j); s.traceComplete=false; s.end=(std::max)(s.start,s.end);
                if (s.end>=Now()-Retention) { WriteJson(directory/"sessions"/(Name(s)+".json"),Summary(s,true,static_cast<int>(AIGameEnd::Abort))); sessions.push_back(s); } }
            catch (...) { healthy.store(false); }
            std::error_code ec; std::filesystem::remove(checkpoint,ec);
        }
        // The newest presentation context per scene/source from earlier runs. Read here on the worker; the game thread only ever copies it out under cacheMutex.
        try {
            std::ifstream in(directory/"replay-context.json"); nlohmann::json j; if (in) in>>j;
            if (Compatible(j) && j.contains("contexts") && j["contexts"].is_object()) {
                replayFile=j["contexts"]; std::lock_guard<std::mutex> lock(cacheMutex);
                for (int sc=0; sc<64; ++sc) for (int src=0; src<2; ++src) {
                    const std::string key=std::to_string(sc)+(src?"-demo":"-human");
                    if (replayFile.contains(key)) persisted[sc][src]=ReplayFromJson(replayFile[key]);
                }
            }
        } catch (...) { replayFile=nlohmann::json::object(); }
        auto state=directory/"listener-state.json";
        if (std::filesystem::exists(state)) { try { std::ifstream in(state); nlohmann::json j; in>>j; lastExternal=j.value("lastSequence",uint64_t(0)); } catch (...) { healthy.store(false); } }
        Rebuild(); Event e; e.kind=EngineStart; Push(e);
    }
    void Prune() {
        const int64_t cutoff=Now()-Retention;
        std::error_code ec;
        for (const auto& file:std::filesystem::directory_iterator(directory)) {
            const std::string name=file.path().filename().string();
            if (name.rfind("updates-",0)!=0 || file.path().extension()!=".jsonl") continue;
            int64_t hour=0; try { hour=std::stoll(name.substr(8)); } catch (...) { continue; }
            if ((hour+1)*3600000LL <= cutoff) std::filesystem::remove(file.path(),ec);
            else if (hour*3600000LL < cutoff) {
                // Trim the boundary hour to the exact rolling 21 x 24-hour cutoff.
                auto temp=file.path(); temp += ".tmp"; std::ifstream in(file.path()); std::ofstream out(temp); std::string line;
                while (std::getline(in,line)) { try { auto j=nlohmann::json::parse(line); if (j.at("utcMs").get<int64_t>() >= cutoff) out<<line<<'\n'; } catch (...) { healthy.store(false); } }
                in.close(); out.close(); if (!out) throw std::runtime_error("AI retention write failed");
                auto old=file.path(); old += ".previous"; std::filesystem::rename(file.path(),old); std::filesystem::rename(temp,file.path()); std::filesystem::remove(old,ec);
            }
        }
        sessions.erase(std::remove_if(sessions.begin(),sessions.end(),[&](const Session& s){
            if (s.end >= cutoff) return false; std::filesystem::remove(directory/"sessions"/(Name(s)+".json"),ec); return true;
        }),sessions.end());
        // Also remove expired summaries that were excluded during startup loading.
        for (const auto& file:std::filesystem::directory_iterator(directory/"sessions")) {
            if (file.path().extension()!=".json" || file.path().filename().string().rfind("game-",0)!=0) continue;
            try { std::ifstream in(file.path()); nlohmann::json j; in>>j; in.close(); if (j.at("endUtcMs").get<int64_t>() < cutoff) std::filesystem::remove(file.path(),ec); } catch (...) { healthy.store(false); }
        }
        Rebuild(); lastPrune=Now();
    }
    void Process(const Event& e) {
        const int64_t hour=e.utc/3600000LL;
        if (hour != journalHour) {
            journal.close(); journal.open(directory/("updates-"+std::to_string(hour)+"-v2.jsonl"),std::ios::app|std::ios::binary); journalHour=hour;
            auto header=Identity("journal-header",-1,0,false);
            header["bootUtcMs"]=bootUtc; header["utcMs"]=e.utc; header["writerId"]=writer; header["frameLayout"]=FrameLayout();
            header["buttonBits"]={{"fire",1},{"alternate",2},{"start",4},{"next",8},{"previous",16},{"padConnected",32},{"X",64},{"Y",128}};
            header["actionKeys"]={"R","G","V","B","I","H","Z","X","M","TAB","UP","DOWN","LEFT","RIGHT","W","A","S","D","SHIFT","CONTROL","1","2","3","4","5","6","7","8","9","0","Q","E","U","N","L","F","T","K","P","HOME","END","PAGE_UP","PAGE_DOWN","BACKSPACE","[","]"};
            Seal(header); journal<<header.dump()<<'\n';
        }
        const auto& f=e.frame;
        auto row=Identity("event",f.scene,f.player,f.demo);
        row["writerId"]=writer;
        row.update({{"bootUtcMs",bootUtc},{"eventOrder",e.order},{"recordingDrops",e.drops},{"utcMs",e.utc},{"sequence",e.sequence},{"session",e.session},{"kind",static_cast<int>(e.kind)}});
        if (e.kind==Update) row["frame"]={f.dt,f.score,f.lives,f.wave,f.mode,f.x,f.y,f.moveX,f.moveY,f.aimX,f.aimY,f.buttons,f.playing,f.hasPosition,f.over,f.complete,f.floor,f.area,f.height,f.health,f.armour,f.weapon,f.ammo,f.hostiles,f.cursorX,f.cursorY,f.actions,f.playback,f.danger,f.carryMass,f.carryCapacity,f.inventoryCount,f.inventoryOpen};
        // Presentation context is journalled only when it changed (camera/music drift is thresholded), so ordinary ticks stay compact. The newest copy per scene/source is also
        // kept in replay-context.json for the next run.
        bool replayChanged=false;
        if (e.kind==Update && f.replay.valid && working.id==e.session && !SameReplay(working.replay,f.replay)) {
            replayChanged=true; working.replay=f.replay; row["replay"]=ReplayJson(f.replay);
        }
        if (e.kind==Update) row["idleInput"]=f.moveX*f.moveX+f.moveY*f.moveY<0.0001f && f.aimX*f.aimX+f.aimY*f.aimY<0.0001f && (f.buttons&~32u)==0 && f.actions==0;
        if (e.kind==Request) row["strategy"]={{"aggression",e.policy[0]},{"avoidance",e.policy[1]},{"predictionSeconds",e.policy[2]},{"preferredRange",e.policy[3]},{"learnedSessions",e.learnedSessions}};
        if (e.kind==Update && working.id==e.session) {
            if (working.haveCursor && std::abs(f.cursorX-working.cursorX)+std::abs(f.cursorY-working.cursorY)>0.0001f) row["idleInput"]=false;
            if (working.inventoryCount>=0 && f.inventoryCount>=0 && (working.inventoryCount!=f.inventoryCount
                || std::abs(working.carryMass-f.carryMass)>0.001f || working.inventoryOpen!=f.inventoryOpen))
                row["inventoryChange"]={{"beforeCount",working.inventoryCount},{"count",f.inventoryCount},{"beforeMass",working.carryMass},{"mass",f.carryMass},{"capacity",f.carryCapacity},{"open",f.inventoryOpen},{"playback",f.playback}};
        }
        if (e.kind==BossDefeat) { row["area"]=e.area; row["enemyKind"]=static_cast<int>(e.value); row["noRespawnThisRun"]=true; }
        if (e.kind==Decision) { row["stage"]=e.value; row["continue"]=e.accepted; }
        if (e.kind==End || e.kind==Decision) row["reason"]=static_cast<int>(e.reason);
        if (e.kind==Request && e.area) row["nativeCommand"]=e.area;
        if (e.kind==Request || e.kind==Command || e.kind==Result) { row["command"]=static_cast<int>(e.command); row["value"]=e.value; row["accepted"]=e.accepted; row["direct"]=e.direct; }
        if (e.kind==BoardRequest || e.kind==BoardSuggestion || e.kind==BoardMove)
        {
            const auto& b=e.board;
            row["board"]={{"request",b.plan.request},{"position",b.plan.position},{"observedPosition",b.observedPosition},{"resultingPosition",b.resultingPosition},{"iq",b.plan.iq},
                {"moveNumber",b.plan.moveNumber},{"purpose",b.plan.purpose},{"goal",static_cast<int>(b.plan.goal)},{"acknowledged",b.plan.acknowledged},
                {"from",b.move.from},{"to",b.move.to},{"promotion",b.move.promotion},{"piece",b.move.piece},{"flags",b.move.flags},
                {"score",b.score},{"completedDepth",b.depth},{"suggestionReady",b.ready},{"accepted",b.accepted},{"applied",b.applied},{"human",b.human}};
        }
        if (e.kind==Playback) row["playback"]={{"id",e.area},{"seconds",e.value}};
        if (e.kind==InventoryDirective) row["directive"]={{"operation",static_cast<int>(e.value)},{"item",static_cast<int64_t>(e.area)-1},{"automatic",e.direct}};
        if (e.kind==InventoryResult) row["inventoryResult"]={{"applied",e.accepted},{"mass",e.policy[0]},{"health",e.policy[1]}};
        if (e.kind==RecoveryRequest || e.kind==RecoveryResult)
            row["recovery"]={{"revision",e.area},{"startX",e.policy[0]},{"startY",e.policy[1]},{"x",e.policy[2]},{"y",e.policy[3]},{"found",e.accepted}};
        Seal(row);
        journal<<row.dump()<<'\n'; if (!journal) throw std::runtime_error("AI journal write failed");
        // A failed batch flush is rewritten with the same event identity. Apply
        // counters and command side effects once, even if its journal row repeats.
        if (e.order<=lastAppliedEvent) return;
        if (e.kind==InventoryDirective) WriteJson(directory/"action-directive.json",row);
        if (e.kind==InventoryResult) WriteJson(directory/"action-result.json",row);
        if (e.kind==RecoveryRequest || e.kind==RecoveryResult) WriteJson(directory/"recovery-state.json",row);
        if (replayChanged && Valid(f.scene,f.player)) {
            auto entry=ReplayJson(f.replay); entry["scene"]=f.scene; entry["source"]=f.demo?"demo":"human"; entry["session"]=e.session; entry["utcMs"]=e.utc;
            replayFile[std::to_string(f.scene)+(f.demo?"-demo":"-human")]=entry;
            auto doc=Identity("replay-context",f.scene,f.player,f.demo); doc["writerId"]=writer; doc["contexts"]=replayFile; Seal(doc);
            WriteJson(directory/"replay-context.json",doc);
        }
        if (row.contains("inventoryChange")) WriteJson(directory/"inventory-state.json",row);
        if (e.kind==BoardRequest || e.kind==BoardSuggestion || e.kind==BoardMove) WriteJson(directory/"board-advice.json",row);
        if (e.kind==BoardMove && working.id==e.session && e.board.applied && e.board.move.from>=0 && (e.board.human || e.frame.demo))
        { ++working.boardMoves; if (e.board.move.flags&1u) ++working.boardCaptures; if (e.board.move.centre>.6f) ++working.boardCentres; }
        if (e.kind==BossDefeat && working.id==e.session) ++working.bossDefeats;
        if (e.kind==Begin) { working=Session(); working.id=e.session; working.scene=f.scene; working.player=f.player; working.demo=f.demo; working.start=e.utc; working.playerName=e.playerName; working.firstOrder=working.lastOrder=e.order; working.startDrops=e.drops; working.writer=writer; }
        if (e.kind==Update && working.id==e.session) {
            working.score=f.score; working.wave=f.wave; working.end=e.utc; ++working.observations; working.lastOrder=e.order;
            if (f.playback) working.playback=f.playback;
            working.traceComplete=working.traceComplete && e.drops==working.startDrops;
            working.cursorX=f.cursorX; working.cursorY=f.cursorY; working.haveCursor=true;
            working.inventoryCount=f.inventoryCount; working.carryMass=f.carryMass; working.inventoryOpen=f.inventoryOpen;
            if (f.health>=0) { if (working.previousHealth>=0) working.healthLost+=(std::max)(0.0f,working.previousHealth-f.health); working.previousHealth=f.health; }
            if (working.previousLives >= 0 && f.lives < working.previousLives) working.livesLost+=static_cast<uint32_t>(working.previousLives-f.lives);
            working.previousLives=f.lives;
            if (f.playing && f.dt>0 && !f.over && !f.complete) {
                if (working.initialState.is_null()) {
                    // Capture actual starting gameplay facts, not Begin's empty
                    // placeholder. File work stays on the existing AI worker.
                    working.initialState={{"eventOrder",e.order},{"utcMs",e.utc},{"mode",f.mode},{"wave",f.wave},{"floor",f.floor},{"area",f.area},
                        {"hasPosition",f.hasPosition},{"x",f.x},{"y",f.y},{"height",f.height},{"score",f.score},{"lives",f.lives},
                        {"health",f.health},{"armour",f.armour},{"weapon",f.weapon},{"ammo",f.ammo},{"hostiles",f.hostiles},
                        {"carryMass",f.carryMass},{"carryCapacity",f.carryCapacity},{"inventoryCount",f.inventoryCount}};
                    auto initial=Identity("initial-state",f.scene,f.player,f.demo);
                    initial["session"]=e.session; initial["writerId"]=writer; initial["state"]=working.initialState;
                    Seal(initial); WriteJson(directory/"initial-state.json",initial);
                }
                const double dt=(std::clamp)(f.dt,0.0f,0.25f); ++working.frames; working.seconds+=dt;
                if (row["idleInput"].get<bool>()) working.idleSeconds+=dt;
                const float observedX=f.x-working.px, observedY=f.y-working.py;
                const bool moving=f.moveX*f.moveX+f.moveY*f.moveY>.04f || (f.hasPosition && working.havePosition && observedX*observedX+observedY*observedY>.0001f);
                if (moving) working.moving+=dt;
                if (f.danger>0.0f) { working.dangerSeconds+=dt; if (moving) working.evasionSeconds+=dt; }
                if (f.buttons&1u) working.firing+=dt;
                working.mx+=f.moveX*dt; working.my+=f.moveY*dt;
                if (f.hasPosition)
                {
                    const float dx=f.x-working.px,dy=f.y-working.py;
                    if (working.havePosition && f.moveX*f.moveX+f.moveY*f.moveY>.04f && dx*dx+dy*dy<.0001f) working.stuckSeconds+=dt;
                    working.px=f.x; working.py=f.y; working.havePosition=true;
                }

                if (working.previousX*f.moveX+working.previousY*f.moveY < -.1f) ++working.turns;
                working.previousX=f.moveX; working.previousY=f.moveY;
            }
        }
        if (e.kind==End && working.id==e.session) {
            working.finished=true;
            working.end=e.utc; working.lastOrder=e.order; working.traceComplete=working.traceComplete && dropped.load()==working.startDrops; WriteJson(directory/"sessions"/(Name(working)+".json"),Summary(working,true,static_cast<int>(e.reason)));
            sessions.push_back(working); working=Session(); std::error_code ec; std::filesystem::remove(directory/"active.json",ec); Rebuild();
        }
        if (e.kind==Playback && e.area && e.session) {
            const auto video=directory.parent_path()/"Playbacks"/(std::to_string(e.area)+".mp4");
            std::error_code error;
            const auto bytes=std::filesystem::file_size(video,error);
            // A result may have been evicted before this worker batch. Never
            // recreate a sidecar for a discarded or failed video capture.
            if (!error && bytes>0) {
                auto manifest=Identity("playback-manifest",f.scene,f.player,f.demo);
                manifest["writerId"]=writer; manifest["session"]=e.session; manifest["id"]=e.area;
                manifest["video"]=video.filename().string(); manifest["videoBytes"]=bytes;
                manifest["seconds"]=e.value; manifest["score"]=f.score; manifest["wave"]=f.wave; manifest["fps"]=30;
                Seal(manifest); auto sidecar=video; sidecar.replace_extension(".json"); WriteJson(sidecar,manifest);
                if (!std::filesystem::exists(video,error)) std::filesystem::remove(sidecar,error);
            }
        }
        if (e.kind==Command && e.accepted && e.sequence>lastForwarded)
        {
            // Do not expose a direct command to the engine before its acceptance
            // has actually reached the journal. The render thread never waits for IO.
            journal.flush(); if (!journal) throw std::runtime_error("AI command audit failed");
            WriteJson(directory/"listener-state.json",{{"lastSequence",e.sequence}});
            std::lock_guard<std::mutex> lock(queueMutex);
            commands[(commandHead+commandSize)%commands.size()] = {f.scene,f.player,e.command,(std::clamp)(e.value,0.0f,1.0f),e.sequence};
            ++commandSize; lastForwarded=e.sequence; if (awaitingAudit) --awaitingAudit;
        }
        if (e.kind==Command || e.kind==Result) WriteJson(directory/"acknowledgement.json",row);
        lastAppliedEvent=e.order;
    }
    void Pump() {
        // One bounded batch, then return to the pre-existing model/command worker.
        if (!retryCount)
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            retryCount=(std::min)(size,retryBatch.size()); retryOffset=0;
            for(size_t i=0;i<retryCount;++i) retryBatch[i]=queue[(head+i)%Capacity];
            head=(head+retryCount)%Capacity; size-=retryCount;
        }
        for (;retryOffset<retryCount;++retryOffset) Process(retryBatch[retryOffset]);
        if (journal.is_open()) {
            journal.flush();
            if (!journal) { retryOffset=0; throw std::runtime_error("AI journal flush failed"); }
        }
        retryCount=retryOffset=0;
        ProcessRecovery();
        if (working.id && Now()-lastCheckpoint>=1000) { working.traceComplete=working.traceComplete && dropped.load()==working.startDrops; WriteJson(directory/"active.json",Summary(working,false,0)); lastCheckpoint=Now(); }
        if (Now()-lastPrune>=60000) Prune();
        #if defined(DEFINE_EXT_CMD_SEQUENCE)
            const auto inbox=directory/"command.json";
            if (std::filesystem::exists(inbox)) {
                try { std::ifstream in(inbox); nlohmann::json j; in>>j;
                    const int command=j.at("command").get<int>();
                    const int64_t expiry=j.at("expiresUtcMs").get<int64_t>();
                    const auto type=expiry>=Now() && expiry<=Now()+60000 ? (command>=1 && command<=4 ? static_cast<AIGameCommand>(command) : AIGameCommand::None) : AIGameCommand::None;
                    Submit(j.at("scene").get<int>(),type,j.value("value",0.0f),j.at("sequence").get<uint64_t>(),j.value("player",1));
                } catch (...) { Event e; e.kind=Command; Push(e); }
                std::error_code ec; std::filesystem::remove(inbox,ec);
            }
        #endif
    }
};

uint64_t GamingAI::RecordInventoryDirective(int scene,AIInventoryOperation operation,int item,bool automatic) {
    if (!m_isInitialized.load() || m_shouldShutdown.load() || operation==AIInventoryOperation::None) return 0;
    auto& history=*m_gameHistory; std::lock_guard<std::mutex> lock(history.lifecycleMutex);
    auto event=history.Make(GameHistory::InventoryDirective); event.frame.scene=scene;
    event.sequence=history.nextSequence.fetch_add(1); event.value=static_cast<float>(operation);
    event.area=static_cast<uint64_t>(static_cast<int64_t>(item)+1); event.direct=automatic;
    return history.Push(event) ? event.sequence : 0;
}
void GamingAI::RecordInventoryResult(uint64_t sequence,bool applied,float mass,float health) {
    if (!sequence || !m_isInitialized.load() || m_shouldShutdown.load()) return;
    auto& history=*m_gameHistory; std::lock_guard<std::mutex> lock(history.lifecycleMutex);
    auto event=history.Make(GameHistory::InventoryResult); event.sequence=sequence;
    event.accepted=applied; event.policy={mass,health,0,0}; history.Push(event);
}
AIInventoryDecision GamingAI::PlanInventory(AIInventoryMemory& memory,const AIInventoryState& state,const AIInventoryItem* items,int count,float dt) {
    AIInventoryDecision result;
    memory.wait=(std::max)(0.0f,memory.wait-(std::clamp)(dt,0.0f,0.25f));
    if (!state.safe) {
        // Instant recovery in games without an inventory window can save a
        // critically hurt actor while movement continues. Never discard here.
        if(state.autonomous && !state.hasWindow && memory.wait<=0) for(int i=0;i<count && i<64;++i)
            if(items[i].kind==AIInventoryKind::Recovery && items[i].usable && items[i].count>0 && items[i].need>=0.5f) {
                result.operation=AIInventoryOperation::Use; result.item=items[i].id; memory.wait=0.55f; return result;
            }
        if (state.open && state.autonomous) result.operation=AIInventoryOperation::Close; return result;
    }
    if (memory.wait>0) return result;
    const bool pressure=state.full || (state.capacity>0 && state.mass+state.requiredSpace>state.capacity);
    if (!pressure) memory.prompted=false;
    if (!state.autonomous) {
        if (pressure && !state.open && state.hasWindow && !memory.prompted) {
            result.operation=AIInventoryOperation::Open; memory.prompted=true; memory.wait=1.0f;
        }
        return result;
    }
    if (pressure && !state.open && state.hasWindow) { memory.wait=0.8f; result.operation=AIInventoryOperation::Open; return result; }
    if (!pressure && !state.open && state.hasWindow) return result;
    float benefit=0; int best=-1;
    for (int i=0;i<count && i<64;++i) {
        const auto& item=items[i]; if (item.count<=0 || !item.usable) continue;
        float score=0;
        if (item.kind==AIInventoryKind::Equipment && item.quality>item.equippedQuality) score=10.0f+(item.quality-item.equippedQuality)*3.0f;
        if (item.kind==AIInventoryKind::Recovery && (item.need>=(state.hasWindow?0.2f:0.5f) || (pressure && item.need>0.01f))) score=item.need*30.0f;
        if (score>benefit) { benefit=score; best=i; }
    }
    if (best>=0) {
        result.operation=items[best].kind==AIInventoryKind::Equipment ? AIInventoryOperation::Equip : AIInventoryOperation::Use;
        result.item=items[best].id; memory.wait=0.55f; return result;
    }
    float discard=1.0e30f; best=-1;
    int weapons=0; for (int i=0;i<count && i<64;++i) if (items[i].kind==AIInventoryKind::Weapon && items[i].count>0) ++weapons;
    if (pressure) for (int i=0;i<count && i<64;++i) {
        const auto& item=items[i];
        if (item.count<=0 || item.weight<=0 || item.essential || item.kind==AIInventoryKind::Key) continue;
        if ((item.kind==AIInventoryKind::Recovery && item.count<=1) || (item.kind==AIInventoryKind::Resource && item.count<=2)
            || (item.kind==AIInventoryKind::Ammo && item.count<=10) || (item.kind==AIInventoryKind::Weapon && weapons<=2)) continue;
        const float value=(item.quality+1.0f)/item.weight;
        if (value<discard) { discard=value; best=i; }
    }
    if (best>=0) {
        const auto& item=items[best]; result.operation=AIInventoryOperation::Drop; result.item=item.id;
        const int keep=item.kind==AIInventoryKind::Ammo ? 10 : (item.kind==AIInventoryKind::Recovery ? 1 : (item.kind==AIInventoryKind::Resource ? 2 : 0));
        const float space=(std::max)(0.5f,state.mass+state.requiredSpace-state.capacity+0.25f);
        result.amount=(std::max)(1,(std::min)({128,item.count-keep,static_cast<int>(std::ceil(space/item.weight))}));
        memory.wait=0.55f; return result;
    }
    if (state.open) result.operation=AIInventoryOperation::Close;
    memory.wait=pressure ? 8.0f : 1.0f; return result;
}

bool GamingAI::ValidateGameRecording(const std::filesystem::path& path, int expectedScene) {
    try {
        std::ifstream in(path,std::ios::binary); if (!in) return false;
        if (path.extension()==".jsonl") {
            std::string line; bool header=false;
            while (std::getline(in,line)) {
                if (line.empty()) continue;
                auto row=nlohmann::json::parse(line);
                const bool isHeader=row.value("recordType",std::string())=="journal-header";
                if (!GameHistory::Compatible(row,isHeader ? -1 : expectedScene)) return false;
                if (isHeader) header=true; else if (!header) return false;
            }
            return header && !in.bad();
        }
        if (std::filesystem::file_size(path)>1024*1024) return false;
        nlohmann::json value; in>>value;
        if (!GameHistory::Compatible(value,expectedScene)) return false;
        if (value.at("recordType")=="playback-manifest") {
            const auto id=value.at("id").get<uint64_t>();
            if (!id || !value.at("session").get<uint64_t>() || value.at("video")!=std::to_string(id)+".mp4") return false;
            const auto video=path.parent_path()/(std::to_string(id)+".mp4");
            return value.at("videoBytes").get<uint64_t>()>0 && std::filesystem::file_size(video)==value.at("videoBytes").get<uint64_t>();
        }
        return value.at("recordType")=="session";
    } catch (...) { return false; }
}
uint64_t GamingAI::CurrentGameSession() const {
    std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); return m_gameHistory->session;
}
void GamingAI::RecordPlayback(uint64_t session,uint64_t playback,const AIGameFrame& result,float seconds) {
    if (!session || !playback || !m_isInitialized.load() || m_shouldShutdown.load()) return;
    GameHistory::Event event; event.kind=GameHistory::Playback; event.session=session; event.area=playback;
    event.frame=result; event.frame.playback=playback; event.value=seconds; m_gameHistory->Push(event);
}

void GamingAI::ConfigureGameLearning(bool record, bool recall, bool adapt, bool restart) {
    // Recording human and autonomous play is a required engine service. The
    // legacy setting remains readable for configuration compatibility.
    (void)record; record = true;
    auto& h=*m_gameHistory; std::lock_guard<std::mutex> lifecycle(h.lifecycleMutex);
    const bool wasRecording=h.record;
    if (wasRecording && !record) h.Finish(AIGameEnd::Abort);
    h.record=record; h.recall=recall; h.adapt.store(adapt); h.restartEnabled=restart;
    if (!wasRecording && record && h.scene>=0) { const int keepStage=h.stage; h.Start(h.scene,h.player,h.currentDemo); h.stage=keepStage; }
    if (!restart && h.stage==2) h.stage=0;
}
AIGameFlow GamingAI::AdvanceGameLifecycle(int scene,bool demo,bool reset,float dt,bool confirm,bool cancel,bool allow,int player) {
    if (!m_isInitialized.load() || m_shouldShutdown.load()) return {};
    std::lock_guard<std::mutex> lifecycle(m_gameHistory->lifecycleMutex);
    return m_gameHistory->Advance(scene,demo,reset,dt,confirm,cancel,allow,player);
}
bool GamingAI::GetReplayContext(int scene, bool demo, AIReplayContext& out) const {
    if (scene<0 || scene>=64 || !m_gameHistory) return false;
    auto& h=*m_gameHistory; const int src=demo?1:0;
    { std::lock_guard<std::mutex> lock(h.lifecycleMutex); if (h.live[scene][src].valid) { out=h.live[scene][src]; return true; } }
    std::lock_guard<std::mutex> lock(h.cacheMutex);
    if (!h.persisted[scene][src].valid) return false;
    out=h.persisted[scene][src]; return true;
}
// The core's answer to "the demo has not progressed". Escalation is time based and bounded; the adapter only executes it (cache refresh, a short held input pulse). The probe directions
// walk round a 16-point circle with a stride that is coprime to 16, so successive pulses go to different headings instead of repeating one blocked move.
AIDemoUnstick GamingAI::ObserveDemoStall(AIDemoWatchdog& w, float dt, bool playing, bool hasPosition, float x, float y,
    int score, int lives, int wave, float& dirX, float& dirY) {
    dirX=dirY=0.0f;
    if (!playing || !hasPosition || !(dt>0.0f) || !std::isfinite(x) || !std::isfinite(y)) { w.idle=0.0f; w.hold=0.0f; return AIDemoUnstick::None; }
    dt=(std::min)(dt,0.25f);
    const bool scored=score!=w.score || lives!=w.lives || wave!=w.wave;
    const float moved=std::hypot(x-w.anchorX,y-w.anchorY);
    if (!w.anchored || scored || moved>0.75f) {             // real progress: a new anchor and a clean slate
        w.anchored=true; w.score=score; w.lives=lives; w.wave=wave; w.anchorX=x; w.anchorY=y; w.idle=0.0f; w.hold=0.0f; w.strikes=0;
        return AIDemoUnstick::None;
    }
    if (w.hold>0.0f) { w.hold-=dt; dirX=w.dirX; dirY=w.dirY; return AIDemoUnstick::Probe; }
    w.idle+=dt;
    if (w.strikes==0 && w.idle>=4.0f) { w.strikes=1; return AIDemoUnstick::Refresh; }
    if (w.strikes>=1 && w.idle>=(w.strikes==1 ? 8.0f : 6.0f)) {
        const int index=static_cast<int>((w.strikes*5u+3u)%16u); const float angle=static_cast<float>(index)*0.39269908f;       // 2*pi/16
        w.dirX=std::sin(angle); w.dirY=std::cos(angle); w.hold=0.8f; w.idle=0.0f; ++w.strikes; dirX=w.dirX; dirY=w.dirY;
        return AIDemoUnstick::Probe;
    }
    return AIDemoUnstick::None;
}
float GamingAI::BoardSearchDeadline(int timeLimitSeconds, bool demo, float unlimitedSeconds) {
    // Configured searches keep their limit plus slack. The attract demo must never sit visibly undecided: unlimited searches end after at most 8 s and the adapter plays a legal fallback.
    const float configured=timeLimitSeconds>0 ? static_cast<float>(timeLimitSeconds)+3.0f : unlimitedSeconds;
    return (demo && timeLimitSeconds<=0) ? (std::min)(configured,8.0f) : configured;
}
void GamingAI::RecordGameUpdate(const AIGameFrame& frame) { if (!m_isInitialized.load() || m_shouldShutdown.load()) return; std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); m_gameHistory->Observe(frame); }
void GamingAI::NotifyGameReset() { auto& h=*m_gameHistory; std::lock_guard<std::mutex> lock(h.lifecycleMutex); if (h.scene>=0) { if (!h.resetExpected) h.Start(h.scene,h.player,h.currentDemo); h.resetExpected=false; h.ClearDefeats(); h.Push(h.Make(GameHistory::Reset)); } }
void GamingAI::EndGameSession(AIGameEnd reason) { std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); m_gameHistory->Finish(reason); }
AIListenerReply GamingAI::RequestGameStrategy(int scene,int player,bool demo) {
    AIListenerReply reply; if (!m_isInitialized.load() || m_shouldShutdown.load()) return reply; reply.strategy=m_gameHistory->Strategy(scene,player,demo);
    GameHistory::Event e; e.kind=GameHistory::Request; e.frame.scene=scene; e.frame.player=player; e.frame.demo=demo;
    reply.sequence=m_gameHistory->nextSequence.fetch_add(1); e.sequence=reply.sequence; e.accepted=true; e.direct=reply.strategy.sessions==0;
    e.policy={reply.strategy.aggression,reply.strategy.avoidance,reply.strategy.predictionSeconds,reply.strategy.preferredRange}; e.learnedSessions=reply.strategy.sessions;
    reply.acknowledged=m_gameHistory->Push(e); reply.direct=reply.strategy.sessions==0;
    return reply;
}
bool GamingAI::SubmitExternalGameCommand(int scene,AIGameCommand command,float value,uint64_t sequence,int player) { return m_gameHistory->Submit(scene,command,value,sequence,player); }
uint64_t GamingAI::RequestGameRecovery(int scene,uint64_t revision,float x,float y,float goalX,float goalY,const AINavigationCandidate* candidates,int count) {
    if (!m_isInitialized.load() || m_shouldShutdown.load() || !candidates || count<=0 || count>16
        || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(goalX) || !std::isfinite(goalY)) return 0;
    auto& h=*m_gameHistory;
    std::unique_lock<std::mutex> lifecycle(h.lifecycleMutex,std::try_to_lock);
    if (!lifecycle || !h.active || !h.currentDemo || h.scene!=scene) return 0;
    std::unique_lock<std::mutex> lock(h.recoveryMutex,std::try_to_lock);
    if (!lock || h.recoveryBusy) return 0;
    AIRecoveryJob job; job.plan.scene=scene; job.plan.player=h.player; job.plan.session=h.session; job.plan.revision=revision;
    job.plan.request=h.nextSequence.fetch_add(1); job.plan.startX=x; job.plan.startY=y;
    job.goalX=goalX; job.goalY=goalY; job.count=count;
    for (int i=0;i<count;++i) {
        const auto& c=candidates[i];
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.danger) || c.danger<0) return 0;
        job.candidates[i]=c;
    }
    auto event=h.Make(GameHistory::RecoveryRequest); event.sequence=job.plan.request;
    event.area=revision; event.policy={x,y,goalX,goalY};
    if (!h.Push(event)) return 0;
    h.recoveryJob=job; h.recoveryBusy=h.recoveryQueued=true; h.recoveryReady=false;
    return job.plan.request;
}
bool GamingAI::PollGameRecovery(int scene,uint64_t revision,AIRecoveryPlan& plan) {
    auto& h=*m_gameHistory;
    std::unique_lock<std::mutex> lifecycle(h.lifecycleMutex,std::try_to_lock);
    if (!lifecycle) return false;
    std::unique_lock<std::mutex> lock(h.recoveryMutex,std::try_to_lock);
    if (!lock || !h.recoveryBusy) return false;
    if (!h.active || !h.currentDemo || h.scene!=scene || h.recoveryJob.plan.session!=h.session || h.recoveryJob.plan.revision!=revision) {
        h.recoveryBusy=h.recoveryQueued=h.recoveryReady=false; return false;
    }
    if (!h.recoveryReady) return false;
    plan=h.recoveryReply; h.recoveryBusy=h.recoveryReady=false; return true;
}
bool GamingAI::PollGameCommand(int scene,AIGameCommand& command,float& value,uint64_t& sequence) {
    auto& h=*m_gameHistory; std::lock_guard<std::mutex> lock(h.queueMutex);
    if (!h.commandSize) return false; auto p=h.commands[h.commandHead]; h.commandHead=(h.commandHead+1)%h.commands.size(); --h.commandSize;
    command=p.command; value=p.value; sequence=p.sequence;
    // A command for a different scene is acknowledged as unapplied, not forwarded.
    if (p.scene!=scene || p.player!=h.player) command=AIGameCommand::None;
    return true;
}
void GamingAI::RecordGameCommandResult(uint64_t sequence,bool applied) { GameHistory::Event e; e.kind=GameHistory::Result; e.sequence=sequence; e.accepted=applied; e.direct=true; m_gameHistory->Push(e); }
bool GamingAI::GameHistoryHealthy() const { return m_gameHistory->healthy.load(); }
float GamingAI::InitialBossStrength(int wave) { return (std::clamp)(0.15f+0.005f*static_cast<float>((std::max)(0,wave-5)),0.15f,1.0f); }
float GamingAI::BossCombatRange(float weaponRange, float bodyRadius, float healthFraction) {
    const float desired = bodyRadius + (healthFraction < 0.45f ? 26.0f : 20.0f);
    return (std::min)(weaponRange * 0.75f, desired);
}
float GamingAI::GroundVehicleSpeed(float baseSpeed, int level) {
    // Heavy hulls stay slow; progression never exceeds their configured speed.
    const float ramp = (std::clamp)(static_cast<float>(level - 1) / 39.0f, 0.0f, 1.0f);
    return (std::max)(0.0f, baseSpeed) * (0.35f + 0.65f * ramp);
}
bool GamingAI::AllowCanyonStep(bool canyon, bool bridge, bool fallback) { return !canyon || bridge || fallback; }
int GamingAI::FindTerrainRoute(int width, int height, int start, int goal, uint64_t revision,
    AITerrainReader reader, const void* context, int* path, int capacity, bool& reached, bool& usedCanyon, float border) {
    constexpr int MaxCells = 512 * 512;
    struct Scratch {
        std::array<int, MaxCells> parent{}, queue{};
        std::array<uint32_t, MaxCells> visited{};
        uint32_t stamp = 0; uint64_t revision = 0;
        int width = 0, height = 0, goal = -1, start = -1, routeN = 0, cursor = 0;
        bool reached = false, canyon = false;
        float border = 4.0f;
        AITerrainReader reader = nullptr;
    };
    // One bounded scratch block per calling thread, with no allocation in the search.
    // The BFS queue becomes the cached route after reconstruction.
    static thread_local Scratch scratch;
    reached = usedCanyon = false;
    if (width <= 0 || height <= 0 || width > 512 || height > 512 || !reader || !path || capacity <= 0
        || start < 0 || goal < 0 || start >= width * height || goal >= width * height) return 0;
    auto emit = [&](int cursor) {
        const int n = (std::min)(capacity, scratch.routeN - cursor);
        for (int i = 0; i < n; ++i) path[i] = scratch.queue[cursor + i];
        reached = scratch.reached; usedCanyon = scratch.canyon; return n;
    };
    if (scratch.revision == revision && scratch.width == width && scratch.height == height && scratch.goal == goal && scratch.border == border && scratch.reader == reader) {
        if (scratch.start == start) return emit(scratch.cursor);
        for (int i = scratch.cursor; i < (std::min)(scratch.routeN, scratch.cursor + 32); ++i)
            if (scratch.queue[i] == start) { scratch.start = start; scratch.cursor = i + 1; return emit(i + 1); }
    }
    int best = start; bool found = false; const bool exactGoal = reader(context, goal).clear;
    for (int pass = 0; pass < 2 && !found; ++pass) {
        if (++scratch.stamp == 0) { scratch.visited.fill(0); scratch.stamp = 1; }
        int head = 0, tail = 0, bestDistance = 0x7fffffff;
        scratch.queue[tail++] = start; scratch.parent[start] = -1; scratch.visited[start] = scratch.stamp;
        while (head < tail) {
            const int cell = scratch.queue[head++], x = cell % width, y = cell / width;
            const int dx = x - goal % width, dy = y - goal / width, distance = dx * dx + dy * dy;
            if (distance < bestDistance) { bestDistance = distance; best = cell; }
            if (distance == 0 || (!exactGoal && distance <= 1)) { found = true; break; }
            const auto from = reader(context, cell);
            const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
            for (int i = 0; i < 4; ++i) {
                if (nx[i] < 0 || ny[i] < 0 || nx[i] >= width || ny[i] >= height) continue;
                const int next = ny[i] * width + nx[i]; if (scratch.visited[next] == scratch.stamp) continue;
                if (!NavigationStepAllowed(x + 0.5f, y + 0.5f, nx[i] + 0.5f, ny[i] + 0.5f, static_cast<float>(width), static_cast<float>(height), border)) continue;
                const auto to = reader(context, next); const float rise = to.height - from.height;
                if (!to.clear || !AllowCanyonStep(to.canyon, to.bridge, pass != 0) || rise > 1.8f || rise < -4.0f) continue;
                scratch.visited[next] = scratch.stamp; scratch.parent[next] = cell; scratch.queue[tail++] = next;
            }
        }
        usedCanyon = pass != 0;
    }
    scratch.routeN = 0;
    for (int cell = best; cell != start && cell >= 0 && scratch.routeN < MaxCells; cell = scratch.parent[cell])
        scratch.queue[scratch.routeN++] = cell;
    std::reverse(scratch.queue.begin(), scratch.queue.begin() + scratch.routeN);
    scratch.revision = revision; scratch.width = width; scratch.height = height; scratch.goal = goal;
    scratch.start = start; scratch.cursor = 0; scratch.reached = found; scratch.canyon = usedCanyon;
    scratch.border = border; scratch.reader = reader;
    return emit(0);
}
void GamingAI::RejectDemoGoal(AIDemoPlan& plan) {
    if (!plan.goal) return;
    const int slot = plan.nextRejected;
    plan.rejected[slot] = plan.goal; plan.retryAt[slot] = plan.time + 8.0f;
    plan.nextRejected = (slot + 1) % static_cast<int>(plan.rejected.size());
    plan.goal = 0; plan.progressTime = 0; plan.bestDistance = 1.0e30f; ++plan.recoveries;
}
bool GamingAI::CanFireFacing(float error) { return std::isfinite(error) && std::fabs(error) <= 8.0f; }
void GamingAI::ObserveDemoPerformance(AIDemoPerformance& state, int score, int wave, int lives, float elapsed)
{
    if (score > state.score || wave > state.wave) state.lastProgress = elapsed;
    if (state.lives >= 0 && lives < state.lives) state.lastLoss = elapsed;
    state.score = score; state.wave = wave; state.lives = lives;
}

float GamingAI::DemoTimeLimit(const AIDemoPerformance& state, float elapsed, float normalLimit)
{
    // Recent progress and survival justify a longer demonstration. A stalled
    // run reverts to the existing limit; no demo can extend beyond five minutes.
    const bool performing = (state.score > 0 || state.wave > 1)
        && elapsed - state.lastProgress <= 30.0f && elapsed - state.lastLoss >= 12.0f;
    return performing ? 300.0f : (std::min)(normalLimit, 300.0f);
}

int GamingAI::DemoViewMode(float elapsed, const AIGameStrategy& strategy)
{
    // Give the viewer a sustained demonstration in each perspective. Strategy
    // changes dwell time modestly; camera choices never change control axes.
    const float dwell = 20.0f + (std::clamp)(strategy.avoidance, 0.0f, 1.0f) * 8.0f;
    return static_cast<int>((std::max)(0.0f, elapsed) / dwell) % 2;
}
int GamingAI::PlanDemoSeparation(AIDemoEscapeMemory& m, const AIDemoContact* contacts,
    int count, const AIDemoEscapeCandidate* candidates, int candidateCount, float dt)
{
    m.hold = (std::max)(0.0f, m.hold - (std::clamp)(dt, 0.0f, 0.1f));
    bool contactNear = false;
    for (int i = 0; contacts && i < count && i < 512; ++i) {
        const auto& c = contacts[i]; const float distance = std::sqrt(c.x*c.x + c.y*c.y + c.z*c.z);
        const float margin = (std::max)(0.35f, c.radius * 0.45f);
        if (distance < c.radius + margin * (m.active ? 1.6f : 1.0f)) { contactNear = true; break; }
    }
    if (!contactNear && (!m.active || m.hold <= 0)) { m.active = false; return -1; }
    if (contactNear && !m.active) m.hold = 0.35f;
    m.active = true;
    int selected = -1; float best = -1.0e30f;
    for (int i = 0; candidates && i < candidateCount && i < 16; ++i) {
        const auto& p = candidates[i]; if (!p.clear) continue;
        const float length2 = p.x*p.x + p.y*p.y + p.z*p.z; if (length2 < 0.0001f) continue;
        float clearance = 1.0e20f, improvement = 1.0e20f;
        for (int j = 0; contacts && j < count && j < 512; ++j) {
            const auto& c = contacts[j]; const float start = std::sqrt(c.x*c.x + c.y*c.y + c.z*c.z);
            const float t = (std::clamp)((c.x*p.x + c.y*p.y + c.z*p.z) / length2, 0.0f, 1.0f);
            const float dx = c.x - p.x*t, dy = c.y - p.y*t, dz = c.z - p.z*t;
            clearance = (std::min)(clearance, std::sqrt(dx*dx + dy*dy + dz*dz) - c.radius);
            if (start < c.radius * 2.5f + 1.0f) {
                const float ex = c.x-p.x, ey = c.y-p.y, ez = c.z-p.z;
                improvement = (std::min)(improvement, std::sqrt(ex*ex + ey*ey + ez*ez) - start);
            }
        }
        // Existing overlaps must shrink first. Do not cross a second enemy's
        // body merely because the chosen endpoint is farther from the first.
        const float score = (std::min)(improvement, 10.0f) * 8.0f + (std::min)(clearance, 10.0f) * 3.0f
            + (p.x*m.x + p.y*m.y + p.z*m.z) * 0.15f;
        if (score > best) { best = score; selected = i; }
    }
    if (selected >= 0) { const auto& p = candidates[selected]; m.x = p.x; m.y = p.y; m.z = p.z; }
    return selected;
}
float GamingAI::GunnerPositionScore(float distance, int cover, int lanes) {
    return lanes * 8.0f + (std::min)(cover, 2) * 4.0f - std::fabs(distance - 28.0f) * 0.2f - (distance < 10.0f ? 60.0f : 0.0f);
}
bool GamingAI::GunnerShouldAdvance(bool visible, float distance, float range, float contactAge) {
    return (visible && distance > range * 0.85f) || (!visible && contactAge > 1.5f);
}
void GamingAI::BuildReachability(int width, int height, int start, AITerrainReader reader,
    const void* context, uint8_t* reachable, int capacity) {
    // Bounded building-floor connectivity shared by placement adapters.
    if (!reachable || !reader || width <= 0 || height <= 0 || width > 64 || height > 64
        || capacity < width * height) return;
    const int cells = width * height; std::fill(reachable, reachable + cells, uint8_t(0));
    if (start < 0 || start >= cells || !reader(context, start).clear) return;
    std::array<int, 4096> queue; int head = 0, tail = 0; queue[tail++] = start; reachable[start] = 1;
    while (head < tail) {
        const int c = queue[head++], x = c % width, y = c / width;
        const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int i = 0; i < 4; ++i) {
            if (nx[i] < 0 || ny[i] < 0 || nx[i] >= width || ny[i] >= height) continue;
            const int n = ny[i] * width + nx[i];
            if (!reachable[n] && reader(context, n).clear) { reachable[n] = 1; queue[tail++] = n; }
        }
    }
}
int GamingAI::PlanDemoGoal(AIDemoPlan& plan, const AIDemoGoal* goals, int count,
    float x, float y, float health, bool needsAmmo, float dt, const AIGameStrategy& strategy) {
    dt = (std::max)(0.0f, (std::min)(dt, 0.25f)); plan.time += dt;
    // Measure the outcome of the preceding action, including lost health. This
    // short-lived pressure changes the next decision without inventing progress.
    plan.damagePressure = (std::max)(0.0f, plan.damagePressure - dt * 0.1f)
        + (std::max)(0.0f, plan.lastHealth - health);
    plan.lastHealth = health;
    for (int i = 0; goals && i < count && i < 2048; ++i) if (goals[i].id == plan.goal) {
        const float distance = std::hypot(goals[i].x - x, goals[i].y - y);
        if (distance < plan.bestDistance - 0.5f) { plan.bestDistance = distance; plan.progressTime = 0; }
        else if (!(goals[i].kind == AIDemoGoalKind::Enemy && goals[i].visible && goals[i].sameArea)
            && !(goals[i].kind == AIDemoGoalKind::Heal && distance < 2.5f)) plan.progressTime += dt;
        if (plan.progressTime >= 6.0f) RejectDemoGoal(plan);
        break;
    }
    int selected = -1; float best = -1.0e30f;
    for (int i = 0; goals && i < count && i < 2048; ++i) {
        const auto& g = goals[i]; if (!g.available || !g.id) continue;
        bool banned = false;
        for (size_t j = 0; j < plan.rejected.size(); ++j)
            if (plan.rejected[j] == g.id && plan.retryAt[j] > plan.time && !g.visible) { banned = true; break; }
        if (banned) continue;
        const float distance = std::hypot(g.x - x, g.y - y);
        float score = -distance * 0.6f;
        switch (g.kind) {
        case AIDemoGoalKind::Enemy: score += g.visible && g.sameArea ? 130.0f : 25.0f; break;
        case AIDemoGoalKind::Supply: score += needsAmmo ? 180.0f : 35.0f; break;
        case AIDemoGoalKind::Heal: score += health < 0.42f ? 220.0f + plan.damagePressure * 50.0f : -250.0f; break;
        case AIDemoGoalKind::Collect: score += needsAmmo ? -200.0f : 40.0f; break;
        case AIDemoGoalKind::Explore: score -= 100.0f; if (distance < 3.0f) score -= 1000.0f; break;
        }
        if (!g.sameArea) score -= 15.0f;
        if (g.id == plan.goal) score += 12.0f + strategy.aggression * 5.0f;
        if (score > best) { best = score; selected = i; }
    }
    if (selected >= 0 && plan.goal != goals[selected].id) {
        plan.goal = goals[selected].id; plan.progressTime = 0;
        plan.bestDistance = std::hypot(goals[selected].x - x, goals[selected].y - y); ++plan.decisions;
    }
    return selected;
}
void GamingAI::PlanGroundVehicle(AIGroundVehicleMemory& memory, const AIGroundVehicleState& state,
    const AINavigationCandidate* candidates, int count, const AICombatThreat* threats, int threatCount) {
    memory.replan = 0.2f; memory.speed = GroundVehicleSpeed(state.baseSpeed, state.level);
    float gx = state.targetX - state.x, gy = state.targetY - state.y;
    const float distance = std::hypot(gx, gy);
    if (distance > 0.001f) { gx /= distance; gy /= distance; }
    const float standOff = state.range * (state.artillery ? 0.65f : 0.40f);
    if (state.visible && distance < standOff * 0.55f) { gx = -gx; gy = -gy; }
    else if (state.visible && distance < standOff) { const float tmp = gx; gx = -gy; gy = tmp; }
    float best = -1.0e30f, leastDanger = 1.0e30f; int selected = -1;
    for (int i = 0; i < count && i < 16; ++i) {
        const auto& c = candidates[i]; if (!c.clear) continue;
        float dx = c.x - state.x, dy = c.y - state.y; const float length = std::hypot(dx, dy);
        if (length > 0.001f) { dx /= length; dy /= length; }
        const float risk = CombatMoveDanger(threats, threatCount, state.x, state.y,
            dx * memory.speed, dy * memory.speed, 1.4f);
        const float score = dx * gx + dy * gy;
        if (risk < leastDanger - 0.001f || (risk <= leastDanger + 0.001f && score > best)) {
            selected = i; leastDanger = risk; best = score;
        }
    }
    memory.targetX = selected >= 0 ? candidates[selected].x : state.x;
    memory.targetY = selected >= 0 ? candidates[selected].y : state.y;
    if (selected < 0) memory.speed = 0.0f;
    if (!state.visible && distance < 1.0f) memory.speed = 0.0f;  // hold the search point until fresh contact
    // Brace only when staying put is safe; avoiding fire outranks a firing stance.
    const float stationaryRisk = CombatMoveDanger(threats, threatCount, state.x, state.y, 0, 0, 1.4f);
    if (state.tracked && state.visible && distance < state.range && state.shotCooldown <= 0.35f
        && stationaryRisk <= 0.001f) memory.speed = 0.0f;
}
bool GamingAI::ShouldUseCombatMedkit(float healthFraction, int medkits) {
    return healthFraction < 0.45f && medkits > 0;
}
float GamingAI::DemoPlanningInterval(const AIGameStrategy& strategy, bool heavyThreat) {
    return (std::clamp)((heavyThreat ? 0.12f : 0.22f) - strategy.avoidance * 0.05f - strategy.predictionSeconds * 0.05f, 0.07f, 0.22f);
}
float GamingAI::DemoPredictionHorizon(const AIGameStrategy& strategy) {
    return (std::clamp)(0.6f + strategy.avoidance * 0.25f + strategy.predictionSeconds, 0.6f, 1.0f);
}
float GamingAI::CombatMoveDanger(const AICombatThreat* threats, int count, float x, float y, float vx, float vy, float horizon) {
    float danger = 0.0f;
    // Fixed actor pools bound this sweep. Relative motion catches crossing shots,
    // not just bullets near the destination; timed blasts use the arrival time.
    for (int i = 0; i < count; ++i) {
        const auto& t = threats[i];
        const float radius = t.radius + (t.boss ? 10.0f : (t.crushing ? 2.5f : 1.6f));
        float time = 0.0f;
        const float rx = t.x - x, ry = t.y - y, dx = t.vx - vx, dy = t.vy - vy;
        if (t.impactTime >= 0.0f) {
            if (t.impactTime > horizon) continue;
            time = t.impactTime;
        } else {
            const float speed2 = dx * dx + dy * dy;
            if (speed2 > 0.001f) time = (std::clamp)(-(rx * dx + ry * dy) / speed2, 0.0f, horizon);
        }
        const float cx = rx + dx * time, cy = ry + dy * time;
        const float overlap = radius * radius - cx * cx - cy * cy;
        if (overlap > 0.0f) danger += (t.boss ? 2.0f : 8.0f) * overlap / (radius * radius);
        // When already inside a danger circle, favour movement that exits it.
        const float ex = rx + dx * horizon, ey = ry + dy * horizon;
        const float endOverlap = radius * radius - ex * ex - ey * ey;
        if (endOverlap > 0.0f && t.impactTime < 0.0f) danger += endOverlap / (radius * radius);
    }
    return danger;
}
int GamingAI::PlanNavigationStep(AINavigationMemory& m,float x,float y,float gx,float gy,float dt,const AINavigationCandidate* candidates,int count,const AIGameStrategy& strategy,bool safetyFirst,bool exploreIfIdle) {
    m.sampleTime+=dt;
    if (m.sampleTime>=.4f) {
        const float dx=x-m.lastX,dy=y-m.lastY;
        if (dx*dx+dy*dy<.16f) { m.stalled+=m.sampleTime; ++m.failures; } else { m.stalled=0; m.failures=0; }
        m.visited[m.next]={x,y}; m.next=(m.next+1)%static_cast<int>(m.visited.size()); m.count=(std::min)(m.count+1,static_cast<int>(m.visited.size()));
        m.lastX=x; m.lastY=y; m.sampleTime=0;
    }
    float best=-1.0e30f; int selected=-1;
    float leastDanger = 1.0e30f;
    for (int i = 0; i < count && i < 32; ++i)
        if (candidates[i].clear && candidates[i].danger > 0.0f) safetyFirst = true;
    if (safetyFirst) for (int i = 0; i < count && i < 32; ++i)
        if (candidates[i].clear) leastDanger = (std::min)(leastDanger, candidates[i].danger);
    bool safeMovement = false;
    for (int i = 0; i < count && i < 32; ++i) {
        const auto& c = candidates[i];
        if (c.clear && (!safetyFirst || c.danger <= leastDanger + 0.001f)
            && std::hypot(c.x - x, c.y - y) > 0.1f) safeMovement = true;
    }
    const float distance=std::hypot(gx-x,gy-y);
    for (int i=0;i<count && i<32;++i) {
        const auto& c=candidates[i]; if (!c.clear) continue;
        // Survival outranks route progress, even when learning/adaptation is disabled.
        if (safetyFirst && c.danger > leastDanger + 0.001f) continue;
        if (exploreIfIdle && safeMovement && std::hypot(c.x-x,c.y-y) <= 0.1f) continue;
        float revisit=0;
        for (int j=0;j<m.count;++j) { const float dx=c.x-m.visited[j][0],dy=c.y-m.visited[j][1]; if(dx*dx+dy*dy<4.0f) revisit+=1.0f; }
        // A stalled route values unexplored, clear space more than a direct heading.
        const float progress=exploreIfIdle ? 0.0f : distance-std::hypot(gx-c.x,gy-c.y);
        const float score=progress*(m.stalled>.8f?.25f:1.0f)-revisit*(m.stalled>.8f?2.5f:.12f)-c.danger*(2.0f+strategy.avoidance*4.0f);
        if(score>best) {best=score;selected=i;}
    }
    return selected;
}

void GamingAI::ResetGameEncounters() { std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); m_gameHistory->ClearDefeats(); }
bool GamingAI::EncounterDefeated(uint64_t area,int kind) const {
    std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex);
    return m_gameHistory->encounterOverflow || m_gameHistory->FindDefeat((area<<8)|static_cast<uint8_t>(kind),false)!=0;
}
void GamingAI::RecordEncounterDefeat(uint64_t area,int kind) {
    auto& h=*m_gameHistory; std::lock_guard<std::mutex> lock(h.lifecycleMutex); const uint64_t key=(area<<8)|static_cast<uint8_t>(kind);
    if (h.FindDefeat(key,true)==2) { auto e=h.Make(GameHistory::BossDefeat); e.area=area; e.value=static_cast<float>(kind); if (h.record) h.Push(e); }
}

GamingAI::GamingAI() :
    m_isInitialized(false),                                             // System not initialized by default
    m_isMonitoring(false),                                              // Not monitoring by default
    m_analysisReady(false),                                             // No analysis ready initially
    m_shouldShutdown(false),                                            // No shutdown requested initially
    m_hasCleanedUp(false),                                              // Cleanup not performed by default
    m_commandsProcessed(0),                                             // No commands processed initially
    m_lastAnalysisTime(std::chrono::steady_clock::now()),               // Set current time as last analysis
    m_currentModelSize(0),                                              // No model data initially
    m_totalAnalysisCount(0),                                            // No analysis operations performed
    m_performanceStartTime(std::chrono::steady_clock::now()),           // Set performance monitoring start time
    m_sessionStartTime(std::chrono::steady_clock::now()),               // Set session start time
    m_currentSessionID(1),                                              // Start with session ID 1
    m_modelFilename("strategy.dat")                                     // Default model filename
{
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI constructor called - initializing AI intelligence system");
    #endif

    // Reserve memory for performance-critical containers to avoid reallocations
    m_sessionPlayerPositions.reserve(10000);                           // Reserve space for 10K position samples
    m_sessionInputEvents.reserve(5000);                                // Reserve space for 5K input events
    m_analysisTimings.reserve(1000);                                   // Reserve space for 1K timing samples

    m_gameHistory = std::make_unique<GameHistory>();
    // Initialize player analysis data container for maximum players
    m_playerAnalysisData.reserve(gamePlayer.GetActivePlayerCount());   // Reserve space based on active players

    // Initialize AI model data container
    m_aiModelData.reserve(1024 * 1024);                               // Reserve 1MB for initial model data

    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI constructor completed - system ready for initialization");
    #endif
}

// Destructor - Clean up GamingAI system resources
GamingAI::~GamingAI() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI destructor called - cleaning up AI intelligence system");
    #endif

    // Perform cleanup if not already done
    if (!m_hasCleanedUp.load()) {
        Cleanup();                                                      // Ensure proper cleanup
    }

    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI destructor completed - all resources cleaned up");
    #endif
}

//==============================================================================
// Core Initialization and Cleanup Methods
//==============================================================================

// Initialize the AI system with specified configuration
bool GamingAI::Initialize(const AIModelConfiguration& config) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::Initialize() called - starting AI system initialization");
    #endif

    // Prevent double initialization
    if (m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"GamingAI already initialized - skipping");
        return true;                                                    // Already initialized
    }

    // Use ThreadLockHelper for thread-safe initialization
    ThreadLockHelper initLock(threadManager, "gamingai_init", 5000);
    if (!initLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire initialization lock - cannot initialize GamingAI");
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Store configuration settings
        m_configuration = config;
        m_configuration.dataRetentionDays = (std::clamp)(m_configuration.dataRetentionDays, 1.0f, 21.0f);                                       // Save AI configuration

        // Validate configuration parameters
        if (m_configuration.maxModelSizeBytes < 1024 * 1024) {          // Minimum 1MB model size
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Model size too small, setting to minimum 1MB");
            m_configuration.maxModelSizeBytes = 1024 * 1024;            // Set minimum size
        }

        if (m_configuration.analysisIntervalSeconds < 10) {             // Minimum 10 second analysis interval
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Analysis interval too short, setting to minimum 10 seconds");
            m_configuration.analysisIntervalSeconds = 10;               // Set minimum interval
        }

        // Initialize platform-specific performance timers
        #if defined(PLATFORM_WINDOWS)
        m_performanceTimer = CreateWaitableTimer(NULL, TRUE, NULL);     // Create Windows waitable timer
        if (m_performanceTimer == NULL) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to create Windows performance timer");
            return false;                                               // Failed to create timer
        }
        #elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS) || defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
            clock_gettime(CLOCK_MONOTONIC, &m_performanceTimer);       // Initialize POSIX timer
        #endif

        // Initialize AI model data structures
        m_currentAnalysisResult = AIAnalysisResult();                   // Reset analysis results
        m_currentStrategy = EnemyAIStrategy();                          // Reset strategy
        m_currentModelSize.store(0);                                    // Reset model size

        // Attempt to load existing AI model from disk
        std::string modelPath = GetDefaultModelFilename();              // Get platform-specific model path
        if (ModelFileExists(modelPath)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Existing AI model found at: %S", modelPath.c_str());
#endif

            if (!LoadAIModel(modelPath)) {
                debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to load existing AI model - creating new model");
                ResetAIModel();                                         // Create new model if loading fails
            }
        }
        else {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"No existing AI model found - creating new model");
            #endif
            ResetAIModel();                                             // Create new AI model
        }

        m_shouldShutdown.store(false);
        // Create and start AI processing thread using ThreadManager
        threadManager.SetThread(THREAD_AI_PROCESSING, [this]() {
            AIThreadTasking();                                          // Main AI thread function
            }, true);                                                   // Enable debug mode for AI thread

        // Mark system as successfully initialized
        m_isInitialized.store(true);                                    // Set initialization flag
        m_hasCleanedUp.store(false);                                    // Clear cleanup flag

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"GamingAI initialization completed successfully - Model size: %zu bytes, Analysis interval: %u seconds",
                m_configuration.maxModelSizeBytes, m_configuration.analysisIntervalSeconds);
        #endif

        return true;                                                    // Initialization successful
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception during GamingAI initialization: %S", e.what());
        return false;                                                   // Initialization failed
    }
}

// Clean up all AI resources and save current model
void GamingAI::Cleanup() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::Cleanup() called - cleaning up AI intelligence system");
    #endif

    // Prevent double cleanup
    if (m_hasCleanedUp.load()) {
        return;                                                         // Already cleaned up
    }

    try {
        // Signal AI thread to shutdown
        EndGameSession(AIGameEnd::Quit);
        { std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); m_gameHistory->Push(m_gameHistory->Make(GameHistory::EngineStop)); }
        m_shouldShutdown.store(true);                                   // Set shutdown flag

        // Clear command queue and notify waiting threads
        {
            ThreadLockHelper cleanupLock(threadManager, "gamingai_cleanup", 3000);
            if (cleanupLock.IsLocked()) {
                ClearCommandQueue();                                    // Clear all pending commands
                m_commandAvailableCV.notify_all();                     // Wake up waiting threads
            }
        }

        // Save current AI model to disk before shutdown
        // Stop AI processing thread using ThreadManager
        if (threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
            threadManager.TerminateThread(THREAD_AI_PROCESSING);
            try {
                for (;;) { size_t pending; { std::lock_guard<std::mutex> lock(m_gameHistory->queueMutex); pending=m_gameHistory->size; }
                    if (!pending && !m_gameHistory->retryCount) break; m_gameHistory->Pump(); }
                m_gameHistory->journal.close();
            } catch (...) { m_gameHistory->healthy.store(false); }
             // Stop AI thread gracefully
        }

        // Finalize legacy monitoring only after the worker has joined. Session
        // containers must not be cleared while periodic analysis reads them.
        if (m_isMonitoring.load()) EndMonitoring();

        if (m_isInitialized.load() && m_currentModelSize.load() > 0) {
            std::string modelPath = GetDefaultModelFilename();         // Get model save path
            if (!SaveAIModel(modelPath)) {
                debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to save AI model during cleanup");
            }
        }

        // Clean up platform-specific resources
        #if defined(PLATFORM_WINDOWS)
            if (m_performanceTimer != NULL) {
                CloseHandle(m_performanceTimer);                       // Close Windows timer handle
                m_performanceTimer = NULL;
            }
        #endif

        // Clear all AI data structures
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_data_cleanup", 2000);
            if (dataLock.IsLocked()) {
                m_playerAnalysisData.clear();                           // Clear player analysis data
                m_aiModelData.clear();                                  // Clear AI model data
                m_aiModelData.shrink_to_fit();                          // Free allocated memory
                m_sessionPlayerPositions.clear();                      // Clear session position data
                m_sessionPlayerPositions.shrink_to_fit();               // Free allocated memory
                m_sessionInputEvents.clear();                          // Clear session input data
                m_sessionInputEvents.shrink_to_fit();                   // Free allocated memory
                m_analysisTimings.clear();                              // Clear timing data
                m_analysisTimings.shrink_to_fit();                      // Free allocated memory
            }
        }

        // Mark system as cleaned up
        m_isInitialized.store(false);                                   // Clear initialization flag
        m_hasCleanedUp.store(true);                                     // Set cleanup flag

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI cleanup completed successfully");
        #endif
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception during GamingAI cleanup: %S", e.what());
    }
}

//==============================================================================
// Monitoring Control Methods Implementation
//==============================================================================

// Start monitoring player behavior for analysis
bool GamingAI::StartMonitoring() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::StartMonitoring() called - starting player behavior monitoring");
    #endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot start monitoring - GamingAI system not initialized");
        return false;                                                   // System not initialized
    }

    // Check if already monitoring
    if (m_isMonitoring.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Already monitoring player behavior - ignoring start request");
        return true;                                                    // Already monitoring
    }

    // Use ThreadLockHelper for thread-safe monitoring state change
    ThreadLockHelper monitorLock(threadManager, "gamingai_monitor_start", 3000);
    if (!monitorLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire monitoring lock - cannot start monitoring");
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Initialize session tracking variables
        m_sessionStartTime = std::chrono::steady_clock::now();          // Record session start time
        m_currentSessionID.fetch_add(1);                               // Increment session ID counter

        // Clear previous session data to start fresh
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_session_data", 2000);
            if (dataLock.IsLocked()) {
                m_sessionPlayerPositions.clear();                      // Clear previous position data
                m_sessionInputEvents.clear();                          // Clear previous input event data

                // Reserve memory for new session data collection
                m_sessionPlayerPositions.reserve(10000);               // Reserve space for position tracking
                m_sessionInputEvents.reserve(5000);                    // Reserve space for input tracking
            }
        }

        // Get current active players for monitoring setup
        int activePlayerCount = gamePlayer.GetActivePlayerCount();
        std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Setting up monitoring for %d active players", activePlayerCount);
        #endif

        // Initialize analysis data structures for each active player
        for (int playerID : activePlayerIDs) {
            // Create or reset player analysis data entry
            PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];

            // Get player information from GamePlayer system
            const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(playerID);
            if (playerInfo != nullptr) {
                playerData.playerID = static_cast<uint32_t>(playerID);  // Set player ID
                playerData.playerName = playerInfo->playerName;         // Copy player name
                playerData.lastAnalysisTime = std::chrono::system_clock::now(); // Set analysis time
                playerData.isDataValid = true;                         // Mark data as valid for analysis

                // Reset movement pattern data for new session
                playerData.movementData = PlayerMovementPattern();
                playerData.combatData = PlayerCombatPattern();
                playerData.inputData = PlayerInputPattern();

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Initialized monitoring for player %d: %S",
                        playerID, std::wstring(playerInfo->playerName.begin(), playerInfo->playerName.end()).c_str());
                #endif
            }
        }

        // Inject initial monitoring commands into AI processing queue
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_NORMAL);
        InjectAICommand(AICommandType::CMD_ANALYZE_INPUT_PATTERNS, AICommandPriority::PRIORITY_NORMAL);

        // Set monitoring flag to start data collection
        m_isMonitoring.store(true);                                     // Enable monitoring state

        // Update performance monitoring start time
        m_performanceStartTime = std::chrono::steady_clock::now();      // Reset performance tracking

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Player behavior monitoring started successfully - Session ID: %u, Active players: %d",
                m_currentSessionID.load(), activePlayerCount);
        #endif

        return true;                                                    // Monitoring started successfully
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception starting monitoring: %S", e.what());

        // Ensure monitoring flag is cleared on error
        m_isMonitoring.store(false);                                    // Clear monitoring state
        return false;                                                   // Failed to start monitoring
    }
}

// Stop monitoring and finalize current session data
bool GamingAI::EndMonitoring() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::EndMonitoring() called - stopping player behavior monitoring");
    #endif

    // Check if currently monitoring
    if (!m_isMonitoring.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Not currently monitoring - ignoring end request");
        return true;                                                    // Not monitoring, nothing to stop
    }

    // Use ThreadLockHelper for thread-safe monitoring state change
    ThreadLockHelper monitorLock(threadManager, "gamingai_monitor_end", 3000);
    if (!monitorLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire monitoring lock - forcing monitoring stop");

        // Force stop monitoring even without lock in emergency situations
        m_isMonitoring.store(false);                                    // Force clear monitoring state
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Calculate session duration for analysis
        auto sessionEndTime = std::chrono::steady_clock::now();
        auto sessionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            sessionEndTime - m_sessionStartTime);

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Ending monitoring session - Duration: %lld ms, Session ID: %u",
                    sessionDuration.count(), m_currentSessionID.load());
        #endif

        // Inject final analysis commands to process collected session data
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_COMBAT, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_STRATEGY, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_GENERATE_ENEMY_STRATEGY, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_CLEAR_OUTDATED_DATA, AICommandPriority::PRIORITY_NORMAL);

        // Update session data for all monitored players
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_session_finalize", 2000);
            if (dataLock.IsLocked()) {
                // Finalize analysis data for each player
                for (auto& playerPair : m_playerAnalysisData) {
                    PlayerAnalysisData& playerData = playerPair.second;

                    // Update session statistics
                    playerData.sessionsAnalyzed++;                     // Increment session count
                    playerData.movementData.sessionDuration = sessionDuration; // Store session duration
                    playerData.lastAnalysisTime = std::chrono::system_clock::now(); // Update analysis time

                    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"Finalized session data for player %u - Total sessions: %u",
                            playerData.playerID, playerData.sessionsAnalyzed);
                    #endif
                }

                // Calculate and store session performance metrics
                size_t positionSamples = m_sessionPlayerPositions.size();
                size_t inputSamples = m_sessionInputEvents.size();

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_INFO,
                        L"Session data collected - Position samples: %zu, Input samples: %zu",
                        positionSamples, inputSamples);
                #endif
            }
        }

        // Save AI model with updated session data if auto-save is enabled
        if (m_configuration.enableCrossSessionLearning) {
            // Inject model save command with normal priority
            InjectAICommand(AICommandType::CMD_SAVE_AI_MODEL, AICommandPriority::PRIORITY_NORMAL);
        }

        // Clear monitoring flag to stop data collection
        m_isMonitoring.store(false);                                    // Disable monitoring state

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player behavior monitoring stopped successfully");
        #endif

        return true;                                                    // Monitoring stopped successfully
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception stopping monitoring: %S", e.what());

        // Ensure monitoring flag is cleared even on error
        m_isMonitoring.store(false);                                    // Force clear monitoring state
        return false;                                                   // Failed to stop monitoring cleanly
    }
}

//==============================================================================
// Real-time Data Collection Methods (Called during monitoring)
//==============================================================================

// Collect player position data during active monitoring
void GamingAI::CollectPlayerPositionData(uint32_t playerID, const Vector2& position) {
    // Only collect data if currently monitoring
    if (!m_isMonitoring.load()) {
        return;                                                         // Not monitoring, skip data collection
    }

    try {
        // Use fast, non-blocking lock for real-time data collection
        ThreadLockHelper dataLock(threadManager, "gamingai_position_collect", 100, true); // Silent lock with short timeout
        if (dataLock.IsLocked()) {
            // Add position to session tracking with timestamp
            m_sessionPlayerPositions.push_back(position);              // Store position data

            // Prevent unlimited memory growth by maintaining reasonable buffer size
            if (m_sessionPlayerPositions.size() > 15000) {             // Limit to 15K position samples
                // Remove oldest 5000 entries to maintain performance
                m_sessionPlayerPositions.erase(m_sessionPlayerPositions.begin(),
                    m_sessionPlayerPositions.begin() + 5000);

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Position buffer trimmed to maintain performance");
                #endif
            }

            // Update player-specific movement data
            auto playerIt = m_playerAnalysisData.find(playerID);
            if (playerIt != m_playerAnalysisData.end()) {
                PlayerAnalysisData& playerData = playerIt->second;

                // Add position to player's recent position history
                playerData.movementData.recentPositions.push_back(position);

                // Maintain reasonable history size for analysis
                if (playerData.movementData.recentPositions.size() > 500) {
                    playerData.movementData.recentPositions.erase(
                        playerData.movementData.recentPositions.begin(),
                        playerData.movementData.recentPositions.begin() + 100);
                }

                // Update movement sample count
                playerData.movementData.totalMovementSamples++;
            }
        }
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception collecting position data: %S", e.what());
    }
}

// Collect player input event data during active monitoring
void GamingAI::CollectInputEventData(uint32_t inputType, uint32_t inputValue) {
    // Only collect data if currently monitoring
    if (!m_isMonitoring.load()) {
        return;                                                         // Not monitoring, skip data collection
    }

    try {
        // Use fast, non-blocking lock for real-time data collection
        ThreadLockHelper dataLock(threadManager, "gamingai_input_collect", 50, true); // Very short timeout for input
        if (dataLock.IsLocked()) {
            // Encode input type and value into single uint32_t for efficient storage
            uint32_t encodedInput = (inputType << 16) | (inputValue & 0xFFFF);
            m_sessionInputEvents.push_back(encodedInput);              // Store encoded input event

            // Prevent unlimited memory growth for input events
            if (m_sessionInputEvents.size() > 10000) {                 // Limit to 10K input events
                // Remove oldest 2000 entries to maintain performance
                m_sessionInputEvents.erase(m_sessionInputEvents.begin(),
                    m_sessionInputEvents.begin() + 2000);

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Input buffer trimmed to maintain performance");
                #endif
            }
        }
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception collecting input data: %S", e.what());
    }
}

//==============================================================================
// AI Command Queue Methods Implementation
//==============================================================================

// Inject a command into the AI processing queue with priority
bool GamingAI::InjectAICommand(AICommandType commandType, AICommandPriority priority, uint32_t playerID, const std::string& commandData) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"GamingAI::InjectAICommand() called - Type: 0x%08X, Priority: %d, PlayerID: %u",
            static_cast<uint32_t>(commandType), static_cast<int>(priority), playerID);
    #endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
    debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot inject AI command - system not initialized");
        return false;                                                   // System not initialized
    }

    // Check if shutdown is requested
    if (m_shouldShutdown.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Cannot inject AI command - system shutting down");
        return false;                                                   // System shutting down
    }

    // Use ThreadLockHelper for thread-safe command queue access
    ThreadLockHelper queueLock(threadManager, "gamingai_command_queue", 2000);
    if (!queueLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire command queue lock - command rejected");
        return false;                                                   // Failed to acquire lock
    }

    std::lock_guard<std::mutex> queueGuard(m_commandQueueMutex);
    try {
        // Create AI command structure with provided parameters
        AICommand newCommand(commandType, priority, playerID, commandData);

        // Validate command parameters
        if (playerID > 0) {
                // Check if specified player ID is valid and active
                if (!gamePlayer.IsPlayerValid(static_cast<int>(playerID))) {
                debug.logDiagMessage(LogLevel::LOG_WARNING,
                    L"Invalid player ID %u in AI command - using player 0", playerID);
                newCommand.playerID = 0;                                // Default to player 0
            }
        }

        // Check command queue size to prevent memory exhaustion
        if (m_commandQueue.size() >= 1000) return false;   // hard bound, including high-priority producers
        GameHistory::Event audit; audit.kind=GameHistory::Request; audit.area=static_cast<uint32_t>(commandType);
        audit.frame.player=static_cast<int>(playerID); audit.accepted=true;
        if (!m_gameHistory->Push(audit) && commandType!=AICommandType::CMD_EMERGENCY_SHUTDOWN) return false;
        m_commandQueue.push(newCommand);                                // Insert command with priority ordering

        // Handle emergency commands immediately
        if (priority >= AICommandPriority::PRIORITY_EMERGENCY) {
            debug.logDiagLevelMessage(LogLevel::LOG_CRITICAL, L"Emergency AI command injected - immediate processing required");

            // Wake up AI thread immediately for emergency processing
            m_commandAvailableCV.notify_all();                         // Notify AI thread immediately
        }
        else if (priority >= AICommandPriority::PRIORITY_CRITICAL) {
            // Notify AI thread for critical commands
            m_commandAvailableCV.notify_one();                         // Notify AI thread
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"AI command injected successfully - Queue size: %zu, Command type: 0x%08X",
            m_commandQueue.size(), static_cast<uint32_t>(commandType));
#endif

        return true;                                                    // Command injected successfully
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception injecting AI command: %S", e.what());
        return false;                                                   // Failed to inject command
    }
}

// Get current command queue size for monitoring
size_t GamingAI::GetCommandQueueSize() const {
    std::lock_guard<std::mutex> queueGuard(m_commandQueueMutex);
    // Use ThreadLockHelper for thread-safe queue size access
    ThreadLockHelper queueLock(threadManager, "gamingai_queue_size", 500, true); // Silent with short timeout
    if (!queueLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for queue size - returning approximate size");
        return 0;                                                       // Cannot determine size safely
    }

    try {
        size_t queueSize = m_commandQueue.size();                      // Get current queue size

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"AI command queue size: %zu", queueSize);
#endif

        return queueSize;                                               // Return queue size
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception getting queue size: %S", e.what());
        return 0;                                                       // Return 0 on error
    }
}

// Clear all pending commands from queue
void GamingAI::ClearCommandQueue() {
    std::lock_guard<std::mutex> queueGuard(m_commandQueueMutex);
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ClearCommandQueue() called - clearing all pending commands");
#endif

    // Use ThreadLockHelper for thread-safe queue clearing
    ThreadLockHelper queueLock(threadManager, "gamingai_queue_clear", 3000);
    if (!queueLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for queue clear - forcing clear");
        // Force clear even without lock in emergency situations
    }

    try {
        size_t clearedCommands = m_commandQueue.size();                // Count commands to be cleared

        // Clear the priority queue by creating new empty queue
        std::priority_queue<AICommand> emptyQueue;
        m_commandQueue = std::move(emptyQueue);                        // Replace with empty queue

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"AI command queue cleared - %zu commands removed", clearedCommands);
#endif
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception clearing command queue: %S", e.what());
    }
}

//==============================================================================
// AI Command Processing Methods (Private)
//==============================================================================

// Process individual AI commands from queue (called by AI thread)
void GamingAI::ProcessAICommand(const AICommand& command) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"Processing AI command - Type: 0x%08X, Priority: %d, PlayerID: %u",
        static_cast<uint32_t>(command.commandType), static_cast<int>(command.priority), command.playerID);
#endif

    // Record command processing start time for performance measurement
    auto processingStartTime = std::chrono::steady_clock::now();

    try {
        // Process command based on its type
        switch (command.commandType) {
        case AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing player movement analysis for player %u", command.playerID);
#endif

            if (command.playerID == 0) {
                // Analyze all active players
                std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
                for (int playerID : activePlayerIDs) {
                    AnalyzePlayerMovement(static_cast<uint32_t>(playerID));
                }
            }
            else {
                // Analyze specific player
                AnalyzePlayerMovement(command.playerID);
            }
            break;
        }

        case AICommandType::CMD_ANALYZE_PLAYER_COMBAT:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing player combat analysis for player %u", command.playerID);
#endif

            if (command.playerID == 0) {
                // Analyze all active players
                std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
                for (int playerID : activePlayerIDs) {
                    AnalyzePlayerCombat(static_cast<uint32_t>(playerID));
                }
            }
            else {
                // Analyze specific player
                AnalyzePlayerCombat(command.playerID);
            }
            break;
        }

        case AICommandType::CMD_ANALYZE_PLAYER_STRATEGY:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing player strategy analysis");
#endif

            // Analyze overall player strategic patterns
            GenerateEnemyStrategy();                                // Generate strategic recommendations
            UpdateDifficultyRecommendations();                     // Update difficulty settings
            break;
        }

        case AICommandType::CMD_UPDATE_DIFFICULTY:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing difficulty update command");
#endif

            UpdateDifficultyRecommendations();                     // Recalculate difficulty recommendations
            break;
        }

        case AICommandType::CMD_SAVE_AI_MODEL:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing AI model save command");
#endif

            std::string filename = command.commandData.empty() ? GetDefaultModelFilename() : command.commandData;
            if (!SaveModelToDisk(filename)) {
                debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to save AI model during command processing");
            }
            break;
        }

        case AICommandType::CMD_LOAD_AI_MODEL:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing AI model load command");
#endif

            std::string filename = command.commandData.empty() ? GetDefaultModelFilename() : command.commandData;
            if (!LoadModelFromDisk(filename)) {
                debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to load AI model during command processing");
            }
            break;
        }

        case AICommandType::CMD_CLEAR_OUTDATED_DATA:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing outdated data cleanup command");
#endif

            ClearOutdatedData();                                    // Remove old analysis data
            break;
        }

        case AICommandType::CMD_GENERATE_ENEMY_STRATEGY:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing enemy strategy generation command");
#endif

            GenerateEnemyStrategy();                                // Generate new enemy strategies
            break;
        }

        case AICommandType::CMD_ANALYZE_INPUT_PATTERNS:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing input pattern analysis for player %u", command.playerID);
#endif

            if (command.playerID == 0) {
                // Analyze all active players
                std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
                for (int playerID : activePlayerIDs) {
                    AnalyzePlayerInput(static_cast<uint32_t>(playerID));
                }
            }
            else {
                // Analyze specific player
                AnalyzePlayerInput(command.playerID);
            }
            break;
        }

        case AICommandType::CMD_PREDICT_PLAYER_ACTION:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing player action prediction for player %u", command.playerID);
#endif

            // Predict next player action using historical patterns
            Vector2 predictedAction = PredictPlayerNextAction(command.playerID);

            // Store prediction results in current analysis data
            auto playerIt = m_playerAnalysisData.find(command.playerID);
            if (playerIt != m_playerAnalysisData.end()) {
                // Update player analysis with prediction data
                PlayerAnalysisData& playerData = playerIt->second;
                // Prediction results could be stored in a future enhancement
            }
            break;
        }

        case AICommandType::CMD_EMERGENCY_SHUTDOWN:
        {
            debug.logDiagLevelMessage(LogLevel::LOG_CRITICAL, L"Processing emergency shutdown command");

            // Set shutdown flag and force immediate cleanup
            m_shouldShutdown.store(true);                           // Signal shutdown

            // Save critical data before shutdown
            if (m_currentModelSize.load() > 0) {
                SaveModelToDisk(GetDefaultModelFilename());         // Emergency model save
            }
            break;
        }

        default:
        {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Unknown AI command type: 0x%08X - ignoring command", static_cast<uint32_t>(command.commandType));
            break;
        }
        }

        // Update command processing statistics
        m_commandsProcessed.fetch_add(1);                               // Increment processed command counter

        // Record processing time for performance monitoring
        auto processingEndTime = std::chrono::steady_clock::now();
        auto processingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            processingEndTime - processingStartTime);

        // Store timing data for performance analysis
        {
            ThreadLockHelper timingLock(threadManager, "gamingai_timing", 100, true); // Silent timing lock
            if (timingLock.IsLocked()) {
                m_analysisTimings.push_back(processingDuration);       // Store processing time

                // Maintain reasonable timing history size
                if (m_analysisTimings.size() > 1000) {
                    m_analysisTimings.erase(m_analysisTimings.begin(), m_analysisTimings.begin() + 200);
                }
            }
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"AI command processed successfully - Duration: %lld ms, Total processed: %zu",
            processingDuration.count(), m_commandsProcessed.load());
#endif
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION,
            L"Exception processing AI command 0x%08X: %S", static_cast<uint32_t>(command.commandType), e.what());
    }
}

//==============================================================================
// Analysis Result Methods Implementation
//==============================================================================

// Return comprehensive AI analysis results (thread-safe)
AIAnalysisResult GamingAI::ReturnAIAnalysis() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ReturnAIAnalysis() called - retrieving current analysis results");
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot return analysis - GamingAI system not initialized");

        // Return empty result with error indication
        AIAnalysisResult emptyResult;
        emptyResult.isAnalysisValid = false;
        emptyResult.analysisNotes = "AI system not initialized";
        return emptyResult;
    }

    // Use ThreadLockHelper for thread-safe analysis data access with extended timeout
    ThreadLockHelper analysisLock(threadManager, "gamingai_analysis_result", 5000);
    if (!analysisLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire analysis lock - returning last known results");

        // Return last known analysis with warning
        AIAnalysisResult warningResult = m_currentAnalysisResult;
        warningResult.analysisNotes += " [WARNING: Lock timeout - data may be stale]";
        return warningResult;
    }

    try {
        // Create comprehensive analysis result structure
        AIAnalysisResult analysisResult;

        // Set basic result metadata
        analysisResult.isAnalysisValid = true;                         // Mark as valid analysis
        analysisResult.analysisTimestamp = std::chrono::steady_clock::now(); // Set current timestamp
        analysisResult.analysisVersion = m_currentAnalysisResult.analysisVersion + 1; // Increment version

        // Get current active players for analysis
        std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
        analysisResult.analyzedPlayerCount = static_cast<uint32_t>(activePlayerIDs.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Preparing analysis results for %u active players", analysisResult.analyzedPlayerCount);
#endif

        // Clear and reserve space for player analysis data
        analysisResult.playerAnalysis.clear();
        analysisResult.playerAnalysis.reserve(activePlayerIDs.size());

        // Compile analysis data for each active player
        float totalSkillLevel = 0.0f;                                  // For calculating average skill
        uint32_t validPlayerCount = 0;                                 // Count of players with valid data

        for (int playerID : activePlayerIDs) {
            auto playerIt = m_playerAnalysisData.find(static_cast<uint32_t>(playerID));
            if (playerIt != m_playerAnalysisData.end()) {
                const PlayerAnalysisData& playerData = playerIt->second;

                // Validate player data quality before including in results
                if (ValidateAnalysisData(playerData)) {
                    // Copy player analysis data to result
                    analysisResult.playerAnalysis.push_back(playerData);

                    // Accumulate skill levels for average calculation
                    totalSkillLevel += static_cast<float>(playerData.skillLevel);
                    validPlayerCount++;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Added player %u analysis - Skill: %u, Sessions: %u",
                        playerData.playerID, playerData.skillLevel, playerData.sessionsAnalyzed);
#endif
                }
                else {
                    debug.logDiagMessage(LogLevel::LOG_WARNING,
                        L"Player %u analysis data failed validation - excluding from results",
                        static_cast<uint32_t>(playerID));
                }
            }
        }

        // Calculate overall difficulty recommendation based on player skill levels
        if (validPlayerCount > 0) {
            float averageSkillLevel = totalSkillLevel / static_cast<float>(validPlayerCount);

            // Use MathPrecalculation for smooth difficulty curve calculation
            float normalizedSkill = averageSkillLevel / 100.0f;         // Normalize to [0, 1] range

            // Apply smooth step interpolation for difficulty curve
            analysisResult.overallDifficultyRecommendation = FAST_MATH.FastSmoothStep(0.1f, 0.9f, normalizedSkill);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Calculated difficulty recommendation: %.3f (based on average skill: %.1f)",
                analysisResult.overallDifficultyRecommendation, averageSkillLevel);
#endif
        }
        else {
            // Default difficulty if no valid player data
            analysisResult.overallDifficultyRecommendation = 0.5f;      // Medium difficulty default

            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"No valid player data - using default difficulty");
        }

        // Copy current enemy strategy recommendations
        analysisResult.recommendedStrategy = m_currentStrategy;

        // Generate comprehensive analysis notes
        std::ostringstream notesStream;
        notesStream << "Analysis based on " << validPlayerCount << " players. ";
        notesStream << "Total analysis operations: " << m_totalAnalysisCount.load() << ". ";

        // Add performance metrics to notes
        if (!m_analysisTimings.empty()) {
            // Calculate average analysis time using MathPrecalculation
            float totalTime = 0.0f;
            for (const auto& timing : m_analysisTimings) {
                totalTime += static_cast<float>(timing.count());
            }
            float averageTime = totalTime / static_cast<float>(m_analysisTimings.size());

            notesStream << "Average analysis time: " << std::fixed << std::setprecision(2) << averageTime << "ms. ";
        }

        // Add monitoring status to notes
        if (m_isMonitoring.load()) {
            auto monitorDuration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_sessionStartTime);
            notesStream << "Currently monitoring (Session duration: " << monitorDuration.count() << "s). ";
        }

        // Add model size information
        size_t currentModelSize = m_currentModelSize.load();
        notesStream << "AI model size: " << (currentModelSize / 1024) << "KB";
        if (currentModelSize > m_configuration.maxModelSizeBytes * 0.8f) {
            notesStream << " [WARNING: Approaching size limit]";
        }

        analysisResult.analysisNotes = notesStream.str();

        // Update stored analysis result for future reference
        m_currentAnalysisResult = analysisResult;

        // Mark analysis as ready for external consumption
        m_analysisReady.store(true);

        // Increment total analysis count for statistics
        m_totalAnalysisCount.fetch_add(1);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Analysis results compiled successfully - Players: %u, Difficulty: %.3f, Valid: %s",
            analysisResult.analyzedPlayerCount, analysisResult.overallDifficultyRecommendation,
            analysisResult.isAnalysisValid ? L"Yes" : L"No");
#endif

        return analysisResult;                                          // Return complete analysis result

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception generating analysis results: %S", e.what());

        // Return error result on exception
        AIAnalysisResult errorResult;
        errorResult.isAnalysisValid = false;
        errorResult.analysisNotes = "Exception occurred during analysis generation";
        errorResult.analysisTimestamp = std::chrono::steady_clock::now();
        return errorResult;
    }
}

// Force immediate analysis update
bool GamingAI::ForceAnalysisUpdate() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ForceAnalysisUpdate() called - forcing immediate analysis");
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot force analysis update - system not initialized");
        return false;                                                   // System not initialized
    }

    try {
        // Inject high-priority analysis commands for immediate processing
        bool allCommandsInjected = true;

        // Force player movement analysis
        if (!InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject movement analysis command");
        }

        // Force player combat analysis
        if (!InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_COMBAT, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject combat analysis command");
        }

        // Force input pattern analysis
        if (!InjectAICommand(AICommandType::CMD_ANALYZE_INPUT_PATTERNS, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject input analysis command");
        }

        // Force strategy generation
        if (!InjectAICommand(AICommandType::CMD_GENERATE_ENEMY_STRATEGY, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject strategy generation command");
        }

        // Force difficulty update
        if (!InjectAICommand(AICommandType::CMD_UPDATE_DIFFICULTY, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject difficulty update command");
        }

        // Wake up AI thread to process commands immediately
        {
            ThreadLockHelper notifyLock(threadManager, "gamingai_force_notify", 1000);
            if (notifyLock.IsLocked()) {
                m_commandAvailableCV.notify_all();                     // Wake up AI processing thread

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"AI thread notified for immediate analysis processing");
#endif
            }
        }

        // Wait briefly for processing to begin (non-blocking check)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));    // Brief wait for processing start

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Force analysis update completed - Commands injected: %s, Queue size: %zu",
            allCommandsInjected ? L"Success" : L"Partial", GetCommandQueueSize());
#endif

        return allCommandsInjected;                                     // Return success status
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception forcing analysis update: %S", e.what());
        return false;                                                   // Failed to force update
    }
}

//==============================================================================
// Analysis Data Validation and Quality Assurance Methods
//==============================================================================

// Validate player analysis data quality before including in results
bool GamingAI::ValidateAnalysisData(const PlayerAnalysisData& data) const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Validating analysis data for player %u", data.playerID);
#endif

    try {
        // Check basic data validity flags
        if (!data.isDataValid) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u data marked as invalid", data.playerID);
#endif
            return false;                                               // Data marked as invalid
        }

        // Validate player ID range
        if (data.playerID >= 100) {                                     // Reasonable player ID limit
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player ID %u exceeds reasonable range", data.playerID);
            return false;                                               // Invalid player ID
        }

        // Validate skill level range
        if (data.skillLevel > 100) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u skill level %u exceeds maximum",
                data.playerID, data.skillLevel);
            return false;                                               // Invalid skill level
        }

        // Validate adaptability factor range
        if (data.adaptabilityFactor < 0.0f || data.adaptabilityFactor > 1.0f) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u adaptability factor %.3f out of range",
                data.playerID, data.adaptabilityFactor);
            return false;                                               // Invalid adaptability factor
        }

        // Validate movement data
        if (data.movementData.movementPredictability < 0.0f || data.movementData.movementPredictability > 1.0f) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u movement predictability %.3f out of range",
                data.playerID, data.movementData.movementPredictability);
            return false;                                               // Invalid movement predictability
        }

        // Validate reaction time (should be reasonable for human players)
        if (data.movementData.reactionTime < 50.0f || data.movementData.reactionTime > 5000.0f) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u reaction time %.1fms unrealistic",
                data.playerID, data.movementData.reactionTime);
            return false;                                               // Unrealistic reaction time
        }

        // Validate combat data
        if (data.combatData.accuracyPercentage < 0.0f || data.combatData.accuracyPercentage > 1.0f) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u accuracy %.3f out of range",
                data.playerID, data.combatData.accuracyPercentage);
            return false;                                               // Invalid accuracy percentage
        }

        // Validate that player has sufficient data for meaningful analysis
        if (data.sessionsAnalyzed == 0) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u has no analyzed sessions", data.playerID);
#endif
            return false;                                               // No session data available
        }

        // Check data freshness (analysis should be recent)
        auto currentTime = std::chrono::system_clock::now();
        auto dataAge = std::chrono::duration_cast<std::chrono::hours>(currentTime - data.lastAnalysisTime);

        if (dataAge.count() > 72) {                                     // Data older than 72 hours
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u analysis data is %lld hours old",
                data.playerID, dataAge.count());
            return false;                                               // Data too old
        }

        // Validate movement sample count
        if (data.movementData.totalMovementSamples < 10) {              // Minimum samples for valid analysis
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u has insufficient movement samples: %u",
                data.playerID, data.movementData.totalMovementSamples);
#endif
            return false;                                               // Insufficient movement data
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u analysis data validation passed", data.playerID);
#endif

        return true;                                                    // All validation checks passed
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception validating player %u data: %S",
            data.playerID, e.what());
        return false;                                                   // Failed validation due to exception
    }
}

//==============================================================================
// Performance Monitoring Methods for Analysis
//==============================================================================

// Get average analysis processing time
std::chrono::milliseconds GamingAI::GetAverageAnalysisTime() const {
    // Use ThreadLockHelper for thread-safe access to timing data
    ThreadLockHelper timingLock(threadManager, "gamingai_avg_timing", 1000, true); // Silent with timeout
    if (!timingLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire timing lock - returning zero");
        return std::chrono::milliseconds(0);                           // Cannot access timing data
    }

    try {
        if (m_analysisTimings.empty()) {
            return std::chrono::milliseconds(0);                       // No timing data available
        }

        // Calculate average using MathPrecalculation for precision
        uint64_t totalTime = 0;
        for (const auto& timing : m_analysisTimings) {
            totalTime += static_cast<uint64_t>(timing.count());
        }

        uint64_t averageTime = totalTime / m_analysisTimings.size();

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Average analysis time calculated: %llu ms (from %zu samples)",
            averageTime, m_analysisTimings.size());
#endif

        return std::chrono::milliseconds(averageTime);
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception calculating average analysis time: %S", e.what());
        return std::chrono::milliseconds(0);                           // Return zero on error
    }
}

// Get AI thread performance metrics
bool GamingAI::GetThreadPerformanceMetrics(float& cpuUsage, uint64_t& memoryUsage) const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Retrieving AI thread performance metrics");
#endif

    try {
        // Initialize output parameters
        cpuUsage = 0.0f;
        memoryUsage = 0;

        // Check if AI thread exists and is running
        if (!threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"AI thread does not exist - cannot get performance metrics");
            return false;                                               // Thread doesn't exist
        }

        // Get current model size as memory usage approximation
        memoryUsage = m_currentModelSize.load();

        // Add estimated memory usage for analysis structures
        memoryUsage += m_playerAnalysisData.size() * sizeof(PlayerAnalysisData);
        memoryUsage += m_sessionPlayerPositions.size() * sizeof(Vector2);
        memoryUsage += m_sessionInputEvents.size() * sizeof(uint32_t);
        memoryUsage += m_analysisTimings.size() * sizeof(std::chrono::milliseconds);

        // Estimate CPU usage based on recent processing activity
        auto currentTime = std::chrono::steady_clock::now();
        auto timeSinceLastAnalysis = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - m_lastAnalysisTime);

        // Calculate CPU usage estimate based on analysis frequency and timing
        if (timeSinceLastAnalysis.count() < m_configuration.analysisIntervalSeconds * 2) {
            // Recent activity detected
            if (!m_analysisTimings.empty()) {
                // Estimate CPU usage based on average analysis time vs interval
                uint64_t avgAnalysisMs = static_cast<uint64_t>(GetAverageAnalysisTime().count());
                uint64_t intervalMs = static_cast<uint64_t>(m_configuration.analysisIntervalSeconds) * 1000;

                cpuUsage = static_cast<float>(avgAnalysisMs) / static_cast<float>(intervalMs);
                cpuUsage = std::min(cpuUsage, 1.0f);                   // Cap at 100%
            }
            else {
                cpuUsage = 0.1f;                                        // Minimal usage estimate
            }
        }
        else {
            cpuUsage = 0.05f;                                           // Low usage when inactive
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Performance metrics - CPU: %.1f%%, Memory: %llu bytes",
            cpuUsage * 100.0f, memoryUsage);
#endif

        return true;                                                    // Successfully retrieved metrics
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception getting performance metrics: %S", e.what());

        // Reset output parameters on error
        cpuUsage = 0.0f;
        memoryUsage = 0;
        return false;                                                   // Failed to get metrics
    }
}

//==============================================================================
// Configuration Management Methods Implementation
//==============================================================================

// Update AI configuration settings
bool GamingAI::UpdateConfiguration(const AIModelConfiguration& config) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::UpdateConfiguration() called - updating AI configuration settings");
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot update configuration - GamingAI system not initialized");
        return false;                                                   // System not initialized
    }

    // Use ThreadLockHelper for thread-safe configuration update
    ThreadLockHelper configLock(threadManager, "gamingai_config_update", 3000);
    if (!configLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire configuration lock - update rejected");
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Store current configuration for rollback if needed
        AIModelConfiguration previousConfig = m_configuration;

        // Validate new configuration parameters before applying
        AIModelConfiguration validatedConfig = config;                 // Copy for validation

        // Validate maximum model size (minimum 1MB, maximum 2GB)
        if (validatedConfig.maxModelSizeBytes < 1024 * 1024) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Model size too small, adjusting to minimum 1MB");
            validatedConfig.maxModelSizeBytes = 1024 * 1024;           // Set minimum 1MB
        }
        else if (validatedConfig.maxModelSizeBytes > 2ULL * 1024 * 1024 * 1024) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Model size too large, adjusting to maximum 2GB");
            validatedConfig.maxModelSizeBytes = 2ULL * 1024 * 1024 * 1024; // Set maximum 2GB
        }

        // Validate analysis interval (minimum 5 seconds, maximum 300 seconds)
        if (validatedConfig.analysisIntervalSeconds < 5) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Analysis interval too short, adjusting to minimum 5 seconds");
            validatedConfig.analysisIntervalSeconds = 5;               // Set minimum 5 seconds
        }
        else if (validatedConfig.analysisIntervalSeconds > 300) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Analysis interval too long, adjusting to maximum 300 seconds");
            validatedConfig.analysisIntervalSeconds = 300;             // Set maximum 5 minutes
        }

        // Validate data retention period (minimum 1 day, maximum 365 days)
        if (validatedConfig.dataRetentionDays < 1.0f) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Data retention too short, adjusting to minimum 1 day");
            validatedConfig.dataRetentionDays = 1.0f;                  // Set minimum 1 day
        }
        else if (validatedConfig.dataRetentionDays > 21.0f) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Data retention too long, adjusting to maximum 365 days");
            validatedConfig.dataRetentionDays = 21.0f;                // Set maximum 21 days
        }

        // Validate learning rate (must be between 0.01 and 1.0)
        if (validatedConfig.learningRate < 0.01f) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Learning rate too low, adjusting to minimum 0.01");
            validatedConfig.learningRate = 0.01f;                      // Set minimum learning rate
        }
        else if (validatedConfig.learningRate > 1.0f) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Learning rate too high, adjusting to maximum 1.0");
            validatedConfig.learningRate = 1.0f;                       // Set maximum learning rate
        }

        // Validate max player history entries (minimum 100, maximum 10000)
        if (validatedConfig.maxPlayerHistoryEntries < 100) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Player history entries too few, adjusting to minimum 100");
            validatedConfig.maxPlayerHistoryEntries = 100;             // Set minimum entries
        }
        else if (validatedConfig.maxPlayerHistoryEntries > 10000) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Player history entries too many, adjusting to maximum 10000");
            validatedConfig.maxPlayerHistoryEntries = 10000;           // Set maximum entries
        }

        // Check if current model size exceeds new limit
        size_t currentModelSize = m_currentModelSize.load();
        if (currentModelSize > validatedConfig.maxModelSizeBytes) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Current model size (%zu bytes) exceeds new limit (%zu bytes) - cleanup required",
                currentModelSize, validatedConfig.maxModelSizeBytes);

            // Inject command to clean outdated data to reduce model size
            InjectAICommand(AICommandType::CMD_CLEAR_OUTDATED_DATA, AICommandPriority::PRIORITY_HIGH);
        }

        // Apply validated configuration
        m_configuration = validatedConfig;

        // Log configuration changes for debugging
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Configuration updated successfully:");
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Max model size: %zu bytes (%.1f MB)",
            m_configuration.maxModelSizeBytes,
            static_cast<float>(m_configuration.maxModelSizeBytes) / (1024.0f * 1024.0f));
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Analysis interval: %u seconds",
            m_configuration.analysisIntervalSeconds);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Data retention: %.1f days",
            m_configuration.dataRetentionDays);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Learning rate: %.3f",
            m_configuration.learningRate);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Max history entries: %u",
            m_configuration.maxPlayerHistoryEntries);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Advanced prediction: %s",
            m_configuration.enableAdvancedPrediction ? L"Enabled" : L"Disabled");
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Cross-session learning: %s",
            m_configuration.enableCrossSessionLearning ? L"Enabled" : L"Disabled");
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Real-time analysis: %s",
            m_configuration.enableRealTimeAnalysis ? L"Enabled" : L"Disabled");
#endif

        return true;                                                    // Configuration updated successfully
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception updating configuration: %S", e.what());
        return false;                                                   // Failed to update configuration
    }
}

// Get current AI configuration
AIModelConfiguration GamingAI::GetConfiguration() const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GamingAI::GetConfiguration() called - retrieving current configuration");
#endif

    // Use ThreadLockHelper for thread-safe configuration access
    ThreadLockHelper configLock(threadManager, "gamingai_config_get", 1000, true); // Silent with short timeout
    if (!configLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire configuration lock - returning default config");
        return AIModelConfiguration();                                  // Return default configuration
    }

    try {
        // Return copy of current configuration
        AIModelConfiguration currentConfig = m_configuration;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Configuration retrieved - Model size: %zu bytes, Analysis interval: %u seconds",
            currentConfig.maxModelSizeBytes, currentConfig.analysisIntervalSeconds);
#endif

        return currentConfig;                                           // Return current configuration
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception getting configuration: %S", e.what());
        return AIModelConfiguration();                                  // Return default on error
    }
}

// Set maximum AI model size
void GamingAI::SetMaxModelSize(size_t sizeInBytes) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::SetMaxModelSize() called - setting size to %zu bytes", sizeInBytes);
#endif

    // Use ThreadLockHelper for thread-safe model size update
    ThreadLockHelper configLock(threadManager, "gamingai_modelsize_set", 2000);
    if (!configLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for model size update");
        return;                                                         // Failed to acquire lock
    }

    try {
        // Validate new model size
        size_t validatedSize = sizeInBytes;

        // Apply minimum and maximum limits
        const size_t MIN_MODEL_SIZE = 1024 * 1024;                     // 1MB minimum
        const size_t MAX_MODEL_SIZE = 2ULL * 1024 * 1024 * 1024;       // 2GB maximum

        if (validatedSize < MIN_MODEL_SIZE) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Model size %zu too small, adjusting to minimum %zu bytes",
                validatedSize, MIN_MODEL_SIZE);
            validatedSize = MIN_MODEL_SIZE;                             // Set to minimum
        }
        else if (validatedSize > MAX_MODEL_SIZE) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Model size %zu too large, adjusting to maximum %zu bytes",
                validatedSize, MAX_MODEL_SIZE);
            validatedSize = MAX_MODEL_SIZE;                             // Set to maximum
        }

        // Check if current model exceeds new size limit
        size_t currentModelSize = m_currentModelSize.load();
        if (currentModelSize > validatedSize) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Current model size (%zu) exceeds new limit (%zu) - triggering cleanup",
                currentModelSize, validatedSize);

            // Inject cleanup command to reduce model size
            InjectAICommand(AICommandType::CMD_CLEAR_OUTDATED_DATA, AICommandPriority::PRIORITY_HIGH);
        }

        // Update configuration with new model size
        m_configuration.maxModelSizeBytes = validatedSize;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Max model size updated to %zu bytes (%.2f MB)",
            validatedSize, static_cast<float>(validatedSize) / (1024.0f * 1024.0f));
#endif
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception setting max model size: %S", e.what());
    }
}

// Set analysis interval
void GamingAI::SetAnalysisInterval(uint32_t seconds) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::SetAnalysisInterval() called - setting interval to %u seconds", seconds);
#endif

    // Use ThreadLockHelper for thread-safe interval update
    ThreadLockHelper configLock(threadManager, "gamingai_interval_set", 2000);
    if (!configLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for analysis interval update");
        return;                                                         // Failed to acquire lock
    }

    try {
        // Validate new analysis interval
        uint32_t validatedInterval = seconds;

        // Apply minimum and maximum limits
        const uint32_t MIN_INTERVAL = 5;                               // 5 seconds minimum
        const uint32_t MAX_INTERVAL = 300;                             // 5 minutes maximum

        if (validatedInterval < MIN_INTERVAL) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Analysis interval %u too short, adjusting to minimum %u seconds",
                validatedInterval, MIN_INTERVAL);
            validatedInterval = MIN_INTERVAL;                           // Set to minimum
        }
        else if (validatedInterval > MAX_INTERVAL) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Analysis interval %u too long, adjusting to maximum %u seconds",
                validatedInterval, MAX_INTERVAL);
            validatedInterval = MAX_INTERVAL;                           // Set to maximum
        }

        // Update configuration with new analysis interval
        uint32_t previousInterval = m_configuration.analysisIntervalSeconds;
        m_configuration.analysisIntervalSeconds = validatedInterval;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Analysis interval updated from %u to %u seconds",
            previousInterval, validatedInterval);
#endif

        // If interval was shortened significantly, trigger immediate analysis
        if (validatedInterval < previousInterval / 2) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Interval shortened significantly - triggering immediate analysis");
#endif

            // Inject analysis command to adapt to new timing
            InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_NORMAL);
        }
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception setting analysis interval: %S", e.what());
    }
}

//==============================================================================
// Advanced Configuration Management Methods
//==============================================================================

// Update specific configuration parameter by name (for external config systems)
bool GamingAI::UpdateConfigurationParameter(const std::string& parameterName, const std::string& parameterValue) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"GamingAI::UpdateConfigurationParameter() called - Parameter: %S, Value: %S",
        parameterName.c_str(), parameterValue.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot update parameter - system not initialized");
        return false;                                                   // System not initialized
    }

    // Use ThreadLockHelper for thread-safe parameter update
    ThreadLockHelper configLock(threadManager, "gamingai_param_update", 2000);
    if (!configLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for parameter update");
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Process parameter updates based on parameter name
        if (parameterName == "maxModelSizeBytes") {
            size_t newSize = std::stoull(parameterValue);
            SetMaxModelSize(newSize);
            return true;
        }
        else if (parameterName == "analysisIntervalSeconds") {
            uint32_t newInterval = static_cast<uint32_t>(std::stoul(parameterValue));
            SetAnalysisInterval(newInterval);
            return true;
        }
        else if (parameterName == "dataRetentionDays") {
            float newRetention = std::stof(parameterValue);
            if (newRetention >= 1.0f && newRetention <= 21.0f) {
                m_configuration.dataRetentionDays = newRetention;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Data retention updated to %.1f days", newRetention);
#endif
                return true;
            }
        }
        else if (parameterName == "learningRate") {
            float newRate = std::stof(parameterValue);
            if (newRate >= 0.01f && newRate <= 1.0f) {
                m_configuration.learningRate = newRate;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Learning rate updated to %.3f", newRate);
#endif
                return true;
            }
        }
        else if (parameterName == "maxPlayerHistoryEntries") {
            uint32_t newEntries = static_cast<uint32_t>(std::stoul(parameterValue));
            if (newEntries >= 100 && newEntries <= 10000) {
                m_configuration.maxPlayerHistoryEntries = newEntries;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Max player history entries updated to %u", newEntries);
#endif
                return true;
            }
        }
        else if (parameterName == "enableAdvancedPrediction") {
            bool newValue = (parameterValue == "true" || parameterValue == "1");
            m_configuration.enableAdvancedPrediction = newValue;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Advanced prediction %s", newValue ? L"enabled" : L"disabled");
#endif
            return true;
        }
        else if (parameterName == "enableCrossSessionLearning") {
            bool newValue = (parameterValue == "true" || parameterValue == "1");
            m_configuration.enableCrossSessionLearning = newValue;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Cross-session learning %s", newValue ? L"enabled" : L"disabled");
#endif
            return true;
        }
        else if (parameterName == "enableRealTimeAnalysis") {
            bool newValue = (parameterValue == "true" || parameterValue == "1");
            m_configuration.enableRealTimeAnalysis = newValue;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Real-time analysis %s", newValue ? L"enabled" : L"disabled");
#endif
            return true;
        }
        else {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Unknown configuration parameter: %S", parameterName.c_str());
            return false;                                               // Unknown parameter
        }

        debug.logDiagMessage(LogLevel::LOG_WARNING,
            L"Parameter %S value %S out of valid range",
            parameterName.c_str(), parameterValue.c_str());
        return false;                                                   // Parameter value out of range
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION,
            L"Exception updating parameter %S: %S", parameterName.c_str(), e.what());
        return false;                                                   // Failed to update parameter
    }
}

// Export current configuration to string format (for saving to config files)
std::string GamingAI::ExportConfiguration() const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GamingAI::ExportConfiguration() called - exporting current configuration");
#endif

    // Use ThreadLockHelper for thread-safe configuration export
    ThreadLockHelper configLock(threadManager, "gamingai_config_export", 1000, true); // Silent with timeout
    if (!configLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for config export - using cached data");
    }

    try {
        // Create configuration string in key=value format
        std::ostringstream configStream;

        configStream << "# GamingAI Configuration Export\n";
        configStream << "# Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\n\n";

        configStream << "maxModelSizeBytes=" << m_configuration.maxModelSizeBytes << "\n";
        configStream << "analysisIntervalSeconds=" << m_configuration.analysisIntervalSeconds << "\n";
        configStream << "dataRetentionDays=" << std::fixed << std::setprecision(1) << m_configuration.dataRetentionDays << "\n";
        configStream << "learningRate=" << std::fixed << std::setprecision(3) << m_configuration.learningRate << "\n";
        configStream << "maxPlayerHistoryEntries=" << m_configuration.maxPlayerHistoryEntries << "\n";
        configStream << "enableAdvancedPrediction=" << (m_configuration.enableAdvancedPrediction ? "true" : "false") << "\n";
        configStream << "enableCrossSessionLearning=" << (m_configuration.enableCrossSessionLearning ? "true" : "false") << "\n";
        configStream << "enableRealTimeAnalysis=" << (m_configuration.enableRealTimeAnalysis ? "true" : "false") << "\n";

        std::string configString = configStream.str();

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Configuration exported - %zu characters", configString.length());
#endif

        return configString;                                            // Return configuration string
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception exporting configuration: %S", e.what());
        return "";                                                      // Return empty string on error
    }
}

// Import configuration from string format (for loading from config files)
bool GamingAI::ImportConfiguration(const std::string& configString) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"GamingAI::ImportConfiguration() called - importing configuration (%zu characters)",
        configString.length());
#endif

    try {
        // Parse configuration string line by line
        std::istringstream configStream(configString);
        std::string line;
        int parametersUpdated = 0;

        while (std::getline(configStream, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Find parameter=value separator
            size_t equalPos = line.find('=');
            if (equalPos == std::string::npos) {
                continue;                                               // Skip malformed lines
            }

            // Extract parameter name and value
            std::string paramName = line.substr(0, equalPos);
            std::string paramValue = line.substr(equalPos + 1);

            // Remove whitespace
            paramName.erase(0, paramName.find_first_not_of(" \t"));
            paramName.erase(paramName.find_last_not_of(" \t") + 1);
            paramValue.erase(0, paramValue.find_first_not_of(" \t"));
            paramValue.erase(paramValue.find_last_not_of(" \t") + 1);

            // Update parameter if valid
            if (UpdateConfigurationParameter(paramName, paramValue)) {
                parametersUpdated++;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"Updated parameter: %S = %S", paramName.c_str(), paramValue.c_str());
#endif
            }
            else {
                debug.logDiagMessage(LogLevel::LOG_WARNING,
                    L"Failed to update parameter: %S = %S", paramName.c_str(), paramValue.c_str());
            }
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Configuration import completed - %d parameters updated", parametersUpdated);
#endif

        return parametersUpdated > 0;                                   // Return success if any parameters updated
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception importing configuration: %S", e.what());
        return false;                                                   // Failed to import configuration
    }
}

//==============================================================================
// Model Persistence Methods Implementation
//==============================================================================

// Save current AI model to disk
bool GamingAI::SaveAIModel(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::SaveAIModel() called - saving model to: %S", filename.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot save AI model - system not initialized");
        return false;                                                   // System not initialized
    }

    // Use specified filename or default
    std::string saveFilename = filename.empty() ? GetDefaultModelFilename() : filename;

    // Use ThreadLockHelper for thread-safe model saving
    ThreadLockHelper modelLock(threadManager, "gamingai_model_save", 10000); // Extended timeout for save operations
    if (!modelLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire model lock for saving - operation aborted");
        return false;                                                   // Failed to acquire lock
    }

    try {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Starting AI model save operation to: %S", saveFilename.c_str());
#endif

        // Save model using platform-appropriate method
        bool saveResult = SaveModelToDisk(saveFilename);

        if (saveResult) {
            // Update stored filename for future operations
            m_modelFilename = saveFilename;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"AI model saved successfully - File: %S, Size: %zu bytes",
                saveFilename.c_str(), m_currentModelSize.load());
#endif
        }
        else {
            debug.logDiagMessage(LogLevel::LOG_ERROR, L"Failed to save AI model to: %S", saveFilename.c_str());
        }

        return saveResult;                                              // Return save operation result
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception saving AI model: %S", e.what());
        return false;                                                   // Failed to save model
    }
}

// Load AI model from disk
bool GamingAI::LoadAIModel(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::LoadAIModel() called - loading model from: %S", filename.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Cannot load AI model - system not initialized");
        return false;                                                   // System not initialized
    }

    // Use specified filename or default
    std::string loadFilename = filename.empty() ? GetDefaultModelFilename() : filename;

    // Check if model file exists before attempting load
    if (!ModelFileExists(loadFilename)) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"AI model file does not exist: %S", loadFilename.c_str());
        return false;                                                   // File doesn't exist
    }

    // Use ThreadLockHelper for thread-safe model loading
    ThreadLockHelper modelLock(threadManager, "gamingai_model_load", 10000); // Extended timeout for load operations
    if (!modelLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire model lock for loading - operation aborted");
        return false;                                                   // Failed to acquire lock
    }

    try {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Starting AI model load operation from: %S", loadFilename.c_str());
#endif

        // Load model using platform-appropriate method
        bool loadResult = LoadModelFromDisk(loadFilename);

        if (loadResult) {
            // Update stored filename for future operations
            m_modelFilename = loadFilename;

            // Validate loaded model data
            if (ValidateModelData()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO,
                    L"AI model loaded and validated successfully - File: %S, Size: %zu bytes",
                    loadFilename.c_str(), m_currentModelSize.load());
#endif
            }
            else {
                debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Loaded AI model failed validation - using with caution");
            }
        }
        else {
            debug.logDiagMessage(LogLevel::LOG_ERROR, L"Failed to load AI model from: %S", loadFilename.c_str());
        }

        return loadResult;                                              // Return load operation result
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception loading AI model: %S", e.what());
        return false;                                                   // Failed to load model
    }
}

// Check if AI model file exists
bool GamingAI::ModelFileExists(const std::string& filename) const {
    // Use specified filename or default
    std::string checkFilename = filename.empty() ? GetDefaultModelFilename() : filename;

    try {
        // Platform-specific file existence check
#if defined(PLATFORM_WINDOWS)
    // Windows file existence check
        DWORD fileAttributes = GetFileAttributesA(checkFilename.c_str());
        bool exists = (fileAttributes != INVALID_FILE_ATTRIBUTES &&
            !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS) || defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
    // POSIX file existence check
        struct stat fileStat;
        bool exists = (stat(checkFilename.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode));
#else
    // Fallback using standard C++ method
        std::ifstream file(checkFilename);
        bool exists = file.good();
        file.close();
#endif

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Model file existence check - File: %S, Exists: %s",
            checkFilename.c_str(), exists ? L"Yes" : L"No");
#endif

        return exists;                                                  // Return file existence status
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception checking model file existence: %S", e.what());
        return false;                                                   // Assume file doesn't exist on error
    }
}

// Clear all learned AI data and reset to defaults
void GamingAI::ResetAIModel() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ResetAIModel() called - resetting AI model to defaults");
#endif

    // Use ThreadLockHelper for thread-safe model reset
    ThreadLockHelper modelLock(threadManager, "gamingai_model_reset", 5000);
    if (!modelLock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire model lock for reset - operation aborted");
        return;                                                         // Failed to acquire lock
    }

    try {
        // Clear all player analysis data
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_data_reset", 2000);
            if (dataLock.IsLocked()) {
                m_playerAnalysisData.clear();                           // Clear all player data

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Player analysis data cleared");
#endif
            }
        }

        // Reset current analysis results
        m_currentAnalysisResult = AIAnalysisResult();                   // Reset to default state
        m_currentStrategy = EnemyAIStrategy();                         // Reset strategy to defaults

        // Clear AI model data
        m_aiModelData.clear();                                          // Clear model data vector
        m_aiModelData.shrink_to_fit();                                  // Free allocated memory
        m_currentModelSize.store(0);                                   // Reset model size counter

        // Clear session tracking data
        m_sessionPlayerPositions.clear();                              // Clear position history
        m_sessionInputEvents.clear();                                  // Clear input history
        m_analysisTimings.clear();                                     // Clear timing history

        // Reserve memory for new data collection
        m_sessionPlayerPositions.reserve(10000);                       // Reserve space for positions
        m_sessionInputEvents.reserve(5000);                            // Reserve space for inputs
        m_analysisTimings.reserve(1000);                               // Reserve space for timings

        // Reset performance counters
        m_totalAnalysisCount.store(0);                                 // Reset analysis counter
        m_commandsProcessed.store(0);                                  // Reset command counter
        m_analysisReady.store(false);                                  // Mark analysis as not ready

        // Reset timing references
        m_performanceStartTime = std::chrono::steady_clock::now();     // Reset performance start time
        m_lastAnalysisTime = std::chrono::steady_clock::now();         // Reset last analysis time

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"AI model reset completed successfully - all data cleared");
#endif
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception resetting AI model: %S", e.what());
    }
}

//==============================================================================
// Private Model Persistence Implementation Methods
//==============================================================================

// Load AI model from disk storage (platform-specific implementation)
bool GamingAI::LoadModelFromDisk(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Loading AI model from disk: %S", filename.c_str());
#endif

    try {
        // Open model file for binary reading
        std::ifstream modelFile(filename, std::ios::binary | std::ios::ate);
        if (!modelFile.is_open()) {
            debug.logDiagMessage(LogLevel::LOG_ERROR, L"Failed to open model file for reading: %S", filename.c_str());
            return false;                                               // Failed to open file
        }

        // Get file size
        std::streamsize fileSize = modelFile.tellg();
        modelFile.seekg(0, std::ios::beg);

        // Validate file size
        if (fileSize <= 0 || static_cast<size_t>(fileSize) > m_configuration.maxModelSizeBytes) {
            debug.logDiagMessage(LogLevel::LOG_ERROR,
                L"Invalid model file size: %lld bytes (max: %zu bytes)",
                static_cast<long long>(fileSize), m_configuration.maxModelSizeBytes);
            modelFile.close();
            return false;                                               // Invalid file size
        }

        // Read model header for validation
        struct ModelHeader {
            uint32_t magic;                                             // Magic number for validation
            uint32_t version;                                           // Model format version
            uint64_t dataSize;                                          // Size of model data
            uint32_t checksum;                                          // CRC32 checksum for integrity
            uint32_t compressionType;                                   // Compression method used
            char reserved[32];                                          // Reserved for future use
        } header;

        modelFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!modelFile.good()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to read model header");
            modelFile.close();
            return false;                                               // Failed to read header
        }

        // Validate magic number
        const uint32_t EXPECTED_MAGIC = 0x41494D4F;                    // "AIMO" in hex
        if (header.magic != EXPECTED_MAGIC) {
            debug.logDiagMessage(LogLevel::LOG_ERROR,
                L"Invalid model file magic number: 0x%08X (expected: 0x%08X)",
                header.magic, EXPECTED_MAGIC);
            modelFile.close();
            return false;                                               // Invalid magic number
        }

        // Validate version
        const uint32_t SUPPORTED_VERSION = 1;
        if (header.version > SUPPORTED_VERSION) {
            debug.logDiagMessage(LogLevel::LOG_ERROR,
                L"Unsupported model version: %u (max supported: %u)",
                header.version, SUPPORTED_VERSION);
            modelFile.close();
            return false;                                               // Unsupported version
        }

        // Validate data size
        if (header.dataSize != static_cast<uint64_t>(fileSize - sizeof(header))) {
            debug.logDiagMessage(LogLevel::LOG_ERROR,
                L"Model data size mismatch: header says %llu, file has %lld",
                header.dataSize, static_cast<long long>(fileSize - sizeof(header)));
            modelFile.close();
            return false;                                               // Size mismatch
        }

        // Read model data
        m_aiModelData.clear();
        m_aiModelData.resize(static_cast<size_t>(header.dataSize));

        modelFile.read(reinterpret_cast<char*>(m_aiModelData.data()), static_cast<std::streamsize>(header.dataSize));
        if (!modelFile.good()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to read model data");
            modelFile.close();
            m_aiModelData.clear();
            return false;                                               // Failed to read data
        }

        modelFile.close();

        // Verify checksum using MathPrecalculation for fast CRC32
        uint32_t calculatedChecksum = FAST_MATH.FastFNV1aHash(m_aiModelData.data(), m_aiModelData.size());
        if (calculatedChecksum != header.checksum) {
            debug.logDiagMessage(LogLevel::LOG_WARNING,
                L"Model checksum mismatch: calculated 0x%08X, expected 0x%08X",
                calculatedChecksum, header.checksum);
            // Continue loading despite checksum mismatch (with warning)
        }

        // Decompress model data if needed
        if (header.compressionType != 0) {
            if (!DecompressModelData()) {
                debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to decompress model data");
                m_aiModelData.clear();
                return false;                                           // Failed to decompress
            }
        }

        // Deserialize model data into analysis structures
        if (!DeserializeModelData()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to deserialize model data");
            m_aiModelData.clear();
            return false;                                               // Failed to deserialize
        }

        // Update model size tracking
        m_currentModelSize.store(m_aiModelData.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"AI model loaded successfully - Size: %zu bytes, Version: %u, Players: %zu",
            m_aiModelData.size(), header.version, m_playerAnalysisData.size());
#endif

        return true;                                                    // Model loaded successfully
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception loading model from disk: %S", e.what());
        m_aiModelData.clear();                                          // Clear data on error
        return false;                                                   // Failed to load model
    }
}

// Save AI model to disk storage (platform-specific implementation)
bool GamingAI::SaveModelToDisk(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Saving AI model to disk: %S", filename.c_str());
#endif

    try {
        // Serialize current analysis data into model format
        if (!SerializeModelData()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to serialize model data for saving");
            return false;                                               // Failed to serialize
        }

        // Compress model data if enabled and beneficial
        bool compressionUsed = false;
        if (m_aiModelData.size() > 1024 * 1024) {                      // Compress if larger than 1MB
            size_t originalSize = m_aiModelData.size();
            if (CompressModelData()) {
                compressionUsed = true;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"Model data compressed - Original: %zu bytes, Compressed: %zu bytes",
                    originalSize, m_aiModelData.size());
#endif
            }
        }

        // Create model header
        struct ModelHeader {
            uint32_t magic;                                             // Magic number for validation
            uint32_t version;                                           // Model format version
            uint64_t dataSize;                                          // Size of model data
            uint32_t checksum;                                          // CRC32 checksum for integrity
            uint32_t compressionType;                                   // Compression method used
            char reserved[32];                                          // Reserved for future use
        } header;

        // Initialize header
        header.magic = 0x41494D4F;                                     // "AIMO" magic number
        header.version = 1;                                             // Current model version
        header.dataSize = static_cast<uint64_t>(m_aiModelData.size());
        header.checksum = FAST_MATH.FastFNV1aHash(m_aiModelData.data(), m_aiModelData.size()); // Calculate checksum
        header.compressionType = compressionUsed ? 1 : 0;              // Compression type indicator
        memset(header.reserved, 0, sizeof(header.reserved));           // Clear reserved space

        // Open file for binary writing
        std::ofstream modelFile(filename, std::ios::binary | std::ios::trunc);
        if (!modelFile.is_open()) {
            debug.logDiagMessage(LogLevel::LOG_ERROR, L"Failed to open model file for writing: %S", filename.c_str());
            return false;                                               // Failed to open file
        }

        // Write model header
        modelFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!modelFile.good()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to write model header");
            modelFile.close();
            return false;                                               // Failed to write header
        }

        // Write model data
        modelFile.write(reinterpret_cast<const char*>(m_aiModelData.data()),
            static_cast<std::streamsize>(m_aiModelData.size()));
        if (!modelFile.good()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to write model data");
            modelFile.close();
            return false;                                               // Failed to write data
        }

        modelFile.close();

        // Update model size tracking
        m_currentModelSize.store(m_aiModelData.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"AI model saved successfully - File: %S, Size: %zu bytes, Compressed: %s",
            filename.c_str(), m_aiModelData.size(), compressionUsed ? L"Yes" : L"No");
#endif

        return true;                                                    // Model saved successfully
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception saving model to disk: %S", e.what());
        return false;                                                   // Failed to save model
    }
}

// Get default AI model filename for current platform
std::string GamingAI::GetDefaultModelFilename() const {
    try {
        // Platform-specific default paths
#if defined(PLATFORM_WINDOWS)
    // Windows: Use Documents folder or current directory
        std::string defaultPath = "GamingAI_Model.dat";
#elif defined(PLATFORM_LINUX)
    // Linux: Use home directory or /tmp
        std::string defaultPath = "./GamingAI_Model.dat";
#elif defined(PLATFORM_MACOS)
    // macOS: Use Application Support directory
        std::string defaultPath = "./GamingAI_Model.dat";
#elif defined(PLATFORM_ANDROID)
    // Android: Use internal storage
        std::string defaultPath = "/data/data/gamingai/GamingAI_Model.dat";
#elif defined(PLATFORM_IOS)
    // iOS: Use Documents directory
        std::string defaultPath = "./GamingAI_Model.dat";
#else
    // Generic fallback
        std::string defaultPath = "GamingAI_Model.dat";
#endif

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Default model filename: %S", defaultPath.c_str());
#endif

        return defaultPath;                                             // Return platform-specific path
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception getting default model filename: %S", e.what());
        return "GamingAI_Model.dat";                                    // Return fallback filename
    }
}

//==============================================================================
// Main AI Thread Implementation (AIThreadTasking)
//==============================================================================

// Main AI thread processing function
void GamingAI::AIThreadTasking() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"AI thread started - beginning AI processing loop");
#endif

    // Set thread-specific initialization
    try {
        // Initialize thread-local performance monitoring
        auto threadStartTime = std::chrono::steady_clock::now();
        uint64_t totalCommandsProcessed = 0;
        uint64_t totalAnalysisOperations = 0;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"AI thread initialization completed - entering main processing loop");
#endif

        // Main AI processing loop - continues until shutdown requested
        try { m_gameHistory->Load(); } catch (...) { m_gameHistory->healthy.store(false); }
        while (!m_shouldShutdown.load()) {
            try {
                // Record loop iteration start time for performance monitoring
                auto loopStartTime = std::chrono::steady_clock::now();
                bool processedCommands = false;
                try { m_gameHistory->Pump(); }
                catch (...) {
                    if (m_gameHistory->healthy.exchange(false)) debug.logLevelMessage(LogLevel::LOG_ERROR, L"[CPGE-AI-LISTENER] History IO failed; batch retained for retry.");
                    // Replay the whole unflushed batch; event IDs suppress repeated side effects.
                    m_gameHistory->retryOffset=0;
                    m_gameHistory->journal.close(); m_gameHistory->journalHour=-1;
                }

                // A bounded command batch cannot starve session recording. Dispatch
                // outside the queue mutex because commands may enqueue more work.
                for (int budget=0; budget<64 && !m_shouldShutdown.load(); ++budget)
                {
                    AICommand currentCommand;
                    {
                        std::lock_guard<std::mutex> lock(m_commandQueueMutex);
                        if (m_commandQueue.empty()) break;
                        currentCommand=m_commandQueue.top(); m_commandQueue.pop();
                    }
                    ProcessAICommand(currentCommand); ++totalCommandsProcessed; processedCommands=true;
                    if (currentCommand.commandType==AICommandType::CMD_EMERGENCY_SHUTDOWN) { m_shouldShutdown.store(true); break; }
                }

                // Perform periodic analysis if interval has elapsed
                auto currentTime = std::chrono::steady_clock::now();
                auto timeSinceLastAnalysis = std::chrono::duration_cast<std::chrono::seconds>(
                    currentTime - m_lastAnalysisTime);

                if (timeSinceLastAnalysis.count() >= static_cast<long>(m_configuration.analysisIntervalSeconds)) {
                    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"Analysis interval elapsed (%lld seconds) - performing periodic analysis",
                            timeSinceLastAnalysis.count());
                    #endif

                    // Perform comprehensive periodic analysis
                    PerformPeriodicAnalysis();
                    totalAnalysisOperations++;

                    // Update last analysis time
                    m_lastAnalysisTime = currentTime;
                }

                // Calculate and log thread performance metrics periodically
                if (totalCommandsProcessed % 100 == 0 && totalCommandsProcessed > 0) {
                    auto threadRunTime = std::chrono::duration_cast<std::chrono::seconds>(
                        currentTime - threadStartTime);

                    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"AI thread performance - Runtime: %lld sec, Commands: %llu, Analysis ops: %llu",
                            threadRunTime.count(), totalCommandsProcessed, totalAnalysisOperations);
                    #endif
                }

                // Sleep briefly if no work was done to prevent CPU spinning
                if (!processedCommands && timeSinceLastAnalysis.count() < static_cast<long>(m_configuration.analysisIntervalSeconds)) {
                    // Use condition variable to wait for commands or timeout
                    std::unique_lock<std::mutex> lock(m_commandQueueMutex);
                    auto sleepDuration = std::chrono::milliseconds(500); // 500ms sleep when idle

                    m_commandAvailableCV.wait_for(lock, sleepDuration, [this] {
                        return !m_commandQueue.empty() || m_shouldShutdown.load();
                        });
                }

            }
            catch (const std::exception& e) {
                debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception in AI thread main loop: %S", e.what());

                // Continue operation unless it's a critical error
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Brief pause before retry
            }
        }

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"AI thread shutdown completed - Processed %llu commands, %llu analysis operations",
                totalCommandsProcessed, totalAnalysisOperations);
        #endif

    }
    catch (const std::exception& e) 
    {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Fatal exception in AI thread: %S", e.what());
    }

    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"AI thread terminated");
    #endif
}

// Perform periodic analysis operations
void GamingAI::PerformPeriodicAnalysis() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Performing periodic AI analysis");
    #endif

    try {
        // Record analysis start time for performance measurement
        auto analysisStartTime = std::chrono::steady_clock::now();

        // Only perform analysis if we have active players and are monitoring
        if (m_isMonitoring.load()) {
            std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();

            if (!activePlayerIDs.empty()) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Performing analysis for %zu active players", activePlayerIDs.size());
                #endif

                // Analyze each active player
                for (int playerID : activePlayerIDs) {
                    uint32_t playerId = static_cast<uint32_t>(playerID);

                    // Perform comprehensive player analysis
                    AnalyzePlayerMovement(playerId);
                    AnalyzePlayerCombat(playerId);
                    AnalyzePlayerInput(playerId);
                }

                // Generate strategic recommendations based on analysis
                GenerateEnemyStrategy();
                UpdateDifficultyRecommendations();

                // Periodically clean outdated data to manage memory usage
                static uint32_t cleanupCounter = 0;
                cleanupCounter++;
                if (cleanupCounter % 10 == 0) {                         // Clean every 10th analysis cycle
                    ClearOutdatedData();
                }

                // Mark analysis as ready for external consumption
                m_analysisReady.store(true);

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Periodic analysis completed successfully");
                #endif
            }
            else {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"No active players for analysis");
                #endif
            }
        }
        else {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Not monitoring - skipping periodic analysis");
            #endif
        }

        // Record analysis timing for performance monitoring
        auto analysisEndTime = std::chrono::steady_clock::now();
        auto analysisDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            analysisEndTime - analysisStartTime);

        // Store timing data with thread safety
        {
            ThreadLockHelper timingLock(threadManager, "gamingai_periodic_timing", 100, true);
            if (timingLock.IsLocked()) {
                m_analysisTimings.push_back(analysisDuration);

                // Maintain reasonable timing history size
                if (m_analysisTimings.size() > 1000) {
                    m_analysisTimings.erase(m_analysisTimings.begin(), m_analysisTimings.begin() + 200);
                }
            }
        }

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Periodic analysis timing: %lld ms", analysisDuration.count());
        #endif

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception in periodic analysis: %S", e.what());
    }
}

//==============================================================================
// Core Analysis Methods Implementation
//==============================================================================

// Analyze player movement patterns for specific player
void GamingAI::AnalyzePlayerMovement(uint32_t playerID) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Analyzing movement patterns for player %u", playerID);
    #endif

    try {
        // Get player information from GamePlayer system
        const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(static_cast<int>(playerID));
        if (playerInfo == nullptr) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u not found for movement analysis", playerID);
            return;                                                     // Player not found
        }

        // Use ThreadLockHelper for thread-safe analysis data access
        ThreadLockHelper analysisLock(threadManager, "gamingai_movement_analysis", 2000);
        if (!analysisLock.IsLocked()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for movement analysis");
            return;                                                     // Failed to acquire lock
        }

        // Get or create player analysis data entry
        PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];
        PlayerMovementPattern& movementData = playerData.movementData;

        // Calculate current velocity using MathPrecalculation for precision
        Vector2 currentPosition = playerInfo->position2D;
        Vector2 currentVelocity = playerInfo->velocity2D;

        // Update movement statistics if we have sufficient position history
        if (movementData.recentPositions.size() >= 2) {
            // Calculate average velocity over recent positions
            Vector2 totalVelocity(0.0f, 0.0f);
            uint32_t validSamples = 0;

            for (size_t i = 1; i < movementData.recentPositions.size(); ++i) {
                Vector2 deltaPos = Vector2(
                    movementData.recentPositions[i].x - movementData.recentPositions[i - 1].x,
                    movementData.recentPositions[i].y - movementData.recentPositions[i - 1].y
                );

                // Only include reasonable velocity changes (filter out teleports/glitches)
                float deltaDistance = FAST_MATH.FastSqrt(deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y);
                if (deltaDistance < 100.0f) {                           // Reasonable movement distance
                    totalVelocity.x += deltaPos.x;
                    totalVelocity.y += deltaPos.y;
                    validSamples++;
                }
            }

            if (validSamples > 0) {
                // Calculate average velocity using MathPrecalculation
                movementData.averageVelocity.x = totalVelocity.x / static_cast<float>(validSamples);
                movementData.averageVelocity.y = totalVelocity.y / static_cast<float>(validSamples);

                // Calculate movement predictability based on velocity consistency
                float velocityVariance = 0.0f;
                uint32_t varianceSamples = 0;

                for (size_t i = 1; i < movementData.recentPositions.size(); ++i) {
                    Vector2 deltaPos = Vector2(
                        movementData.recentPositions[i].x - movementData.recentPositions[i - 1].x,
                        movementData.recentPositions[i].y - movementData.recentPositions[i - 1].y
                    );

                    float deltaDistance = FAST_MATH.FastSqrt(deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y);
                    if (deltaDistance < 100.0f) {
                        Vector2 velocityDiff = Vector2(
                            deltaPos.x - movementData.averageVelocity.x,
                            deltaPos.y - movementData.averageVelocity.y
                        );

                        velocityVariance += velocityDiff.x * velocityDiff.x + velocityDiff.y * velocityDiff.y;
                        varianceSamples++;
                    }
                }

                if (varianceSamples > 0) {
                    velocityVariance /= static_cast<float>(varianceSamples);
                    // Convert variance to predictability (lower variance = higher predictability)
                    movementData.movementPredictability = 1.0f / (1.0f + velocityVariance * 0.1f);
                    movementData.movementPredictability = std::clamp(movementData.movementPredictability, 0.0f, 1.0f);
                }

                // Determine preferred movement direction using mathematical analysis
                if (FAST_MATH.FastSqrt(movementData.averageVelocity.x * movementData.averageVelocity.x +
                    movementData.averageVelocity.y * movementData.averageVelocity.y) > 0.1f) {
                    // Normalize preferred direction using MathPrecalculation
                    float velocityMagnitude = FAST_MATH.FastSqrt(
                        movementData.averageVelocity.x * movementData.averageVelocity.x +
                        movementData.averageVelocity.y * movementData.averageVelocity.y
                    );

                    if (velocityMagnitude > 0.0f) {
                        movementData.preferredDirection.x = movementData.averageVelocity.x / velocityMagnitude;
                        movementData.preferredDirection.y = movementData.averageVelocity.y / velocityMagnitude;
                    }
                }

                // Calculate aggressiveness factor based on movement speed and direction changes
                float averageSpeed = FAST_MATH.FastSqrt(
                    movementData.averageVelocity.x * movementData.averageVelocity.x +
                    movementData.averageVelocity.y * movementData.averageVelocity.y
                );

                // Higher speed and lower predictability indicate more aggressive movement
                movementData.aggressivenessFactor = (averageSpeed * 0.1f) + ((1.0f - movementData.movementPredictability) * 0.5f);
                movementData.aggressivenessFactor = std::clamp(movementData.aggressivenessFactor, 0.0f, 1.0f);
            }
        }

        // Update movement sample count
        movementData.totalMovementSamples++;

        // Estimate reaction time based on movement response to threats
        // This would typically involve analyzing response to enemy actions
        // For now, use a baseline calculation based on movement consistency
        if (movementData.movementPredictability > 0.8f) {
            movementData.reactionTime = 200.0f;                        // Fast, predictable movement
        }
        else if (movementData.movementPredictability > 0.5f) {
            movementData.reactionTime = 350.0f;                        // Average reaction time
        }
        else {
            movementData.reactionTime = 500.0f;                        // Slower, more erratic movement
        }

        // Update player skill level based on movement analysis
        playerData.skillLevel = CalculatePlayerSkillLevel(playerData);

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Movement analysis completed for player %u - Predictability: %.3f, Aggressiveness: %.3f, Skill: %u",
                playerID, movementData.movementPredictability, movementData.aggressivenessFactor, playerData.skillLevel);
        #endif

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception analyzing player %u movement: %S", playerID, e.what());
    }
}

// Analyze player combat behavior patterns
void GamingAI::AnalyzePlayerCombat(uint32_t playerID) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Analyzing combat patterns for player %u", playerID);
    #endif

    try {
        // Get player information from GamePlayer system
        const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(static_cast<int>(playerID));
        if (playerInfo == nullptr) {
            debug.logDiagMessage(LogLevel::LOG_WARNING, L"Player %u not found for combat analysis", playerID);
            return;                                                     // Player not found
        }

        // Use ThreadLockHelper for thread-safe analysis data access
        ThreadLockHelper analysisLock(threadManager, "gamingai_combat_analysis", 2000);
        if (!analysisLock.IsLocked()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for combat analysis");
            return;                                                     // Failed to acquire lock
        }

        // Get or create player analysis data entry
        PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];
        PlayerCombatPattern& combatData = playerData.combatData;

        // Analyze current player combat status
        bool isInCombat = (playerInfo->currentState == PlayerState::ACTIVE &&
            playerInfo->health < playerInfo->maxHealth);

        // Calculate accuracy based on hit/miss ratio (simulated for now)
        // In a real implementation, this would track actual shots fired vs hits
        float baseAccuracy = 0.5f;                                     // Base accuracy assumption

        // Adjust accuracy based on player skill level and movement patterns
        if (playerData.movementData.movementPredictability > 0.7f) {
            baseAccuracy += 0.2f;                                      // Steady aim bonus
        }

        if (playerData.movementData.aggressivenessFactor > 0.8f) {
            baseAccuracy -= 0.1f;                                      // Aggressive movement penalty
        }

        // Apply skill level modifier using MathPrecalculation for smooth scaling
        float skillModifier = static_cast<float>(playerData.skillLevel) / 100.0f;
        combatData.accuracyPercentage = FAST_MATH.FastLerp(0.2f, 0.9f, skillModifier);
        combatData.accuracyPercentage = std::clamp(combatData.accuracyPercentage, 0.0f, 1.0f);

        // Calculate preferred engagement range based on player behavior
        // This would typically be based on distance analysis during combat encounters
        if (playerData.movementData.aggressivenessFactor > 0.7f) {
            combatData.preferredEngagementRange = 5.0f;                // Close-range aggressive player
        }
        else if (playerData.movementData.movementPredictability > 0.7f) {
            combatData.preferredEngagementRange = 15.0f;               // Mid-range tactical player
        }
        else {
            combatData.preferredEngagementRange = 25.0f;               // Long-range cautious player
        }

        // Calculate combat aggression based on movement and health management
        float healthRatio = static_cast<float>(playerInfo->health) / static_cast<float>(playerInfo->maxHealth);
        combatData.combatAggression = (playerData.movementData.aggressivenessFactor * 0.7f) +
            ((1.0f - healthRatio) * 0.3f);    // More aggressive when injured
        combatData.combatAggression = std::clamp(combatData.combatAggression, 0.0f, 1.0f);

        // Determine preferred combat position based on movement patterns
        combatData.preferredCombatPosition = playerData.movementData.preferredDirection;

        // Update combat engagement statistics
        if (isInCombat) {
            combatData.totalCombatEngagements++;

            // Determine if engagement was successful (simplified)
            if (playerInfo->health > playerInfo->maxHealth * 0.5f) {
                combatData.successfulEngagements++;
            }
        }

        // Calculate average combat duration (simulated)
        // In real implementation, this would track actual combat encounter durations
        float skillBasedDuration = FAST_MATH.FastLerp(8000.0f, 3000.0f, skillModifier); // 3-8 seconds based on skill
        combatData.averageCombatDuration = std::chrono::milliseconds(static_cast<long long>(skillBasedDuration));

        // Weapon switch frequency analysis (based on equipment changes)
        // This would track actual weapon switching in a real implementation
        combatData.weaponSwitchFrequency = static_cast<uint32_t>(playerData.skillLevel / 20); // More skilled = more weapon switching

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Combat analysis completed for player %u - Accuracy: %.3f, Aggression: %.3f, Range: %.1f",
                playerID, combatData.accuracyPercentage, combatData.combatAggression, combatData.preferredEngagementRange);
        #endif

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception analyzing player %u combat: %S", playerID, e.what());
    }
}

// Analyze player input patterns and timing
void GamingAI::AnalyzePlayerInput(uint32_t playerID) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Analyzing input patterns for player %u", playerID);
    #endif

    try {
        // Use ThreadLockHelper for thread-safe analysis data access
        ThreadLockHelper analysisLock(threadManager, "gamingai_input_analysis", 2000);
        if (!analysisLock.IsLocked()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for input analysis");
            return;                                                     // Failed to acquire lock
        }

        // Get or create player analysis data entry
        PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];
        PlayerInputPattern& inputData = playerData.inputData;

        // Analyze session input events to calculate actions per minute
        if (!m_sessionInputEvents.empty()) {
            auto sessionDuration = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - m_sessionStartTime);

            if (sessionDuration.count() > 0) {
                // Count different types of input events
                uint32_t keyboardEvents = 0;
                uint32_t mouseEvents = 0;
                uint32_t joystickEvents = 0;

                // Analyze encoded input events
                for (uint32_t encodedInput : m_sessionInputEvents) {
                    uint32_t inputType = (encodedInput >> 16) & 0xFFFF;

                    switch (inputType) {
                    case INPUT_TYPE_KEYBOARD:
                        keyboardEvents++;
                        break;
                    case INPUT_TYPE_MOUSE:
                        mouseEvents++;
                        break;
                    case INPUT_TYPE_JOYSTICK:
                        joystickEvents++;
                        break;
                    }
                }

                // Calculate actions per minute
                float minutesElapsed = static_cast<float>(sessionDuration.count());
                inputData.keyboardActionsPerMinute = static_cast<uint32_t>(keyboardEvents / minutesElapsed);
                inputData.mouseActionsPerMinute = static_cast<uint32_t>(mouseEvents / minutesElapsed);
                inputData.joystickActionsPerMinute = static_cast<uint32_t>(joystickEvents / minutesElapsed);

                // Calculate input consistency based on timing variance
                // Higher APM with consistent timing indicates skilled input patterns
                if (keyboardEvents + mouseEvents + joystickEvents > 10) {
                    float totalAPM = static_cast<float>(keyboardEvents + mouseEvents + joystickEvents) / minutesElapsed;

                    // Normalize consistency based on APM (higher APM generally means more consistent input)
                    inputData.inputConsistency = std::min(totalAPM / 200.0f, 1.0f); // 200 APM = perfect consistency
                }
                else {
                    inputData.inputConsistency = 0.1f;                  // Low consistency for minimal input
                }

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Input analysis for player %u - KB APM: %u, Mouse APM: %u, Consistency: %.3f",
                        playerID, inputData.keyboardActionsPerMinute, inputData.mouseActionsPerMinute, inputData.inputConsistency);
                #endif
            }
        }

        // Analyze input latency based on response times
        // This would typically measure time between input and game response
        // For now, estimate based on input consistency and session performance
        if (inputData.inputConsistency > 0.8f) {
            inputData.inputLatency = 25.0f;                             // Low latency for consistent input
        }
        else if (inputData.inputConsistency > 0.5f) {
            inputData.inputLatency = 50.0f;                             // Average latency
        }
        else {
            inputData.inputLatency = 100.0f;                            // Higher latency for inconsistent input
        }

        // Update mouse movement pattern analysis
        // This would track actual mouse movement in a real implementation
        Vector2 estimatedMouseMovement = playerData.movementData.averageVelocity;
        inputData.mouseMovementPattern = Vector2(
            estimatedMouseMovement.x * 0.1f,                           // Scale movement to mouse sensitivity
            estimatedMouseMovement.y * 0.1f
        );

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception analyzing player %u input: %S", playerID, e.what());
    }
}

//==============================================================================
// Strategic Analysis Methods Implementation
//==============================================================================

// Generate strategic recommendations based on analysis
void GamingAI::GenerateEnemyStrategy() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Generating enemy AI strategy based on player analysis");
    #endif

    try {
        // Use ThreadLockHelper for thread-safe strategy generation
        ThreadLockHelper strategyLock(threadManager, "gamingai_strategy_gen", 3000);
        if (!strategyLock.IsLocked()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for strategy generation");
            return;                                                     // Failed to acquire lock
        }

        // Initialize strategy with current timestamp
        EnemyAIStrategy newStrategy;
        newStrategy.strategyTimestamp = std::chrono::steady_clock::now();

        // Calculate overall player metrics for strategy determination
        float averageSkillLevel = 0.0f;
        float averageAggressiveness = 0.0f;
        float averagePredictability = 0.0f;
        float averageAccuracy = 0.0f;
        uint32_t validPlayerCount = 0;

        // Aggregate player statistics for strategy calculation
        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;

            if (ValidateAnalysisData(playerData)) {
                averageSkillLevel += static_cast<float>(playerData.skillLevel);
                averageAggressiveness += playerData.movementData.aggressivenessFactor;
                averagePredictability += playerData.movementData.movementPredictability;
                averageAccuracy += playerData.combatData.accuracyPercentage;
                validPlayerCount++;
            }
        }

        if (validPlayerCount == 0) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"No valid player data for strategy generation - using defaults");

            // Use default strategy for no player data
            newStrategy.recommendedDifficulty = 0.5f;                  // Medium difficulty
            newStrategy.aggressionLevel = 0.5f;                       // Medium aggression
            newStrategy.tacticalIntelligence = 0.5f;                  // Medium intelligence
            newStrategy.recommendedEnemyCount = 3;                    // Default enemy count
        }
        else {
            // Calculate averages using MathPrecalculation for precision
            averageSkillLevel /= static_cast<float>(validPlayerCount);
            averageAggressiveness /= static_cast<float>(validPlayerCount);
            averagePredictability /= static_cast<float>(validPlayerCount);
            averageAccuracy /= static_cast<float>(validPlayerCount);

            // Normalize skill level to [0, 1] range
            float normalizedSkill = averageSkillLevel / 100.0f;

            // Calculate recommended difficulty using smooth curves
            // Higher skill = higher difficulty, but with diminishing returns
            newStrategy.recommendedDifficulty = FAST_MATH.FastSmoothStep(0.2f, 0.9f, normalizedSkill);

            // Adjust difficulty based on player behavior patterns
            if (averageAggressiveness > 0.8f) {
                // Very aggressive players can handle higher difficulty
                newStrategy.recommendedDifficulty = std::min(newStrategy.recommendedDifficulty + 0.1f, 1.0f);
            }
            else if (averagePredictability > 0.8f) {
                // Predictable players may need adaptive challenge
                newStrategy.recommendedDifficulty = FAST_MATH.FastLerp(newStrategy.recommendedDifficulty, 0.7f, 0.3f);
            }

            // Calculate AI aggression level to counter player behavior
            if (averageAggressiveness > 0.7f) {
                // Counter aggressive players with tactical AI
                newStrategy.aggressionLevel = FAST_MATH.FastLerp(0.3f, 0.6f, normalizedSkill);
            }
            else if (averagePredictability > 0.7f) {
                // Use unpredictable aggression against predictable players
                newStrategy.aggressionLevel = FAST_MATH.FastLerp(0.5f, 0.8f, normalizedSkill);
            }
            else {
                // Standard aggression scaling with skill
                newStrategy.aggressionLevel = FAST_MATH.FastLerp(0.4f, 0.7f, normalizedSkill);
            }

            // Calculate tactical intelligence based on player accuracy and skill
            float intelligenceBase = (normalizedSkill * 0.7f) + (averageAccuracy * 0.3f);
            newStrategy.tacticalIntelligence = FAST_MATH.FastSmoothStep(0.3f, 0.9f, intelligenceBase);

            // Determine optimal enemy count based on player performance
            if (averageSkillLevel > 80.0f && averageAccuracy > 0.8f) {
                newStrategy.recommendedEnemyCount = 6;                  // High skill = more enemies
            }
            else if (averageSkillLevel > 60.0f) {
                newStrategy.recommendedEnemyCount = 4;                  // Medium skill = moderate enemies
            }
            else if (averageSkillLevel > 30.0f) {
                newStrategy.recommendedEnemyCount = 3;                  // Low-medium skill = fewer enemies
            }
            else {
                newStrategy.recommendedEnemyCount = 2;                  // Low skill = minimal enemies
            }

            // Calculate optimal engagement range based on player combat preferences
            float totalPreferredRange = 0.0f;
            uint32_t rangeCount = 0;

            for (const auto& playerPair : m_playerAnalysisData) {
                const PlayerAnalysisData& playerData = playerPair.second;
                if (ValidateAnalysisData(playerData)) {
                    totalPreferredRange += playerData.combatData.preferredEngagementRange;
                    rangeCount++;
                }
            }

            if (rangeCount > 0) {
                float averagePlayerRange = totalPreferredRange / static_cast<float>(rangeCount);
                // AI should engage at range that challenges but doesn't overwhelm players
                newStrategy.engagementRange = averagePlayerRange * 1.2f; // 20% farther than player preference
            }
            else {
                newStrategy.engagementRange = 15.0f;                    // Default engagement range
            }
        }

        // Determine recommended positioning strategy
        if (averagePredictability > 0.7f) {
            // Counter predictable movement with flanking positions
            newStrategy.recommendedPositioning = Vector2(0.8f, 0.6f);  // Aggressive flanking
        }
        else if (averageAggressiveness > 0.7f) {
            // Counter aggressive players with defensive positions
            newStrategy.recommendedPositioning = Vector2(0.3f, 0.8f);  // Defensive positioning
        }
        else {
            // Balanced positioning for average players
            newStrategy.recommendedPositioning = Vector2(0.5f, 0.5f);  // Balanced positioning
        }

        // Generate tactical recommendations based on analysis
        newStrategy.recommendedTactics.clear();

        if (averageSkillLevel > 70.0f) {
            newStrategy.recommendedTactics.push_back("advanced_flanking");
            newStrategy.recommendedTactics.push_back("coordinated_attacks");
            newStrategy.recommendedTactics.push_back("predictive_movement");
        }
        else if (averageSkillLevel > 40.0f) {
            newStrategy.recommendedTactics.push_back("basic_flanking");
            newStrategy.recommendedTactics.push_back("pattern_variation");
            newStrategy.recommendedTactics.push_back("adaptive_timing");
        }
        else {
            newStrategy.recommendedTactics.push_back("direct_engagement");
            newStrategy.recommendedTactics.push_back("simple_patterns");
            newStrategy.recommendedTactics.push_back("fair_timing");
        }

        // Add behavioral counters based on player patterns
        if (averageAggressiveness > 0.8f) {
            newStrategy.recommendedTactics.push_back("defensive_counters");
            newStrategy.recommendedTactics.push_back("patience_tactics");
        }

        if (averagePredictability > 0.8f) {
            newStrategy.recommendedTactics.push_back("unpredictable_movement");
            newStrategy.recommendedTactics.push_back("pattern_breaking");
        }

        // Calculate prediction accuracy based on data quality and quantity
        float dataQuality = static_cast<float>(validPlayerCount) / 8.0f; // Assuming max 8 players
        uint64_t totalAnalysisOps = m_totalAnalysisCount.load();
        float experienceFactor = std::min(static_cast<float>(totalAnalysisOps) / 100.0f, 1.0f);

        newStrategy.predictionAccuracy = (dataQuality * 0.5f) + (experienceFactor * 0.5f);
        newStrategy.predictionAccuracy = std::clamp(newStrategy.predictionAccuracy, 0.3f, 0.95f);

        // Store the new strategy
        m_currentStrategy = newStrategy;

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Enemy strategy generated - Difficulty: %.3f, Aggression: %.3f, Intelligence: %.3f, Enemies: %u",
                newStrategy.recommendedDifficulty, newStrategy.aggressionLevel,
                newStrategy.tacticalIntelligence, newStrategy.recommendedEnemyCount);

            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Strategy details - Range: %.1f, Prediction accuracy: %.3f, Tactics: %zu",
                newStrategy.engagementRange, newStrategy.predictionAccuracy, newStrategy.recommendedTactics.size());
        #endif

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception generating enemy strategy: %S", e.what());
    }
}

// Update overall difficulty recommendations
void GamingAI::UpdateDifficultyRecommendations() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Updating difficulty recommendations based on player performance");
    #endif

    try {
        // Use ThreadLockHelper for thread-safe difficulty update
        ThreadLockHelper difficultyLock(threadManager, "gamingai_difficulty_update", 2000);
        if (!difficultyLock.IsLocked()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for difficulty update");
            return;                                                     // Failed to acquire lock
        }

        // Calculate adaptive difficulty based on current strategy and player performance
        float baseDifficulty = m_currentStrategy.recommendedDifficulty;

        // Adjust difficulty based on recent player performance trends
        float performanceTrend = CalculatePerformanceTrend();

        // Apply learning rate to difficulty adjustments for smooth transitions
        float adjustedDifficulty = FAST_MATH.FastLerp(baseDifficulty, baseDifficulty + performanceTrend,
            m_configuration.learningRate);

        // Clamp difficulty to reasonable bounds
        adjustedDifficulty = std::clamp(adjustedDifficulty, 0.1f, 0.95f);

        // Update current analysis result with new difficulty recommendation
        m_currentAnalysisResult.overallDifficultyRecommendation = adjustedDifficulty;

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Difficulty updated - Base: %.3f, Trend: %.3f, Final: %.3f",
                baseDifficulty, performanceTrend, adjustedDifficulty);
        #endif

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception updating difficulty: %S", e.what());
    }
}

// Clean up outdated analysis data
void GamingAI::ClearOutdatedData() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Clearing outdated AI analysis data");
    #endif

    try {
        // Use ThreadLockHelper for thread-safe data cleanup
        ThreadLockHelper cleanupLock(threadManager, "gamingai_data_cleanup", 5000);
        if (!cleanupLock.IsLocked()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for data cleanup");
            return;                                                     // Failed to acquire lock
        }

        auto currentTime = std::chrono::system_clock::now();
        auto retentionDuration = std::chrono::hours(static_cast<long>(m_configuration.dataRetentionDays * 24));

        size_t playersRemoved = 0;
        size_t entriesCleared = 0;

        // Remove outdated player analysis data
        for (auto it = m_playerAnalysisData.begin(); it != m_playerAnalysisData.end();) {
            const PlayerAnalysisData& playerData = it->second;
            auto dataAge = currentTime - playerData.lastAnalysisTime;

            if (dataAge > retentionDuration) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Removing outdated data for player %u (age: %lld hours)",
                        playerData.playerID, std::chrono::duration_cast<std::chrono::hours>(dataAge).count());
                #endif

                it = m_playerAnalysisData.erase(it);
                playersRemoved++;
            }
            else {
                // Clear excessive movement history to save memory
                PlayerAnalysisData& mutablePlayerData = it->second;
                if (mutablePlayerData.movementData.recentPositions.size() > m_configuration.maxPlayerHistoryEntries) {
                    size_t entriesToRemove = mutablePlayerData.movementData.recentPositions.size() -
                        (m_configuration.maxPlayerHistoryEntries / 2);

                    mutablePlayerData.movementData.recentPositions.erase(
                        mutablePlayerData.movementData.recentPositions.begin(),
                        mutablePlayerData.movementData.recentPositions.begin() + entriesToRemove);

                    entriesCleared += entriesToRemove;
                }
                ++it;
            }
        }

        // Trim analysis timing history to reasonable size
        if (m_analysisTimings.size() > 1000) {
            size_t timingsToRemove = m_analysisTimings.size() - 500;
            m_analysisTimings.erase(m_analysisTimings.begin(),
                m_analysisTimings.begin() + timingsToRemove);
            entriesCleared += timingsToRemove;
        }

        // Trim session data if it's getting too large
        if (m_sessionPlayerPositions.size() > 15000) {
            size_t positionsToRemove = m_sessionPlayerPositions.size() - 10000;
            m_sessionPlayerPositions.erase(m_sessionPlayerPositions.begin(),
                m_sessionPlayerPositions.begin() + positionsToRemove);
            entriesCleared += positionsToRemove;
        }

        if (m_sessionInputEvents.size() > 10000) {
            size_t eventsToRemove = m_sessionInputEvents.size() - 5000;
            m_sessionInputEvents.erase(m_sessionInputEvents.begin(),
                m_sessionInputEvents.begin() + eventsToRemove);
            entriesCleared += eventsToRemove;
        }

        // Update model size tracking
        size_t newModelSize = CalculateCurrentModelSize();
        m_currentModelSize.store(newModelSize);

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Data cleanup completed - Players removed: %zu, Entries cleared: %zu, New model size: %zu bytes",
                playersRemoved, entriesCleared, newModelSize);
        #endif

    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception clearing outdated data: %S", e.what());
    }
}

//==============================================================================
// Utility and Helper Methods Implementation
//==============================================================================

// Calculate player skill level based on performance metrics
uint32_t GamingAI::CalculatePlayerSkillLevel(const PlayerAnalysisData& playerData) const {
    try {
        // Weight different aspects of player performance
        const float MOVEMENT_WEIGHT = 0.3f;
        const float COMBAT_WEIGHT = 0.4f;
        const float INPUT_WEIGHT = 0.3f;

        // Calculate movement skill component
        float movementSkill = 0.0f;
        if (playerData.movementData.totalMovementSamples > 0) {
            // Higher predictability and appropriate aggression indicate skill
            movementSkill = (playerData.movementData.movementPredictability * 0.4f) +
                (std::min(playerData.movementData.aggressivenessFactor * 1.5f, 1.0f) * 0.4f) +
                (std::max(0.0f, 1.0f - (playerData.movementData.reactionTime / 1000.0f)) * 0.2f);
        }

        // Calculate combat skill component
        float combatSkill = 0.0f;
        if (playerData.combatData.totalCombatEngagements > 0) {
            float successRate = static_cast<float>(playerData.combatData.successfulEngagements) /
                static_cast<float>(playerData.combatData.totalCombatEngagements);

            combatSkill = (playerData.combatData.accuracyPercentage * 0.5f) +
                (successRate * 0.3f) +
                (std::min(playerData.combatData.combatAggression * 1.2f, 1.0f) * 0.2f);
        }

        // Calculate input skill component
        float inputSkill = 0.0f;
        uint32_t totalAPM = playerData.inputData.keyboardActionsPerMinute +
            playerData.inputData.mouseActionsPerMinute +
            playerData.inputData.joystickActionsPerMinute;

        if (totalAPM > 0) {
            // Normalize APM to skill range (200 APM = max skill for input)
            float apmSkill = std::min(static_cast<float>(totalAPM) / 200.0f, 1.0f);
            inputSkill = (apmSkill * 0.6f) + (playerData.inputData.inputConsistency * 0.4f);
        }

        // Combine skill components using weights
        float overallSkill = (movementSkill * MOVEMENT_WEIGHT) +
            (combatSkill * COMBAT_WEIGHT) +
            (inputSkill * INPUT_WEIGHT);

        // Apply experience modifier based on sessions analyzed
        float experienceModifier = 1.0f + (std::min(static_cast<float>(playerData.sessionsAnalyzed), 20.0f) * 0.025f);
        overallSkill *= experienceModifier;

        // Convert to skill level (1-100) using smooth scaling
        uint32_t skillLevel = static_cast<uint32_t>(FAST_MATH.FastSmoothStep(1.0f, 100.0f, overallSkill));
        skillLevel = std::clamp(skillLevel, 1U, 100U);

        return skillLevel;
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception calculating skill level: %S", e.what());
        return 50;                                                      // Return average skill on error
    }
}

// Predict next player action based on historical patterns
Vector2 GamingAI::PredictPlayerNextAction(uint32_t playerID) const {
    try {
        auto playerIt = m_playerAnalysisData.find(playerID);
        if (playerIt == m_playerAnalysisData.end()) {
            return Vector2(0.0f, 0.0f);                                 // No data available
        }

        const PlayerAnalysisData& playerData = playerIt->second;
        const PlayerMovementPattern& movementData = playerData.movementData;

        // Use movement predictability to determine prediction confidence
        if (movementData.movementPredictability > 0.7f && movementData.recentPositions.size() >= 3) {
            // High predictability - use pattern extrapolation
            size_t posCount = movementData.recentPositions.size();
            Vector2 recentDirection = Vector2(
                movementData.recentPositions[posCount - 1].x - movementData.recentPositions[posCount - 2].x,
                movementData.recentPositions[posCount - 1].y - movementData.recentPositions[posCount - 2].y
            );

            // Apply predictability factor and preferred direction bias
            Vector2 predictedAction = Vector2(
                (recentDirection.x * movementData.movementPredictability) +
                (movementData.preferredDirection.x * (1.0f - movementData.movementPredictability)),
                (recentDirection.y * movementData.movementPredictability) +
                (movementData.preferredDirection.y * (1.0f - movementData.movementPredictability))
            );

            return predictedAction;
        }
        else if (movementData.totalMovementSamples > 10) {
            // Medium predictability - use average behavior
            return movementData.preferredDirection;
        }
        else {
            // Low predictability - return neutral prediction
            return Vector2(0.0f, 0.0f);
        }
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception predicting player action: %S", e.what());
        return Vector2(0.0f, 0.0f);                                     // Return neutral on error
    }
}

// Calculate adaptive difficulty adjustment
float GamingAI::CalculateAdaptiveDifficulty(const PlayerAnalysisData& playerData) const {
    try {
        // Base difficulty on player skill level
        float baseDifficulty = static_cast<float>(playerData.skillLevel) / 100.0f;

        // Adjust for player behavior patterns
        if (playerData.movementData.aggressivenessFactor > 0.8f) {
            baseDifficulty += 0.1f;                                     // Aggressive players can handle more
        }

        if (playerData.combatData.accuracyPercentage > 0.8f) {
            baseDifficulty += 0.15f;                                    // Accurate players need more challenge
        }

        if (playerData.inputData.inputConsistency > 0.8f) {
            baseDifficulty += 0.05f;                                    // Consistent input indicates skill
        }

        // Adjust based on recent performance (if available)
        if (playerData.combatData.totalCombatEngagements > 5) {
            float recentSuccessRate = static_cast<float>(playerData.combatData.successfulEngagements) /
                static_cast<float>(playerData.combatData.totalCombatEngagements);

            if (recentSuccessRate > 0.8f) {
                baseDifficulty += 0.1f;                                 // High success rate = increase difficulty
            }
            else if (recentSuccessRate < 0.3f) {
                baseDifficulty -= 0.1f;                                 // Low success rate = decrease difficulty
            }
        }

        // Apply learning rate for smooth transitions
        return std::clamp(baseDifficulty, 0.1f, 0.95f);
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception calculating adaptive difficulty: %S", e.what());
        return 0.5f;                                                    // Return medium difficulty on error
    }
}

//==============================================================================
// Performance and Statistics Helper Methods
//==============================================================================

// Calculate performance trend for difficulty adjustment
float GamingAI::CalculatePerformanceTrend() const {
    try {
        // Calculate average performance change over recent sessions
        float totalTrend = 0.0f;
        uint32_t trendSamples = 0;

        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;

            if (playerData.sessionsAnalyzed >= 3) {
                // Estimate performance trend based on combat success and skill
                float currentPerformance = static_cast<float>(playerData.skillLevel) / 100.0f;

                if (playerData.combatData.totalCombatEngagements > 0) {
                    float successRate = static_cast<float>(playerData.combatData.successfulEngagements) /
                        static_cast<float>(playerData.combatData.totalCombatEngagements);
                    currentPerformance = (currentPerformance + successRate) * 0.5f;
                }

                // Simple trend calculation (would be more sophisticated with historical data)
                float trend = (currentPerformance - 0.5f) * 0.2f;      // Scale trend impact
                totalTrend += trend;
                trendSamples++;
            }
        }

        if (trendSamples > 0) {
            return totalTrend / static_cast<float>(trendSamples);
        }

        return 0.0f;                                                    // No trend data available
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception calculating performance trend: %S", e.what());
        return 0.0f;                                                    // Return neutral trend on error
    }
}

// Calculate current model size for memory management
size_t GamingAI::CalculateCurrentModelSize() const {
    try {
        size_t totalSize = 0;

        // Calculate size of player analysis data
        totalSize += m_playerAnalysisData.size() * sizeof(PlayerAnalysisData);

        // Add size of movement history for each player
        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;
            totalSize += playerData.movementData.recentPositions.size() * sizeof(Vector2);
        }

        // Add size of session data
        totalSize += m_sessionPlayerPositions.size() * sizeof(Vector2);
        totalSize += m_sessionInputEvents.size() * sizeof(uint32_t);
        totalSize += m_analysisTimings.size() * sizeof(std::chrono::milliseconds);

        // Add size of AI model data
        totalSize += m_aiModelData.size();

        // Add overhead estimates for containers and structures
        totalSize += m_playerAnalysisData.size() * 64;                  // Container overhead estimate

        return totalSize;
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception calculating model size: %S", e.what());
        return m_currentModelSize.load();                               // Return last known size on error
    }
}

//==============================================================================
// Model Data Serialization Methods (Implementation Stubs)
//==============================================================================

// Serialize current analysis data into model format
bool GamingAI::SerializeModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Serializing AI model data for storage");
#endif

    try {
        // Clear existing model data
        m_aiModelData.clear();

        // Reserve space for serialized data
        m_aiModelData.reserve(1024 * 1024);                             // Reserve 1MB initially

        // Serialize player analysis data (simplified binary format)
        uint32_t playerCount = static_cast<uint32_t>(m_playerAnalysisData.size());

        // Write player count
        m_aiModelData.insert(m_aiModelData.end(),
            reinterpret_cast<uint8_t*>(&playerCount),
            reinterpret_cast<uint8_t*>(&playerCount) + sizeof(playerCount));

        // Serialize each player's data
        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;

            // Serialize basic player info (simplified)
            m_aiModelData.insert(m_aiModelData.end(),
                reinterpret_cast<const uint8_t*>(&playerData),
                reinterpret_cast<const uint8_t*>(&playerData) + sizeof(PlayerAnalysisData));
        }

        // Update model size
        m_currentModelSize.store(m_aiModelData.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Model data serialized - Size: %zu bytes, Players: %u",
            m_aiModelData.size(), playerCount);
#endif

        return true;
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception serializing model data: %S", e.what());
        m_aiModelData.clear();                                          // Clear data on error
        return false;                                                   // Failed to serialize
    }
}

// Deserialize model data into analysis structures
bool GamingAI::DeserializeModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Deserializing AI model data from storage");
#endif

    try {
        if (m_aiModelData.empty()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"No model data available for deserialization");
            return false;                                               // No data to deserialize
        }

        // Clear existing analysis data
        m_playerAnalysisData.clear();

        // Read player count
        if (m_aiModelData.size() < sizeof(uint32_t)) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Model data too small for player count");
            return false;                                               // Insufficient data
        }

        uint32_t playerCount;
        memcpy(&playerCount, m_aiModelData.data(), sizeof(playerCount));
        size_t offset = sizeof(playerCount);

        // Validate player count
        if (playerCount > 100) {                                        // Reasonable limit
            debug.logDiagMessage(LogLevel::LOG_ERROR, L"Invalid player count in model data: %u", playerCount);
            return false;                                               // Invalid player count
        }

        // Deserialize each player's data
        for (uint32_t i = 0; i < playerCount; ++i) {
            if (offset + sizeof(PlayerAnalysisData) > m_aiModelData.size()) {
                debug.logDiagMessage(LogLevel::LOG_ERROR,
                    L"Insufficient data for player %u (offset: %zu, total: %zu)",
                    i, offset, m_aiModelData.size());
                return false;                                           // Insufficient data
            }

            PlayerAnalysisData playerData;
            memcpy(&playerData, m_aiModelData.data() + offset, sizeof(PlayerAnalysisData));
            offset += sizeof(PlayerAnalysisData);

            // Validate player data
            if (ValidateAnalysisData(playerData)) {
                m_playerAnalysisData[playerData.playerID] = playerData;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"Deserialized player %u data - Skill: %u, Sessions: %u",
                    playerData.playerID, playerData.skillLevel, playerData.sessionsAnalyzed);
#endif
            }
            else {
                debug.logDiagMessage(LogLevel::LOG_WARNING,
                    L"Player %u data failed validation - skipping", playerData.playerID);
            }
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Model data deserialized successfully - Players loaded: %zu", m_playerAnalysisData.size());
#endif

        return true;                                                    // Deserialization successful
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception deserializing model data: %S", e.what());
        m_playerAnalysisData.clear();                                   // Clear data on error
        return false;                                                   // Failed to deserialize
    }
}

// Validate AI model data integrity
bool GamingAI::ValidateModelData() const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Validating AI model data integrity");
#endif

    try {
        // Check if model data exists
        if (m_aiModelData.empty()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"No model data to validate");
            return false;                                               // No data to validate
        }

        // Check model size constraints
        if (m_aiModelData.size() > m_configuration.maxModelSizeBytes) {
            debug.logDiagMessage(LogLevel::LOG_ERROR,
                L"Model data exceeds size limit: %zu > %zu bytes",
                m_aiModelData.size(), m_configuration.maxModelSizeBytes);
            return false;                                               // Model too large
        }

        // Validate minimum data size
        if (m_aiModelData.size() < sizeof(uint32_t)) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Model data too small for basic structure");
            return false;                                               // Data too small
        }

        // Validate player analysis data
        size_t validPlayers = 0;
        size_t invalidPlayers = 0;

        for (const auto& playerPair : m_playerAnalysisData) {
            if (ValidateAnalysisData(playerPair.second)) {
                validPlayers++;
            }
            else {
                invalidPlayers++;
            }
        }

        // Require at least some valid data
        if (validPlayers == 0 && !m_playerAnalysisData.empty()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"No valid player analysis data found in model");
            return false;                                               // No valid data
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Model validation completed - Valid players: %zu, Invalid: %zu, Size: %zu bytes",
            validPlayers, invalidPlayers, m_aiModelData.size());
#endif

        return true;                                                    // Model validation passed
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception validating model data: %S", e.what());
        return false;                                                   // Failed validation
    }
}

// Compress model data for storage efficiency
bool GamingAI::CompressModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Compressing AI model data for storage");
#endif

    try {
        if (m_aiModelData.empty()) {
            return true;                                                // Nothing to compress
        }

        size_t originalSize = m_aiModelData.size();

        // Simple compression placeholder - in a real implementation, you would use
        // a compression library like zlib, lz4, or integrate with PUNPack if available

        // For now, we'll implement a basic run-length encoding for demonstration
        std::vector<uint8_t> compressedData;
        compressedData.reserve(originalSize);                           // Reserve space

        if (originalSize > 0) {
            uint8_t currentByte = m_aiModelData[0];
            uint8_t count = 1;

            for (size_t i = 1; i < originalSize; ++i) {
                if (m_aiModelData[i] == currentByte && count < 255) {
                    count++;
                }
                else {
                    // Write count and byte
                    compressedData.push_back(count);
                    compressedData.push_back(currentByte);

                    currentByte = m_aiModelData[i];
                    count = 1;
                }
            }

            // Write final count and byte
            compressedData.push_back(count);
            compressedData.push_back(currentByte);
        }

        // Only use compression if it actually reduces size
        if (compressedData.size() < originalSize) {
            m_aiModelData = std::move(compressedData);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Model data compressed - Original: %zu bytes, Compressed: %zu bytes (%.1f%% reduction)",
                originalSize, m_aiModelData.size(),
                ((float)(originalSize - m_aiModelData.size()) / (float)originalSize) * 100.0f);
#endif

            return true;                                                // Compression successful
        }
        else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Compression did not reduce size - keeping original data");
#endif

            return false;                                               // Compression not beneficial
        }
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception compressing model data: %S", e.what());
        return false;                                                   // Failed to compress
    }
}

// Decompress model data after loading
bool GamingAI::DecompressModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Decompressing AI model data after loading");
#endif

    try {
        if (m_aiModelData.empty()) {
            return true;                                                // Nothing to decompress
        }

        size_t compressedSize = m_aiModelData.size();

        // Decompress using simple run-length decoding (matching compression above)
        std::vector<uint8_t> decompressedData;
        decompressedData.reserve(compressedSize * 2);                   // Estimate decompressed size

        for (size_t i = 0; i < compressedSize; i += 2) {
            if (i + 1 < compressedSize) {
                uint8_t count = m_aiModelData[i];
                uint8_t byte = m_aiModelData[i + 1];

                // Add repeated bytes to decompressed data
                for (uint8_t j = 0; j < count; ++j) {
                    decompressedData.push_back(byte);
                }
            }
        }

        // Replace compressed data with decompressed data
        m_aiModelData = std::move(decompressedData);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Model data decompressed - Compressed: %zu bytes, Decompressed: %zu bytes",
            compressedSize, m_aiModelData.size());
#endif

        return true;                                                    // Decompression successful
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"Exception decompressing model data: %S", e.what());
        return false;                                                   // Failed to decompress
    }
}

//==============================================================================
// High-Performance Optimized Methods (Assembly when beneficial)
//==============================================================================

// Optimized vector distance calculation using SIMD when available
float GamingAI::FastVectorDistance(const Vector2& pos1, const Vector2& pos2) const {
    try {
        // Calculate distance using MathPrecalculation for optimal performance
        float dx = pos2.x - pos1.x;
        float dy = pos2.y - pos1.y;

        // Use fast square root from MathPrecalculation
        return FAST_MATH.FastSqrt(dx * dx + dy * dy);
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception in fast vector distance: %S", e.what());
        return 0.0f;                                                    // Return 0 on error
    }
}

// Optimized pattern matching algorithm
float GamingAI::FastPatternMatch(const std::vector<Vector2>& pattern1, const std::vector<Vector2>& pattern2) const {
    try {
        if (pattern1.empty() || pattern2.empty()) {
            return 0.0f;                                                // No patterns to match
        }

        size_t minSize = std::min(pattern1.size(), pattern2.size());
        if (minSize == 0) {
            return 0.0f;                                                // No valid comparison size
        }

        float totalSimilarity = 0.0f;

        // Compare corresponding points in both patterns
        for (size_t i = 0; i < minSize; ++i) {
            float distance = FastVectorDistance(pattern1[i], pattern2[i]);

            // Convert distance to similarity (closer = more similar)
            float similarity = 1.0f / (1.0f + distance);
            totalSimilarity += similarity;
        }

        // Return average similarity
        return totalSimilarity / static_cast<float>(minSize);
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception in fast pattern match: %S", e.what());
        return 0.0f;                                                    // Return 0 on error
    }
}

// Optimized data correlation analysis
float GamingAI::FastCorrelationAnalysis(const float* data1, const float* data2, size_t dataCount) const {
    try {
        if (data1 == nullptr || data2 == nullptr || dataCount == 0) {
            return 0.0f;                                                // Invalid input parameters
        }

        // Calculate means
        float mean1 = 0.0f, mean2 = 0.0f;
        for (size_t i = 0; i < dataCount; ++i) {
            mean1 += data1[i];
            mean2 += data2[i];
        }
        mean1 /= static_cast<float>(dataCount);
        mean2 /= static_cast<float>(dataCount);

        // Calculate correlation coefficient
        float numerator = 0.0f;
        float sum1Sq = 0.0f, sum2Sq = 0.0f;

        for (size_t i = 0; i < dataCount; ++i) {
            float diff1 = data1[i] - mean1;
            float diff2 = data2[i] - mean2;

            numerator += diff1 * diff2;
            sum1Sq += diff1 * diff1;
            sum2Sq += diff2 * diff2;
        }

        // Calculate correlation coefficient using MathPrecalculation
        float denominator = FAST_MATH.FastSqrt(sum1Sq * sum2Sq);

        if (denominator > 1e-8f) {
            return numerator / denominator;                             // Return correlation coefficient
        }
        else {
            return 0.0f;                                                // No correlation (zero variance)
        }
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"Exception in fast correlation analysis: %S", e.what());
        return 0.0f;                                                    // Return 0 on error
    }
}

//==============================================================================
// Assembly Optimized Methods (Platform-Specific)
//==============================================================================
//==============================================================================
// High-Performance Optimized Methods (Fixed for x64 Visual Studio)
//==============================================================================

#if defined(PLATFORM_WINDOWS) && defined(_M_X64)
// High-performance memory copy for large data sets using MSVC intrinsics
void GamingAI::FastMemoryCopy(void* dest, const void* src, size_t size) const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Performing fast memory copy - Size: %zu bytes", size);
#endif

    try {
        // Use optimized intrinsics for large copies (>1KB)
        if (size > 1024) {
            // Use MSVC intrinsics instead of inline assembly for x64 compatibility
#if defined(_MSC_VER)
    #include <intrin.h>
#endif
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <cmath>
#include <stdexcept>

// Ensure 16-byte alignment for optimal SIMD performance
            const size_t SIMD_ALIGNMENT = 16;
            uint8_t* destPtr = static_cast<uint8_t*>(dest);
            const uint8_t* srcPtr = static_cast<const uint8_t*>(src);

            // Handle unaligned start
            while (((uintptr_t)destPtr & (SIMD_ALIGNMENT - 1)) && size > 0) {
                *destPtr++ = *srcPtr++;                                // Copy byte by byte until aligned
                size--;
            }

            // Perform 16-byte aligned copies using SSE2 intrinsics
            while (size >= 16) {
                __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcPtr)); // Load 16 bytes
                _mm_store_si128(reinterpret_cast<__m128i*>(destPtr), data);              // Store 16 bytes
                srcPtr += 16;                                           // Advance source pointer
                destPtr += 16;                                          // Advance destination pointer
                size -= 16;                                             // Decrease remaining size
            }

            // Copy remaining bytes
            while (size > 0) {
                *destPtr++ = *srcPtr++;                                // Copy remaining bytes
                size--;
            }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"SSE2-optimized memory copy completed");
#endif
        }
        else {
            // Use standard copy for small sizes (compiler will optimize)
            memcpy(dest, src, size);                                   // Standard library for small copies
        }
    }
    catch (...) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Exception in optimized memory copy - falling back to memcpy");

        // Fallback to standard memory copy on any error
        memcpy(dest, src, size);                                       // Safe fallback implementation
    }
}
#else
// Fallback implementation for non-Windows or non-x64 platforms
void GamingAI::FastMemoryCopy(void* dest, const void* src, size_t size) const {
    // Use standard library function on unsupported platforms
    memcpy(dest, src, size);                                           // Cross-platform compatibility
}
#endif

// High-performance data processing using SSE2 intrinsics
#if defined(PLATFORM_WINDOWS) && defined(_M_X64)
uint32_t GamingAI::FastDataChecksum(const uint8_t* data, size_t size) const {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Calculating fast checksum - Size: %zu bytes", size);
    #endif

    try {
        uint32_t checksum = 0;

        if (size >= 16) {
            const uint8_t* dataPtr = data;                             // Pointer to current data position
            size_t remainingSize = size;                               // Track remaining bytes to process

            // Process 16 bytes at a time using SSE2 intrinsics
            while (remainingSize >= 16) {
                __m128i dataChunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dataPtr)); // Load 16 bytes

                // Sum bytes using horizontal add operations
                __m128i zero = _mm_setzero_si128();                    // Create zero vector for SAD operation
                __m128i sad = _mm_sad_epu8(dataChunk, zero);           // Sum of absolute differences (byte sum)

                // Extract the sum from the SSE register
                uint32_t chunkSum = static_cast<uint32_t>(_mm_cvtsi128_si32(sad)); // Extract low 32 bits
                checksum += chunkSum;                                  // Add to running checksum

                dataPtr += 16;                                         // Advance data pointer
                remainingSize -= 16;                                   // Decrease remaining size
            }

            // Process remaining bytes with standard loop
            while (remainingSize > 0) {
                checksum += *dataPtr++;                                // Add each remaining byte
                remainingSize--;                                       // Decrease counter
            }

            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SSE2-optimized checksum calculated: 0x%08X", checksum);
            #endif
        }
        else {
            // Process small data sizes with simple loop
            for (size_t i = 0; i < size; ++i) {
                checksum += data[i];                                   // Simple byte summation for small data
            }
        }

        return checksum;                                               // Return calculated checksum
    }
    catch (...) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Exception in optimized checksum - using fallback");

        // Fallback to MathPrecalculation hash function on error
        return FAST_MATH.FastFNV1aHash(data, size);                   // Safe fallback using existing hash
    }
}
#else
// Fallback implementation for non-Windows or non-x64 platforms
uint32_t GamingAI::FastDataChecksum(const uint8_t* data, size_t size) const {
    // Use MathPrecalculation hash function on unsupported platforms
    return FAST_MATH.FastFNV1aHash(data, size);                       // Cross-platform compatibility
}
#endif

#pragma warning(pop)

// Pick a combined threat budget, rather than rounding every enemy family up to
// one (which would keep almost the entire roster after an 85% reduction).
uint64_t GamingAI::SelectThreatBudget(int count, float pressure, uint32_t seed)
{
    count = (std::clamp)(count, 0, 64);
    if (!count) return 0;
    const int budget = (std::clamp)(static_cast<int>(std::lround(count * (std::clamp)(pressure, 0.0f, 1.0f))), 1, count);
    std::array<int, 64> order{}; for (int i = 0; i < count; ++i) order[i] = i;
    uint64_t selected = 0;
    for (int i = 0; i < budget; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const int j = i + static_cast<int>(seed % static_cast<uint32_t>(count - i));
        std::swap(order[i], order[j]); selected |= uint64_t(1) << order[i];
    }
    return selected;
}

// Outdoor keep-away rule: while inside the safety margin, no tangential border
// crawl is accepted. A displaced actor may still move strictly inward to recover.
bool GamingAI::NavigationStepAllowed(float fromX, float fromY, float toX, float toY, float width, float height, float margin)
{
    if (!std::isfinite(toX) || !std::isfinite(toY) || toX < 1 || toY < 1 || toX >= width - 1 || toY >= height - 1) return false;
    const float before = (std::min)((std::min)(fromX, fromY), (std::min)(width - fromX, height - fromY));
    const float after = (std::min)((std::min)(toX, toY), (std::min)(width - toX, height - toY));
    return after >= margin || after > before + 0.001f;
}
// Board games retain their existing rule engines and private search workers.
// GamingAI supplies strategic move ordering and acknowledges both the proposed
// move and the move actually committed against the current position hash.
bool GamingAI::FullBoardStrength(int iq) { return iq >= 160; }
AIBoardPlan GamingAI::RequestBoardStrategy(int scene, uint64_t position, int iq, int moveNumber, int pieces, int purpose, bool demo)
{
    AIBoardPlan plan; plan.scene=scene; plan.position=position; plan.iq=iq;
    plan.moveNumber=moveNumber; plan.purpose=purpose; plan.demo=demo;
    if (scene<33 || scene>35 || !m_isInitialized.load()) return plan;
    const AIGameStrategy profile=m_gameHistory->Strategy(scene,1,demo);
    plan.goal=pieces<=12 ? AIBoardGoal::Endgame : (moveNumber<=8 ? AIBoardGoal::Develop
        : (profile.boardCaptureRate>.30f ? AIBoardGoal::KingSafety : AIBoardGoal::Initiative));
    plan.request=m_gameHistory->nextSequence.fetch_add(1);
    GameHistory::Event event;
    { std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); event=m_gameHistory->Make(GameHistory::BoardRequest); }
    event.sequence=plan.request; event.frame.scene=scene; event.frame.demo=demo; event.board.plan=plan;
    plan.acknowledged=true; event.board.plan=plan;
    if (!m_gameHistory->Push(event)) plan.acknowledged=false;
    { std::lock_guard<std::mutex> lock(m_gameHistory->cacheMutex); m_gameHistory->boardStatus[scene-33]=AIBoardStatus(); m_gameHistory->boardStatus[scene-33].plan=plan; }
    return plan;
}
void GamingAI::OrderBoardMoves(const AIBoardPlan& plan, const AIBoardCandidate* moves, int count, int* order)
{
    count=(std::clamp)(count,0,512); if (!moves || !order) return;
    std::array<int,512> priority{};
    for (int i=0;i<count;++i)
    {
        const auto& m=moves[i]; order[i]=i;
        int value=(m.flags&2u ? 50 : 0)+(m.flags&4u ? 70 : 0)+(m.flags&1u ? 30+m.captured*2-m.piece : 0);
        if (plan.goal==AIBoardGoal::Develop) value+=(m.development?20:0)+static_cast<int>(m.centre*12.0f);
        else if (plan.goal==AIBoardGoal::KingSafety) value+=(m.flags&8u?30:0)+(m.flags&2u?10:0);
        else if (plan.goal==AIBoardGoal::Initiative) value+=static_cast<int>(m.centre*18.0f)+(m.flags&1u?10:0);
        else value+=(m.piece==1?12:0)+static_cast<int>(m.centre*8.0f);
        priority[i]=value;
    }
    std::sort(order,order+count,[&](int a,int b){return priority[a]!=priority[b]?priority[a]>priority[b]:a<b;});
}
bool GamingAI::AcknowledgeBoardSuggestion(const AIBoardPlan& plan, const AIBoardCandidate& move, int score, int depth, bool valid)
{
    if (plan.scene<33 || plan.scene>35) return false;
    AIBoardStatus status; status.plan=plan; status.move=move; status.score=score; status.depth=depth; status.ready=valid;
    GameHistory::Event event;
    { std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); event=m_gameHistory->Make(GameHistory::BoardSuggestion); }
    event.frame.scene=plan.scene; event.frame.demo=plan.demo; event.sequence=plan.request; event.board=status;
    const bool acknowledged=m_gameHistory->Push(event);
    { std::lock_guard<std::mutex> lock(m_gameHistory->cacheMutex); auto& cached=m_gameHistory->boardStatus[plan.scene-33];
        if (cached.plan.request==plan.request) { status.plan.acknowledged=acknowledged&&plan.acknowledged; cached=status; } }
    return acknowledged;
}
void GamingAI::RecordBoardMove(int scene, uint64_t before, uint64_t after, uint64_t request,
    const AIBoardCandidate& move, bool human, bool demo, bool applied)
{
    if (scene<33 || scene>35) return;
    AIBoardStatus status;
    { std::lock_guard<std::mutex> lock(m_gameHistory->cacheMutex); auto& cached=m_gameHistory->boardStatus[scene-33];
        status=cached;
        if (!request && human && cached.ready && before==cached.plan.position && move.from==cached.move.from && move.to==cached.move.to && move.promotion==cached.move.promotion) request=cached.plan.request;
        status.applied=applied; status.accepted=applied && before==status.plan.position && request!=0 && request==status.plan.request;
        status.observedPosition=before;
        status.move=move; status.resultingPosition=after; status.human=human;
        if (request && cached.plan.request==request) cached=status; }
    status.plan.scene=scene; if (!request) status.plan.position=before; status.plan.request=request; status.plan.demo=demo;
    GameHistory::Event event;
    { std::lock_guard<std::mutex> lock(m_gameHistory->lifecycleMutex); event=m_gameHistory->Make(GameHistory::BoardMove); }
    event.frame.scene=scene; event.frame.demo=demo; event.board=status; event.sequence=request; m_gameHistory->Push(event);
}
AIBoardStatus GamingAI::GetBoardStatus(int scene) const
{
    if (scene<33 || scene>35) return {};
    std::lock_guard<std::mutex> lock(m_gameHistory->cacheMutex); return m_gameHistory->boardStatus[scene-33];
}
void GamingAI::ResetBoardAdvisor(int scene)
{
    if (scene<33 || scene>35) return;
    std::lock_guard<std::mutex> lock(m_gameHistory->cacheMutex); m_gameHistory->boardStatus[scene-33]=AIBoardStatus();
}

// ------------------------------------------------------------------------------------------------
//  SPACE DOGFIGHT POLICY (see AIDogfightMemory in GamingAI.h).  Allocation free; one call per pilot per frame.
// ------------------------------------------------------------------------------------------------
static float DogRand(AIDogfightMemory& m)                                           // tiny LCG: deterministic per pilot, no global state
{
    m.rng = m.rng * 1664525u + 1013904223u;
    return static_cast<float>((m.rng >> 8) & 0xFFFFu) / 65535.0f;
}
AIDogfightOrders GamingAI::StepDogfighter(AIDogfightMemory& m, const AIDogfightFacts& f)
{
    AIDogfightOrders o;
    const float dt = (std::clamp)(f.dt, 0.0f, 0.1f);
    const int sk = (std::clamp)(f.skill, 0, 3);
    // Freelancer pilot-block values per skill tier (easy / medium / hard / ace): gun accuracy cone (deg), seconds between bursts, dodge interval and length, missile gap.
    static const float kCone[4] = { 15.0f, 10.0f, 8.0f, 6.0f }, kRest[4] = { 2.0f, 1.2f, 0.8f, 0.5f }, kDodgeGap[4] = { 3.0f, 2.6f, 2.0f, 2.0f }, kDodgeLen[4] = { 2.5f, 2.0f, 2.0f, 2.0f }, kMissGap[4] = { 10.0f, 9.0f, 8.0f, 7.0f };
    m.stateT += dt; m.dodgeT -= dt; m.burstT -= dt; m.missileT -= dt; m.mineT -= dt;
    const float len = std::sqrt(f.lx * f.lx + f.ly * f.ly + f.lz * f.lz);
    if (len < 1.0e-4f) return o;
    const float ux = f.lx / len, uy = f.ly / len, uz = f.lz / len;
    const float yawErr = std::atan2(ux, uz), pitchErr = std::atan2(uy, std::sqrt(ux * ux + uz * uz));        // radians; target right / above = positive
    const float angDeg = std::acos((std::clamp)(uz, -1.0f, 1.0f)) * 57.29578f;                               // angle between the nose and the target
    auto aim = [&](float gain) { o.yaw = (std::clamp)(yawErr * gain, -1.0f, 1.0f); o.pitch = (std::clamp)(pitchErr * gain, -1.0f, 1.0f); };
    // Damage reaction: a sudden loss of hull triggers a hard break (DamageReactionBlock); a nearly dead pilot of medium skill or better runs.
    if (m.lastHp < 0.0f) m.lastHp = f.hpFrac;
    const float lost = m.lastHp - f.hpFrac; m.lastHp = f.hpFrac > m.lastHp ? f.hpFrac : m.lastHp - (m.lastHp - f.hpFrac) * 0.5f;
    if (f.hpFrac < 0.18f && sk >= 1) { if (m.state != AIDogState::Flee) { m.state = AIDogState::Flee; m.stateT = 0; } }
    else if (lost > 0.10f && m.state != AIDogState::Break && m.state != AIDogState::Flee) { m.state = AIDogState::Break; m.stateT = 0; m.rollDir = DogRand(m) < 0.5f ? -1.0f : 1.0f; }
    // An enemy nose pointed at us inside gun range starts a dodge (EvadeDodgeBlock): slide for the easy tiers, corkscrew for hard and ace.
    if ((m.state == AIDogState::Trail || m.state == AIDogState::Strafe || m.state == AIDogState::Buzz) && f.tgtFacingMe > 0.90f && f.dist < f.gunRange && m.dodgeT <= -kDodgeGap[sk] * (0.6f + 0.8f * DogRand(m)))
    { m.state = AIDogState::Dodge; m.stateT = 0; m.dodgeT = kDodgeLen[sk]; m.dodgeSide = DogRand(m) < 0.5f ? -1.0f : 1.0f; m.dodgeUp = DogRand(m) < 0.5f ? -1.0f : 1.0f; }
    switch (m.state)
    {
    case AIDogState::Buzz:                                                           // BuzzHeadToward: approach at 0.8 throttle, sliding left / right
        aim(2.0f); o.throttle = 0.8f; o.slideX = std::sin(m.stateT * 2.2f) * 0.6f;
        if (f.dist < 220.0f) { m.state = AIDogState::PassBy; m.stateT = 0; m.dodgeSide = DogRand(m) < 0.5f ? -1.0f : 1.0f; }
        break;
    case AIDogState::PassBy:                                                         // BuzzPassBy: break away at roughly 90 degrees for two seconds
        o.yaw = m.dodgeSide; o.pitch = 0.25f * m.dodgeSide; o.throttle = 1.0f; o.roll = m.dodgeSide * 0.4f;
        if (m.stateT > 2.0f) { m.state = AIDogState::Trail; m.stateT = 0; }
        break;
    case AIDogState::Trail:                                                          // TrailBlock: turn at up to 0.6 throttle until the lock cone (30 deg) holds on the target
        aim(2.4f); o.throttle = f.dist > 300.0f ? 1.0f : 0.6f; o.boost = f.dist > 900.0f;
        if (sk >= 1 && !f.tgtEngineDead && f.dist < 500.0f && m.stateT > 3.0f && DogRand(m) < 0.4f * f.dt) { m.state = AIDogState::EngineKill; m.stateT = 0; }
        else if (angDeg < 30.0f && f.dist < 450.0f && m.stateT > 1.0f) { m.state = AIDogState::Strafe; m.stateT = 0; m.wobble = 0; }
        else if (m.stateT > 10.0f) { m.state = AIDogState::Buzz; m.stateT = 0; }
        break;
    case AIDogState::Strafe:                                                         // StrafeBlock: attack at full throttle, then run away until 600 m clear
        if (m.wobble < 0.5f) { aim(2.6f); o.throttle = 1.0f; if (f.dist < 90.0f || m.stateT > 8.0f) { m.wobble = 1.0f; m.stateT = 0; } }
        else { o.yaw = 0.5f * m.dodgeSide; o.pitch = -0.2f; o.throttle = 1.0f; o.boost = true; if (f.dist > 600.0f || m.stateT > 6.0f) { m.state = AIDogState::Buzz; m.stateT = 0; } }
        break;
    case AIDogState::Dodge:                                                          // EvadeDodge: slide (easy / medium) or corkscrew (hard / ace) while still turning toward the target
        aim(1.2f); o.throttle = 1.0f;
        if (sk >= 2) { o.roll = m.dodgeSide; o.yaw += 0.5f * std::sin(m.stateT * 7.0f); o.pitch += 0.5f * std::cos(m.stateT * 7.0f); o.slideX = m.dodgeSide * 0.5f; }
        else { o.slideX = m.dodgeSide; o.slideY = m.dodgeUp * 0.7f; o.roll = m.dodgeSide * 0.3f; }
        if (m.dodgeT <= 0.0f) { m.state = AIDogState::Trail; m.stateT = 0; }
        break;
    case AIDogState::Break:                                                          // EvadeBreak: roll hard, pull up and outrun for about two and a half seconds
        o.roll = m.rollDir; o.pitch = 0.8f; o.yaw = 0.4f * m.rollDir; o.throttle = 1.0f; o.boost = true; o.slideX = m.rollDir * 0.6f;
        if (m.stateT > 2.5f) { m.state = AIDogState::Trail; m.stateT = 0; }
        break;
    case AIDogState::EngineKill:                                                     // EngineKillBlock: face the target for two seconds and fire the engine disruptor
        aim(3.0f); o.throttle = 0.7f; o.disableEngine = angDeg < 12.0f && f.dist < 520.0f;
        if (m.stateT > 2.0f || f.tgtEngineDead) { m.state = AIDogState::Trail; m.stateT = 0; }
        break;
    case AIDogState::Flee:                                                           // nearly dead: turn tail and burn away, come back when the hull has recovered
        o.yaw = -(std::clamp)(yawErr * 2.0f, -1.0f, 1.0f); o.pitch = -(std::clamp)(pitchErr * 2.0f, -1.0f, 1.0f); o.throttle = 1.0f; o.boost = true; o.slideX = std::sin(m.stateT * 3.0f);
        if (f.hpFrac > 0.30f || m.stateT > 20.0f) { m.state = AIDogState::Buzz; m.stateT = 0; }
        break;
    }
    // Guns: bursts (6 s on, then a rest that shrinks with skill) fired only inside the accuracy cone and gun range.
    if (m.burstT <= 0.0f) { m.burstOn = !m.burstOn; m.burstT = m.burstOn ? 6.0f * (0.75f + 0.5f * DogRand(m)) : kRest[sk] * (0.75f + 0.5f * DogRand(m)); }
    o.fire = m.burstOn && uz > 0.0f && angDeg < kCone[sk] && f.dist < f.gunRange * 1.1f && m.state != AIDogState::PassBy && m.state != AIDogState::Flee;
    // Missiles: range 1000, cone 22.5 deg, a gap of 7 - 10 s; mines when the target sits behind the pilot inside 250 m.
    if (f.hasMissile && m.missileT <= 0.0f && f.dist < 1000.0f && f.dist > 150.0f && uz > 0.0f && angDeg < 22.5f) { o.missile = true; m.missileT = kMissGap[sk] * (0.75f + 0.5f * DogRand(m)); }
    if (f.hasMine && m.mineT <= 0.0f && uz < 0.0f && f.dist < 250.0f) { o.mine = true; m.mineT = 5.0f; }
    if (f.missileInbound) { o.countermeasure = true; o.slideX = m.dodgeSide; o.slideY = m.dodgeUp * 0.5f; o.boost = true; }
    return o;
}
AIDogfightOrders GamingAI::FlyToward(float lx, float ly, float lz, float dist, float arriveDist, float cruiseThrottle)
{
    AIDogfightOrders o; const float len = std::sqrt(lx * lx + ly * ly + lz * lz); if (len < 1.0e-4f) return o;
    const float ux = lx / len, uy = ly / len, uz = lz / len, yawErr = std::atan2(ux, uz), pitchErr = std::atan2(uy, std::sqrt(ux * ux + uz * uz));
    o.yaw = (std::clamp)(yawErr * 2.0f, -1.0f, 1.0f); o.pitch = (std::clamp)(pitchErr * 2.0f, -1.0f, 1.0f);
    float t = cruiseThrottle; if (std::fabs(yawErr) > 1.0f) t *= 0.4f;                                       // a sharp turn first, then full speed
    if (dist < arriveDist * 3.0f) t *= (std::max)(0.15f, dist / (arriveDist * 3.0f));                        // ease off on arrival
    o.throttle = t; return o;
}
AILandingOrders GamingAI::StepLanding(AILandingMemory& m, const AILandingFacts& f)
{
    AILandingOrders o; m.phaseT += f.dt; m.totalT += f.dt;
    const float spd = (std::max)(1.0f, f.spdMax);
    const float apLen = std::sqrt(f.ax * f.ax + f.ay * f.ay + f.az * f.az);
    const bool aligned = apLen > 1.0e-3f && f.az / apLen > 0.97f;                                     // nose within about 14 degrees of the approach point
    switch (m.phase)
    {
    case AILandPhase::Approach:
    {
        // Full speed toward the standoff point, easing off on arrival (the same steering as a waypoint flight).
        const AIDogfightOrders s = FlyToward(f.ax, f.ay, f.az, f.apDist, f.standoff * 0.4f, 1.0f);
        o.yaw = s.yaw; o.pitch = s.pitch; o.throttle = s.throttle;
        if (!f.cruising && !f.cruiseCharging && f.apDist > 4500.0f && aligned) o.cruiseToggle = true;     // far and lined up: engage the cruise drive
        if (f.cruising) { o.throttle = 1.0f; if (f.apDist < 3500.0f) o.cruiseToggle = true; }          // cruise must be off well before the portal
        if (!f.detour && !f.cruising && f.apDist < f.standoff * 0.35f) { m.phase = AILandPhase::Align; m.phaseT = 0.0f; }
        else if (!f.detour && !f.cruising && f.along > 0.0f && f.dist < f.standoff * 1.15f) { m.phase = AILandPhase::Align; m.phaseT = 0.0f; }       // already on the bay's side and inside the standoff distance: go straight in (it used to fly OUT to the standoff point first, away from the target)
        break;
    }
    case AILandPhase::Align:
    {
        // Creep onto the landing axis at the standoff point; once on the right side and close to the axis, start the descent.
        const AIDogfightOrders s = FlyToward(f.ax, f.ay, f.az, f.apDist, (std::max)(60.0f, f.standoff * 0.15f), 0.35f);
        o.yaw = s.yaw; o.pitch = s.pitch; o.throttle = (std::min)(s.throttle, 0.3f);
        if (f.cruising) o.cruiseToggle = true;
        if (f.along > 0.0f && f.lateral < f.radius * 0.4f && f.lz > 0.0f) { m.phase = AILandPhase::Descend; m.phaseT = 0.0f; }
        else if (m.phaseT > 30.0f) { m.phase = AILandPhase::Approach; m.phaseT = 0.0f; }                // lost the axis: fly the approach again
        break;
    }
    default:                                                                                              // Descend
    {
        const AIDogfightOrders s = FlyToward(f.lx, f.ly, f.lz, f.dist, f.radius, 1.0f);
        o.yaw = s.yaw; o.pitch = s.pitch;
        // The speed profile: slow as the portal nears, never above the limit.  Above the wanted speed the engine is cut and the velocity chase brakes the ship.
        const float want = (std::min)(f.maxLandSpeed * 0.8f, 20.0f + f.dist * 0.12f);
        o.throttle = f.speed > want * 1.3f ? 0.0f : (std::clamp)(want / spd, 0.03f, 1.0f);
        // The funnel narrows toward the portal; leaving it (or ending up on the wrong side) sends the ship back to line up again.
        const float funnel = f.radius * 0.5f + (std::max)(0.0f, f.dist - f.radius) * 0.12f;
        if (f.lateral > funnel * 1.6f || f.along < -f.radius * 0.2f) { m.phase = AILandPhase::Align; m.phaseT = 0.0f; }
        else if (f.dist < f.radius * 1.1f && f.speed <= f.maxLandSpeed) o.requestLand = true;
        break;
    }
    }
    o.phase = m.phase; return o;
}

// ------------------------------------------------------------------------------------------------
//  MASTER AI SPACE COMMAND (see AISpaceMemory in GamingAI.h).  Allocation free, bounded loops (16 contacts, goal arrays as given), called a few times a second per demo pilot.
// ------------------------------------------------------------------------------------------------
void GamingAI::ResetSpaceMemory(AISpaceMemory& m)
{
    const uint32_t d = m.decisions, s = m.stalls; m = AISpaceMemory(); m.decisions = d; m.stalls = s;                // goals are per system, so ids and rejections must not leak into the next one
}
static bool SpaceRejected(const AISpaceMemory& m, int id, AISpaceGoalKind k)
{
    for (size_t i = 0; i < m.rejId.size(); ++i) if (m.rejId[i] == id && m.rejKind[i] == static_cast<uint8_t>(k) && m.rejUntil[i] > m.clock) return true;
    return false;
}
AISpaceOrders GamingAI::PlanSpaceMission(AISpaceMemory& m, const AISpaceFacts& f, const AISpaceContact* contacts, int contactCount, const AISpaceGoal* goals, int goalCount)
{
    AISpaceOrders o; const float dt = (std::clamp)(f.dt, 0.0f, 0.5f); m.clock += dt; m.modeT += dt; m.goalT += dt;
    const int nc = (std::clamp)(contactCount, 0, 16), ng = (std::max)(0, goalCount);
    // ---- condition: vitality (hull weighs more than shields) and a damage-pressure meter that fades over about three seconds; a fast drop means someone is hurting us right now
    const float vit = (std::clamp)(f.hull * 0.65f + f.shield * 0.35f, 0.0f, 1.0f); if (m.lastVit < 0.0f) m.lastVit = vit;
    m.pressure = m.pressure * std::exp(-dt / 3.0f) + (std::max)(0.0f, m.lastVit - vit); m.lastVit = vit;
    // ---- THREAT ASSESSMENT: danger per contact = role weight x (skill) x proximity, doubled-ish when its nose is on us at close range, discounted when it is nearly dead
    float total = 0.0f, bestScore = -1.0f, nearD = 1.0e30f; int bestC = -1, nearC = -1;
    for (int i = 0; i < nc; ++i)
    {
        const AISpaceContact& c = contacts[i]; const float lin = (std::max)(0.0f, 1.0f - c.dist / 9000.0f), prox = (std::max)(0.03f, lin * lin);
        float w = c.weight * (0.6f + 0.25f * (std::clamp)(c.skill, 0.0f, 3.0f)); if (c.facingMe && c.dist < 1800.0f) w *= 1.5f; if (c.hpFrac < 0.25f) w *= 0.8f;
        const float th = w * prox; total += th;
        const float score = th + 0.3f * (1.0f - c.hpFrac) * prox + (c.id == m.targetId ? 0.12f : 0.0f);      // the kill order: most dangerous, finish the wounded, stay on the current target (hysteresis)
        if (score > bestScore) { bestScore = score; bestC = i; }
        if (c.dist < nearD) { nearD = c.dist; nearC = i; }
    }
    const float strength = (std::max)(0.05f, vit * (0.55f + 0.25f * (std::min)(1.0f, static_cast<float>(f.missiles) / 4.0f) + 0.20f * (std::clamp)(f.energy, 0.0f, 1.0f)));
    const float ratio = total / (strength * 2.2f);                                                                 // > 1: we are outgunned
    m.threatSm += (total - m.threatSm) * (1.0f - std::exp(-dt * 2.0f));
    o.threat = total; o.strength = strength; o.fightRatio = ratio;
    // ATTACKED FIRST: a hit (damage pressure) or a hostile with its nose on us at close range starts a ten second "fight back" window; inside it every contact within 9 km is engaged, and the pilot does not
    // run unless it is badly outgunned.  Without it a hostile is engaged inside 5.5 km (or 8 km when it is aiming at us).
    if (m.pressure > 0.02f || (nc > 0 && contacts[nearC].facingMe && nearD < 2500.0f)) m.attackedT = 10.0f; else m.attackedT = (std::max)(0.0f, m.attackedT - dt);
    const float engageR = f.warping ? 2000.0f : 5500.0f, faceR = f.warping ? 2500.0f : 8000.0f;                       // under warp, hostiles that are not close or not attacking are flown past
    const bool pressing = nc > 0 && (nearD < engageR || (contacts[nearC].facingMe && nearD < faceR) || (m.attackedT > 0.0f && nearD < 9000.0f) || m.pressure > 0.04f);
    const bool hurt = vit < 0.55f, crit = vit < 0.30f;
    // ---- TASK PRIORITY: survive (missile, outgunned, critical) > repair > fight what presses us > the job > explore.  Modes have a minimum dwell so the pilot does not flip-flop.
    AISpaceMode want = AISpaceMode::Patrol; AISpaceReason why = AISpaceReason::Idle;
    if (m.evadeT > 0.0f) m.evadeT -= dt;
    if (f.missileInbound && nc > 0) { m.evadeT = 2.5f; }
    if (m.evadeT > 0.0f && nc > 0) { want = AISpaceMode::Evade; why = AISpaceReason::Missile; }
    else if (pressing && f.haveHaven && (ratio > (m.attackedT > 0.0f ? 1.6f : 1.0f) || (crit && total > 0.05f))) { want = AISpaceMode::Retreat; why = crit ? AISpaceReason::Damaged : AISpaceReason::Outgunned; }
    else if (m.mode == AISpaceMode::Retreat && m.modeT < 4.0f && nc > 0 && f.haveHaven) { want = AISpaceMode::Retreat; why = m.reason; }      // commit to a fall-back for at least four seconds
    else if (pressing) { want = AISpaceMode::Engage; why = AISpaceReason::Hostiles; }
    else if (m.mode == AISpaceMode::Engage && m.modeT < 2.0f && nc > 0) { want = AISpaceMode::Engage; why = AISpaceReason::Hostiles; }               // keep fighting two more seconds after the pressure lifts
    else if (hurt && f.haveHaven) { want = AISpaceMode::Repair; why = AISpaceReason::Damaged; }
    // ---- DESTINATION: only when nothing above claims the pilot.  Jobs first; without a job an exploration target or a small task (pick up work, discover, scan).
    int goalIdx = -1;
    auto pickGoal = [&]() -> int
    {
        int best = -1; float bs = 0.02f;
        for (int i = 0; i < ng; ++i)
        {
            const AISpaceGoal& g = goals[i]; if (SpaceRejected(m, g.id, g.kind)) continue;
            float want01 = g.jobValue * 2.2f + (g.onRoute ? 1.0f : 0.0f) + g.value * 0.7f * ((f.hasJob && g.jobValue <= 0.0f && !g.onRoute) ? 0.2f : 1.0f);      // with a job, everything that is not the job counts for a fifth
            float s = want01 / (1.0f + g.dist / 7000.0f) - g.hazard * 0.9f; if (g.id == m.goalId && g.kind == m.goalKind) s = s * 1.6f + 0.15f;       // the current goal is sticky: another must clearly beat it (a flip of goal cancelled the autopilot and reopened the map)
            if (s > bs) { bs = s; best = i; }
        }
        return best;
    };
    auto pickHaven = [&](bool ignoreRejections) -> int
    {
        int best = -1; float bd = 1.0e30f;
        for (int i = 0; i < ng; ++i)
        {
            const AISpaceGoal& g = goals[i]; if (g.kind != AISpaceGoalKind::Station && g.kind != AISpaceGoalKind::Planet) continue; if (!ignoreRejections && SpaceRejected(m, g.id, g.kind)) continue;
            const float d = g.dist * (g.kind == AISpaceGoalKind::Station ? 0.8f : 1.0f);                                  // a station is preferred: its police patrol the safe zone
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    };
    if (want == AISpaceMode::Retreat || want == AISpaceMode::Repair) { goalIdx = pickHaven(want == AISpaceMode::Retreat); if (goalIdx < 0) { want = pressing ? AISpaceMode::Engage : AISpaceMode::Patrol; why = pressing ? AISpaceReason::Hostiles : AISpaceReason::Idle; } }
    if (want == AISpaceMode::Patrol)
    {
        goalIdx = pickGoal();
        if (goalIdx >= 0)
        {
            const AISpaceGoal& g = goals[goalIdx]; const bool job = g.jobValue > 0.0f || (g.onRoute && f.hasJob);
            if (job) { want = g.kind == AISpaceGoalKind::Gate ? AISpaceMode::Travel : AISpaceMode::Job; why = g.kind == AISpaceGoalKind::Gate ? AISpaceReason::JobTravel : AISpaceReason::JobWork; }
            else if (g.kind == AISpaceGoalKind::Gate) { want = AISpaceMode::Travel; why = AISpaceReason::Explore; }
            else { want = AISpaceMode::Patrol; why = (g.kind == AISpaceGoalKind::Station && !f.hasJob) ? AISpaceReason::SeekWork : AISpaceReason::Explore; }
        }
    }
    // ---- PROBLEM SOLVING: watch the progress toward the goal; no meaningful gain for 30 s (gates 40, havens 45) or 5 minutes on one goal rejects it for two minutes and the next best is taken
    if (goalIdx >= 0 && want != AISpaceMode::Engage && want != AISpaceMode::Evade)
    {
        const AISpaceGoal& g = goals[goalIdx]; const bool changed = g.id != m.goalId || g.kind != m.goalKind;
        if (changed) { m.goalId = g.id; m.goalKind = g.kind; m.bestDist = g.dist; m.stallT = 0.0f; m.goalT = 0.0f; o.goalChanged = true; ++m.decisions; }
        else if (g.dist < m.bestDist - 30.0f) { m.bestDist = g.dist; m.stallT = 0.0f; }
        else m.stallT += dt;
        const float limit = g.kind == AISpaceGoalKind::Gate ? 40.0f : ((g.kind == AISpaceGoalKind::Station || g.kind == AISpaceGoalKind::Planet) ? 45.0f : 30.0f);
        if ((m.stallT > limit || m.goalT > 300.0f) && want != AISpaceMode::Retreat)
        {
            const int s = m.nextRej; m.nextRej = (m.nextRej + 1) % static_cast<int>(m.rejId.size()); m.rejId[s] = g.id; m.rejKind[s] = static_cast<uint8_t>(g.kind); m.rejUntil[s] = m.clock + 120.0f; ++m.stalls;
            m.goalId = -1; m.stallT = 0.0f; m.goalT = 0.0f; why = AISpaceReason::Stalled; goalIdx = (want == AISpaceMode::Repair) ? pickHaven(false) : pickGoal();
            if (goalIdx >= 0) { m.goalId = goals[goalIdx].id; m.goalKind = goals[goalIdx].kind; m.bestDist = goals[goalIdx].dist; o.goalChanged = true; }
            else if (want != AISpaceMode::Repair) { want = AISpaceMode::Patrol; }
        }
    }
    else if (goalIdx < 0 && want != AISpaceMode::Engage && want != AISpaceMode::Evade) { m.goalId = -1; }
    // ---- publish
    if (want != m.mode) { m.mode = want; m.modeT = 0.0f; ++m.decisions; }
    m.reason = why;
    if (want == AISpaceMode::Engage || want == AISpaceMode::Evade) { o.targetContact = want == AISpaceMode::Evade && nearC >= 0 ? nearC : bestC; if (o.targetContact >= 0) m.targetId = contacts[o.targetContact].id; }
    else if (nc == 0) m.targetId = -1;
    o.mode = m.mode; o.reason = m.reason; o.goalIndex = goalIdx;
    // cruise is only sensible with the guns quiet and nobody close (a fall-back may cruise once it has some distance)
    if (m.cruiseOk) { if (nearD < 2500.0f) m.cruiseOk = false; } else if (nearD > 4000.0f) m.cruiseOk = true;       // HYSTERESIS: warp is refused inside 2.5 km of a hostile and allowed again beyond 4 km (a contact hovering near one distance used to flip it every plan)
    o.allowCruise = (m.mode == AISpaceMode::Travel || m.mode == AISpaceMode::Job || m.mode == AISpaceMode::Patrol || m.mode == AISpaceMode::Repair) ? m.cruiseOk : (m.mode == AISpaceMode::Retreat && nearD > 1200.0f);
    return o;
}

float GamingAI::ReputationPriceMul(int rep) { return (std::clamp)(1.0f - 0.0015f * static_cast<float>(rep), 0.85f, 1.15f); }
bool GamingAI::PoliceHostileToPlayer(int rep, float wanted) { return wanted > 0.0f || rep < -40; }
int GamingAI::MissionBounty(float basePerUnit, int units, int hops, float threat, float roll)
{
    const float h = static_cast<float>((std::max)(0, hops)), th = (std::clamp)(threat, 0.0f, 2.5f); const float v = basePerUnit * static_cast<float>((std::max)(1, units)) * (1.0f + 0.45f * h + 0.06f * h * h) * (1.0f + 1.6f * th + 1.0f * th * th) * (0.8f + 0.4f * roll);
    return (std::max)(50, static_cast<int>(v / 50.0f + 0.5f) * 50);
}
AIMarketQuote GamingAI::QuoteMarket(const AIMarketFacts& f)
{
    AIMarketQuote q; q.supply = (std::clamp)(f.baseline + f.delta + f.wobble, -1.2f, 1.2f);
    q.mult = (std::clamp)(std::exp(-0.85f * q.supply), 0.42f, 2.35f);                 // plenty -> cheap, scarce -> dear
    q.price = (std::max)(5, static_cast<int>(static_cast<float>(f.basePrice) * q.mult + 0.5f)); return q;
}
float GamingAI::MarketTrade(float delta, int units, bool bought) { return (std::clamp)(delta + (bought ? -0.035f : 0.035f) * static_cast<float>(units), -1.0f, 1.0f); }
float GamingAI::MarketRecover(float delta, float seconds) { return delta * std::exp(-(std::max)(0.0f, seconds) * 0.004f); }      // a half-life of about three minutes
AIAvoidOrders GamingAI::AvoidObstacles(const AIObstacle* obs, int n, float gx, float gy, float gz, float speed)
{
    AIAvoidOrders o; o.ax = gx; o.ay = gy; o.az = gz; const float gl2 = gx * gx + gy * gy + gz * gz; if (gl2 < 1.0f || !obs) return o;
    // Clearance beyond the surface grows with speed: at cruise a ship needs thousands of metres to turn.  But a body that sits NEXT TO THE GOAL (a station beside a planet, a portal over a world) must not
    // push the aim point away from it: near the goal the clearance is limited to 60 % of the goal's own gap to the body's surface, so the goal itself is never inside the inflated sphere (this made the
    // autopilot circle or fly away from a target that lay close to a planet).
    auto clearance = [&](const AIObstacle& b)
    {
        const float full = b.radius * 0.35f + (std::min)(speed, 900.0f) * 2.2f + 150.0f, gdx = gx - b.x, gdy = gy - b.y, gdz = gz - b.z, gap = std::sqrt(gdx * gdx + gdy * gdy + gdz * gdz) - b.radius;
        return gap > 0.0f ? (std::min)(full, (std::max)(60.0f, gap * 0.6f)) : full;
    };
    int worst = -1; float worstT = 2.0f;
    for (int i = 0; i < n; ++i)
    {
        const AIObstacle& b = obs[i]; const float t = (b.x * gx + b.y * gy + b.z * gz) / gl2; if (t <= 0.0f) continue;      // behind: no threat
        const float tc = (std::min)(t, 1.0f), px = gx * tc - b.x, py = gy * tc - b.y, pz = gz * tc - b.z, R = b.radius + clearance(b);
        if (px * px + py * py + pz * pz < R * R && tc < worstT) { worstT = tc; worst = i; }                               // the nearest sphere along the path is dealt with first
    }
    if (worst < 0) return o;
    const AIObstacle& b = obs[worst]; const float R = b.radius + clearance(b), tc = (std::min)(1.0f, (b.x * gx + b.y * gy + b.z * gz) / gl2);
    float mx = gx * tc - b.x, my = gy * tc - b.y, mz = gz * tc - b.z; float ml = std::sqrt(mx * mx + my * my + mz * mz);
    if (ml < 1.0f) { mx = 0.0f; my = 1.0f; mz = 0.0f; ml = 1.0f; }                                                      // dead centre: pass over the top
    const float k = R * 1.15f / ml; o.ax = b.x + mx * k; o.ay = b.y + my * k; o.az = b.z + mz * k; o.avoiding = true;      // a waypoint beside the sphere, on the side the path already leans to
    const float dc = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z) - b.radius;                                           // metres to the surface
    const float sp = (std::min)(speed, 900.0f); o.dropCruise = dc < sp * 0.8f + 600.0f; o.throttleScale = dc < sp * 2.0f ? 0.5f : 1.0f;       // the zone is sized from the speed CAPPED at 900 m/s: at warp speed (2.9 km/s) it used to grow to 12 km, so any planet near the 80 km route dropped the warp drive again and again (the path-based waypoint already steers round a body from far away)
    return o;
}

// ------------------------------------------------------------------------------------------------
//  STEADY AIM and WARP POLICY (see AISteadyAim / AIWarpMemory in GamingAI.h).  Allocation free.
// ------------------------------------------------------------------------------------------------
void GamingAI::SteadyAim(AISteadyAim& s, const AISteadyAimFacts& f, float& ox, float& oy, float& oz)
{
    const float gl = std::sqrt(f.gx * f.gx + f.gy * f.gy + f.gz * f.gz); if (gl < 1.0e-4f) { ox = s.wx; oy = s.wy; oz = s.wz; return; }
    const float ux = f.gx / gl, uy = f.gy / gl, uz = f.gz / gl, sp = std::sqrt(f.vx * f.vx + f.vy * f.vy + f.vz * f.vz); float ax = ux, ay = uy, az = uz;
    if (sp > 40.0f && f.dist > f.arrive * 2.0f)                                       // velocity compensation: lead against the sideways drift
    {
        const float along = f.vx * ux + f.vy * uy + f.vz * uz, lx = f.vx - ux * along, ly = f.vy - uy * along, lz = f.vz - uz * along, k = (std::max)(sp, 150.0f);
        ax = ux * k - lx * 0.9f; ay = uy * k - ly * 0.9f; az = uz * k - lz * 0.9f; const float al = std::sqrt(ax * ax + ay * ay + az * az); if (al > 1.0e-4f) { ax /= al; ay /= al; az /= al; }
    }
    if (f.time - s.lastTime > 0.25f) { s.wx = ax; s.wy = ay; s.wz = az; }              // a new trip: no history
    else
    {
        const float k = 1.0f - std::exp(-(std::min)(f.dt, 0.1f) * 7.0f); s.wx += (ax - s.wx) * k; s.wy += (ay - s.wy) * k; s.wz += (az - s.wz) * k;
        const float l = std::sqrt(s.wx * s.wx + s.wy * s.wy + s.wz * s.wz); if (l > 1.0e-4f) { s.wx /= l; s.wy /= l; s.wz /= l; } else { s.wx = ax; s.wy = ay; s.wz = az; }
    }
    s.lastTime = f.time; ox = s.wx; oy = s.wy; oz = s.wz;
}
bool GamingAI::StepWarp(AIWarpMemory& m, const AIWarpFacts& f)
{
    if (!f.cruising && !f.charging && f.allowed && f.time >= m.lockUntil && f.dist > f.engageDist && f.steerDemand < 0.25f && !f.obstacleClose) return true;     // a long trip, lined up: engage
    if (f.cruising && (f.dist < f.dropDist || !f.allowed)) return true;                                                                                                // arrived, or stand down for a fight
    if (f.cruising && f.obstacleClose) { m.lockUntil = f.time + f.lockSeconds; return true; }                                                                         // an obstacle is close: drop it and keep it off for a while
    return false;
}
