// =============================================================================
// ScriptManager.cpp - Scene Script Management System
// Written:  10-05-2026
// Version:  1.1
// Author:   Daniel J. Hobson of Australia (Ultimanium.com)
// =============================================================================
#include "ScriptManager.h"

#ifdef __USE_SCRIPT_MANAGER__

#include "Debug.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cassert>

extern Debug debug;
extern ScriptManager scriptManager;

// =============================================================================
// Scene-name table (mirrors SceneType enum in SceneManager.h)
// =============================================================================
static const std::unordered_map<int, std::string> kSceneNames = {
    { SCENE_NONE,        "SCENE_NONE"        },
    { SCENE_INITIALISE,  "SCENE_INITIALISE"  },
    { SCENE_GAMETITLE,   "SCENE_GAMETITLE"   },
    { SCENE_INTRO,       "SCENE_INTRO"       },
    { SCENE_INTRO_MOVIE, "SCENE_INTRO_MOVIE" },
    { SCENE_GAMEPLAY,    "SCENE_GAMEPLAY"    },
    { SCENE_GAMEOVER,    "SCENE_GAMEOVER"    },
    { SCENE_CREDITS,     "SCENE_CREDITS"     },
    { SCENE_EDITOR,      "SCENE_EDITOR"      },
    { SCENE_LOAD_MP3,    "SCENE_LOAD_MP3"    },
#if defined(_DEBUG)
    { SCENE_EXPERIMENT,  "SCENE_EXPERIMENT"  },
#endif
};

// =============================================================================
// Construction / Destruction
// =============================================================================
ScriptManager::ScriptManager()  = default;
ScriptManager::~ScriptManager() { Cleanup(); }

// =============================================================================
void ScriptManager::Initialize(
    FXManager*     fx,
    ThreadManager* threads,
    SoundManager*  sound,
    GUIManager*    gui,
    SceneManager*  sceneMgr,
    GamePlayer*    player,
    Physics*       physics,
    Joystick*      joystick,
    Camera*        camera)
{
    m_fx        = fx;
    m_threads   = threads;
    m_sound     = sound;
    m_gui       = gui;
    m_scene     = sceneMgr;
    m_player    = player;
    m_physics   = physics;
    m_joystick  = joystick;
    m_camera    = camera;

    RegisterExecuteFunctions();
}

// =============================================================================
void ScriptManager::Cleanup()
{
    StopExecution();
    m_commands.clear();
    m_collisionRules.clear();
    m_execRegistry.clear();
    m_variables.clear();
    m_loopStack.clear();
    m_loaded   = false;
    m_hasError = false;
}

// =============================================================================
// Loading
// =============================================================================
bool ScriptManager::LoadSceneScript(SceneType sceneType)
{
    auto it = kSceneNames.find(static_cast<int>(sceneType));
    if (it == kSceneNames.end()) {
        SetError(0, "Unknown SceneType: " + std::to_string(static_cast<int>(sceneType)));
        return false;
    }
    const std::string path = "Scripts/" + it->second + ".cgs";
    return LoadScriptFromFile(path);
}

bool ScriptManager::LoadScriptFromFile(const std::string& filePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_commands.clear();
    m_collisionRules.clear();
    m_header    = {};
    m_loaded    = false;
    m_hasError  = false;
    m_lastError.clear();

    if (!std::filesystem::exists(filePath)) {
        // No script for this scene is a valid state — not an error
        return false;
    }

    if (!ParseFile(filePath)) {
        return false;
    }

    BuildLabelMap();
    BuildLoopMap();
    m_loaded = true;
    debug.Log("[ScriptManager] Loaded: " + filePath +
              "  (v" + m_header.scriptVersion +
              ", written " + m_header.written + ")");
    return true;
}

// =============================================================================
// Parsing
// =============================================================================
bool ScriptManager::ParseFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        SetError(0, "Cannot open script file: " + path);
        return false;
    }

    std::string line;
    int lineNum      = 0;
    bool headerDone  = false;
    bool seenBodyCmd = false;

    while (std::getline(file, line)) {
        ++lineNum;

        // Strip CR from CRLF line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // Blank lines
        if (line.empty()) continue;

        // Header fields begin with ##
        if (line.rfind("##", 0) == 0) {
            if (headerDone) continue;                    // ignore stray ## after body
            std::string field = line.substr(2);
            auto colon = field.find(':');
            if (colon != std::string::npos) {
                std::string key   = field.substr(0, colon);
                std::string value = field.substr(colon + 1);
                // Trim leading whitespace from value
                size_t vs = value.find_first_not_of(" \t");
                if (vs != std::string::npos) value = value.substr(vs);
                ParseHeaderField(ToUpper(key), value);
            }
            continue;
        }

        // Comments begin with #
        if (line[0] == '#') continue;

        // First non-header, non-comment line ends the header block
        headerDone = true;

        ScriptCommand cmd = ParseLine(line, lineNum);
        if (cmd.type != ScriptCmdType::UNKNOWN &&
            cmd.type != ScriptCmdType::COMMENT)
        {
            if (cmd.type == ScriptCmdType::VAR_DECL && seenBodyCmd)
                SetError(lineNum, "VAR declarations must precede all executable commands");
            if (cmd.type != ScriptCmdType::VAR_DECL)
                seenBodyCmd = true;
            m_commands.push_back(std::move(cmd));
        }
    }
    return true;
}

bool ScriptManager::ParseHeaderField(const std::string& key, const std::string& value)
{
    if      (key == "SCRIPTVERSION") { m_header.scriptVersion = value; }
    else if (key == "WRITTEN")       { m_header.written       = value; }
    else if (key == "SCENE")         { m_header.scene         = value; }
    else if (key == "AUTHOR")        { m_header.author        = value; }
    else if (key == "DESCRIPTION")   { m_header.description   = value; }
    return true;
}

ScriptCommand ScriptManager::ParseLine(const std::string& line, int lineNum)
{
    ScriptCommand cmd;
    cmd.raw        = line;
    cmd.lineNumber = lineNum;

    std::vector<std::string> tokens = Tokenise(line);
    if (tokens.empty()) { cmd.type = ScriptCmdType::COMMENT; return cmd; }

    const std::string kw = ToUpper(tokens[0]);

    if      (kw == "EXECUTE")          { cmd.type = ScriptCmdType::EXECUTE; }
    else if (kw == "QUIT")             { cmd.type = ScriptCmdType::QUIT; }
    else if (kw == "ALERT")            { cmd.type = ScriptCmdType::ALERT; }
    else if (kw == "STOP")             { cmd.type = ScriptCmdType::STOP; }
    else if (kw == "POSITION")         { cmd.type = ScriptCmdType::POSITION; }
    else if (kw == "PLAY_POSITION")    { cmd.type = ScriptCmdType::PLAY_POSITION; }
    else if (kw == "GET_READY")        { cmd.type = ScriptCmdType::GET_READY; }
    else if (kw == "RESET")            { cmd.type = ScriptCmdType::RESET; }
    else if (kw == "SAVE")             { cmd.type = ScriptCmdType::SAVE; }
    else if (kw == "LOAD")             { cmd.type = ScriptCmdType::LOAD; }
    else if (kw == "DETECT_COLLISION") { cmd.type = ScriptCmdType::DETECT_COLLISION; }
    else if (kw == "LABEL") {
        cmd.type = ScriptCmdType::LABEL;
        cmd.args.assign(tokens.begin() + 1, tokens.end());
        // Strip optional trailing colon and normalise to upper-case
        if (!cmd.args.empty()) {
            if (!cmd.args[0].empty() && cmd.args[0].back() == ':')
                cmd.args[0].pop_back();
            cmd.args[0] = ToUpper(cmd.args[0]);
        }
        return cmd;
    }
    else if (kw == "GOTO") {
        cmd.type = ScriptCmdType::GOTO;
        cmd.args.assign(tokens.begin() + 1, tokens.end());
        if (!cmd.args.empty())
            cmd.args[0] = ToUpper(cmd.args[0]);
        return cmd;
    }
    else if (kw == "VAR") {
        cmd.type = ScriptCmdType::VAR_DECL;
        std::vector<std::string> vargs(tokens.begin() + 1, tokens.end());
        // Strip trailing semicolon from the last token (value token)
        if (!vargs.empty() && !vargs.back().empty() && vargs.back().back() == ';')
            vargs.back().pop_back();
        cmd.args = std::move(vargs);
        return cmd;
    }
    else if (kw == "FOR") {
        // FOR <var> = <start> TO <end> [STEP <n>] [DO]
        if (tokens.size() < 6) {
            SetError(lineNum, "FOR: syntax — FOR <var> = <start> TO <end> [STEP <n>] DO");
            cmd.type = ScriptCmdType::UNKNOWN;
            return cmd;
        }
        const std::string varName  = tokens[1];
        // tokens[2] == "="
        const std::string startStr = tokens[3];
        // tokens[4] == "TO"
        const std::string endStr   = tokens[5];
        std::string stepStr = "1";
        if (tokens.size() > 7 && ToUpper(tokens[6]) == "STEP")
            stepStr = tokens[7];
        cmd.type = ScriptCmdType::FOR_LOOP;
        cmd.args = { varName, startStr, endStr, stepStr };
        return cmd;
    }
    else if (kw == "BEGIN") {
        cmd.type = ScriptCmdType::BEGIN_BLOCK;
        return cmd;
    }
    else if (kw == "END") {
        cmd.type = ScriptCmdType::END_BLOCK;
        return cmd;
    }
    else if (kw == "WAIT" || kw.rfind("WAIT(", 0) == 0) {
        cmd.type = ScriptCmdType::WAIT;
        // Support both "WAIT(2.5)" (single token) and "WAIT 2.5" (space-separated)
        size_t parenOpen  = kw.find('(');
        size_t parenClose = kw.rfind(')');
        if (parenOpen != std::string::npos && parenClose != std::string::npos && parenClose > parenOpen) {
            cmd.args.push_back(kw.substr(parenOpen + 1, parenClose - parenOpen - 1));
            return cmd;
        }
        cmd.args.assign(tokens.begin() + 1, tokens.end());
        return cmd;
    }
    else {
        SetError(lineNum, "Unknown command: " + tokens[0]);
        cmd.type = ScriptCmdType::UNKNOWN;
        return cmd;
    }

    // Args are everything after the keyword token
    cmd.args.assign(tokens.begin() + 1, tokens.end());
    return cmd;
}

// Tokeniser — splits on whitespace, preserves double-quoted strings as one token
std::vector<std::string> ScriptManager::Tokenise(const std::string& line)
{
    std::vector<std::string> result;
    size_t i = 0;
    const size_t n = line.size();

    while (i < n) {
        // Skip whitespace
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= n) break;

        if (line[i] == '"') {
            // Quoted token
            ++i;
            std::string tok;
            while (i < n && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < n) { ++i; }
                tok += line[i++];
            }
            if (i < n) ++i;  // consume closing "
            result.push_back(tok);
        } else {
            // Unquoted token (stop at whitespace or open-paren boundary)
            std::string tok;
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i])))
                tok += line[i++];
            result.push_back(tok);
        }
    }
    return result;
}

// =============================================================================
// Execution
// =============================================================================
void ScriptManager::ExecuteScript()
{
    if (m_executing.exchange(true)) return;  // Already running
    m_stopRequested.store(false);
    RunCommands(m_commands);
    m_executing.store(false);
}

void ScriptManager::ExecuteScriptAsync()
{
    if (m_executing.exchange(true)) return;
    m_stopRequested.store(false);

    std::thread([this]() {
        RunCommands(m_commands);
        m_executing.store(false);
    }).detach();
}

void ScriptManager::StopExecution()
{
    m_stopRequested.store(true);
}

void ScriptManager::ExecuteCommandLine(const std::string& line)
{
    if (line.empty()) return;

    if (!m_fx) {
        debug.Log("[CONSOLE] ScriptManager not initialized — command ignored.");
        return;
    }
    if (m_executing.load()) {
        debug.Log("[CONSOLE] Script busy — wait for it to finish before issuing commands.");
        return;
    }

    ScriptCommand cmd = ParseLine(line, 0);
    if (cmd.type != ScriptCmdType::UNKNOWN && cmd.type != ScriptCmdType::COMMENT)
        DispatchCommand(cmd, 0);
    else
        debug.Log("[CONSOLE] Unrecognised command: " + line);
}

void ScriptManager::RunCommands(const std::vector<ScriptCommand>& cmds)
{
    const int count = static_cast<int>(cmds.size());
    int stepCount   = 0;
    int i           = 0;
    m_loopStack.clear();

    while (i < count) {
        if (m_stopRequested.load()) break;

        if (++stepCount > kMaxExecutionSteps) {
            SetError(0, "Script exceeded maximum execution steps — possible infinite loop.");
            break;
        }

        m_jumpTarget = -1;
        DispatchCommand(cmds[i], i);

        if (m_jumpTarget >= 0 && m_jumpTarget < count) {
            i = m_jumpTarget;
        } else {
            ++i;
        }
    }
}

void ScriptManager::DispatchCommand(const ScriptCommand& cmd, int cmdIdx)
{
    switch (cmd.type) {
        case ScriptCmdType::VAR_DECL:
            Cmd_VarDecl(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::FOR_LOOP:
            Cmd_ForLoop(cmd, cmdIdx);
            break;
        case ScriptCmdType::BEGIN_BLOCK:
            Cmd_BeginBlock();
            break;
        case ScriptCmdType::END_BLOCK:
            Cmd_EndBlock(cmd);
            break;
        case ScriptCmdType::EXECUTE:
            Cmd_Execute(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::QUIT:
            Cmd_Quit();
            break;
        case ScriptCmdType::ALERT:
            Cmd_Alert(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::STOP:
            Cmd_Stop(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::POSITION:
            Cmd_Position(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::PLAY_POSITION:
            Cmd_PlayPosition(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::GET_READY:
            Cmd_GetReady();
            break;
        case ScriptCmdType::RESET:
            Cmd_Reset();
            break;
        case ScriptCmdType::SAVE:
            Cmd_Save();
            break;
        case ScriptCmdType::LOAD:
            Cmd_Load();
            break;
        case ScriptCmdType::DETECT_COLLISION:
            Cmd_DetectCollision(cmd.args, cmd.lineNumber);
            break;
        case ScriptCmdType::WAIT:
            Cmd_Wait(SafeStof(cmd.args.empty() ? "0" : cmd.args[0]));
            break;
        case ScriptCmdType::LABEL:
            Cmd_Label(cmd.args);
            break;
        case ScriptCmdType::GOTO:
            Cmd_Goto(cmd.args, cmd.lineNumber);
            break;
        default:
            break;
    }
}

// =============================================================================
// Per-frame collision polling
// =============================================================================
void ScriptManager::Update(float /*deltaTime*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& rule : m_collisionRules) {
        if (rule.triggered && rule.oneShot) continue;
        if (EvaluateCollisionRule(rule)) {
            rule.triggered = true;
            TriggerCollisionAction(rule.onTriggerAction);
        }
    }
}

bool ScriptManager::EvaluateCollisionRule(const CollisionRule& rule)
{
    if (!m_player || !m_physics) return false;

    const std::string typeA = ToUpper(rule.entityTypeA);
    const std::string typeB = ToUpper(rule.entityTypeB);

    // PLAYER vs PLAYER — sphere overlap on 3D positions
    if (typeA == "PLAYER" && typeB == "PLAYER") {
        const PlayerInfo* a = m_player->GetPlayerInfo(rule.entityIndexA);
        const PlayerInfo* b = m_player->GetPlayerInfo(rule.entityIndexB);
        if (!a || !b) return false;

        PhysicsVector3D posA{ a->position3D.x, a->position3D.y, a->position3D.z };
        PhysicsVector3D posB{ b->position3D.x, b->position3D.y, b->position3D.z };
        return m_physics->CheckSphereCollision(
            posA, rule.collisionRadius,
            posB, rule.collisionRadius);
    }

    // PLAYER vs WALL — pixel-perfect collision bitmap check
    if (typeA == "PLAYER" && typeB == "WALL") {
        const PlayerInfo* a = m_player->GetPlayerInfo(rule.entityIndexA);
        if (!a) return false;
        return m_player->CheckCollisionAtPoint(
            rule.entityIndexA,
            Vector2(a->position2D.x, a->position2D.y));
    }

    // Other type combinations — stubbed; expand as world data becomes available
    debug.Log("[ScriptManager] DETECT_COLLISION: unsupported type pair " +
              rule.entityTypeA + " / " + rule.entityTypeB);
    return false;
}

void ScriptManager::TriggerCollisionAction(const std::string& actionStr)
{
    if (actionStr.empty()) return;
    ScriptCommand cmd = ParseLine(actionStr, 0);
    DispatchCommand(cmd);
}

// =============================================================================
// Command: EXECUTE FunctionName(arg0, arg1, ...)
// =============================================================================
void ScriptManager::Cmd_Execute(const std::vector<std::string>& args, int line)
{
    if (args.empty()) {
        SetError(line, "EXECUTE requires a function call");
        return;
    }

    // args[0] is "FunctionName(arg0,arg1,...)" — split on '('
    const std::string& call = args[0];
    size_t parenOpen  = call.find('(');
    size_t parenClose = call.rfind(')');

    if (parenOpen == std::string::npos || parenClose == std::string::npos) {
        SetError(line, "EXECUTE: malformed call (missing parentheses): " + call);
        return;
    }

    std::string funcName  = call.substr(0, parenOpen);
    std::string innerArgs = call.substr(parenOpen + 1, parenClose - parenOpen - 1);

    // Split innerArgs by comma into individual parameter strings
    std::vector<std::string> params;
    {
        std::istringstream ss(innerArgs);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t first = token.find_first_not_of(" \t");
            size_t last  = token.find_last_not_of(" \t");
            if (first != std::string::npos)
                params.push_back(token.substr(first, last - first + 1));
        }
    }

    // Also merge any additional whitespace-separated tokens from the original line
    // (handles: Execute FunctionName(a b c) — uncommon but possible)
    for (size_t i = 1; i < args.size(); ++i)
        params.push_back(args[i]);

    // Look up in registry (case-insensitive via normalised key)
    std::string key = ToUpper(funcName);
    auto it = m_execRegistry.find(key);
    if (it == m_execRegistry.end()) {
        SetError(line, "EXECUTE: unknown function: " + funcName);
        return;
    }
    it->second(params);
}

// =============================================================================
// Command: QUIT
// =============================================================================
void ScriptManager::Cmd_Quit()
{
    debug.Log("[ScriptManager] QUIT — requesting application shutdown.");
    StopExecution();
    PostQuitMessage(0);
}

// =============================================================================
// Command: ALERT <General|Error|CRITICAL> "<message>"
// =============================================================================
void ScriptManager::Cmd_Alert(const std::vector<std::string>& args, int line)
{
    if (args.size() < 2) {
        SetError(line, "ALERT requires <severity> <message>");
        return;
    }

    const std::string sev = ToUpper(args[0]);
    std::string message;
    // Reconstruct message from remaining tokens (handles unquoted multi-word)
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) message += ' ';
        message += args[i];
    }

    AlertSeverity severity = AlertSeverity::General;
    if      (sev == "ERROR")    severity = AlertSeverity::Error;
    else if (sev == "CRITICAL") severity = AlertSeverity::CRITICAL;

    // Build the GUI alert window title and colour based on severity
    std::wstring wMessage(message.begin(), message.end());
    std::wstring title;
    switch (severity) {
        case AlertSeverity::General:  title = L"[ALERT] ";   break;
        case AlertSeverity::Error:    title = L"[ERROR] ";   break;
        case AlertSeverity::CRITICAL: title = L"[CRITICAL] "; break;
    }
    title += wMessage;

    if (m_gui) m_gui->CreateAlertWindow(title);

    debug.Log("[ScriptManager] ALERT [" + sev + "] " + message);

    // CRITICAL alerts trigger immediate shutdown after display
    if (severity == AlertSeverity::CRITICAL)
        Cmd_Quit();
}

// =============================================================================
// Command: STOP <MUSIC|EFFECTS|MOUSE|GAMEPAD>
// =============================================================================
void ScriptManager::Cmd_Stop(const std::vector<std::string>& args, int line)
{
    if (args.empty()) {
        SetError(line, "STOP requires a target: MUSIC, EFFECTS, MOUSE, or GAMEPAD");
        return;
    }

    const std::string target = ToUpper(args[0]);

    if (target == "MUSIC") {
        if (m_sound) m_sound->StopPlaybackThread();
        debug.Log("[ScriptManager] STOP MUSIC");
    }
    else if (target == "EFFECTS") {
        if (m_fx) m_fx->StopAllFX();
        debug.Log("[ScriptManager] STOP EFFECTS");
    }
    else if (target == "MOUSE") {
        m_mouseStopped = true;
        // Actual suppression is read by the main loop via IsMouseStopped()
        debug.Log("[ScriptManager] STOP MOUSE");
    }
    else if (target == "GAMEPAD") {
        m_gamepadStopped = true;
        // Actual suppression is read by the main loop via IsGamepadStopped()
        debug.Log("[ScriptManager] STOP GAMEPAD");
    }
    else {
        SetError(line, "STOP: unknown target: " + args[0]);
    }
}

// =============================================================================
// Command: POSITION <x> <y> <z> <yaw> <pitch>
// =============================================================================
void ScriptManager::Cmd_Position(const std::vector<std::string>& args, int line)
{
    if (args.size() < 5) {
        SetError(line, "POSITION requires x y z yaw pitch");
        return;
    }
    if (!m_camera) return;

    float x   = SafeStof(args[0]);
    float y   = SafeStof(args[1]);
    float z   = SafeStof(args[2]);
    float yaw = SafeStof(args[3]);
    float pit = SafeStof(args[4]);

    m_camera->SetPosition(x, y, z);
    m_camera->SetYawPitch(yaw, pit);
    m_camera->UpdateCameraMatrices();

    debug.Log("[ScriptManager] POSITION set to (" +
              args[0] + ", " + args[1] + ", " + args[2] +
              ") Yaw=" + args[3] + " Pitch=" + args[4]);
}

// =============================================================================
// Command: PLAY_POSITION <x> <y> <z>
// =============================================================================
void ScriptManager::Cmd_PlayPosition(const std::vector<std::string>& args, int line)
{
    if (args.size() < 3) {
        SetError(line, "PLAY_POSITION requires x y z");
        return;
    }
    if (!m_player) return;

    float x = SafeStof(args[0]);
    float y = SafeStof(args[1]);
    float z = SafeStof(args[2]);

    // Apply to all active players; individual override requires playerID arg
    std::vector<int> ids = m_player->GetActivePlayerIDs();
    for (int id : ids) {
        PlayerInfo* pi = m_player->GetPlayerInfo(id);
        if (!pi) continue;
        pi->position3D = Vector3(x, y, z);
        pi->position2D = Vector2(x, y);
    }

    debug.Log("[ScriptManager] PLAY_POSITION set to (" +
              args[0] + ", " + args[1] + ", " + args[2] + ")");
}

// =============================================================================
// Command: GET_READY
// =============================================================================
void ScriptManager::Cmd_GetReady()
{
    // Play the "get ready" cue for the current player.
    // SFX_BEEP is the placeholder until SFX_GET_READY is added to SFX_ID.
    if (m_sound)
        m_sound->PlayImmediateSFX(SFX_ID::SFX_BEEP);

    debug.Log("[ScriptManager] GET_READY");
}

// =============================================================================
// Command: RESET
// =============================================================================
void ScriptManager::Cmd_Reset()
{
    debug.Log("[ScriptManager] RESET — returning to SCENE_GAMETITLE.");

    m_collisionRules.clear();
    m_mouseStopped   = false;
    m_gamepadStopped = false;

    if (m_fx)    m_fx->StopAllFX();
    if (m_sound) m_sound->StopPlaybackThread();

    if (m_scene) {
        m_scene->SetGotoScene(SCENE_GAMETITLE);
        m_scene->InitiateScene();
    }
}

// =============================================================================
// Command: SAVE  — precache all scene state to file
// =============================================================================
void ScriptManager::Cmd_Save()
{
    debug.Log("[ScriptManager] SAVE — writing precache.");

    if (m_scene) {
        const std::wstring savePath = L"Precache/scene_precache.bin";
        if (!m_scene->SaveSceneState(savePath))
            SetError(0, "SAVE: SceneManager::SaveSceneState failed.");
    }
}

// =============================================================================
// Command: LOAD  — restore precached scene state
// =============================================================================
void ScriptManager::Cmd_Load()
{
    debug.Log("[ScriptManager] LOAD — reading precache.");

    if (m_scene) {
        const std::wstring loadPath = L"Precache/scene_precache.bin";
        if (!m_scene->LoadSceneState(loadPath))
            SetError(0, "LOAD: SceneManager::LoadSceneState failed.");
    }
}

// =============================================================================
// Command: DETECT_COLLISION <typeA> <idxA> <typeB> <idxB> [radius] <action…>
//
//   Example:  DETECT_COLLISION PLAYER 0 WALL 0 ALERT Error "Hit wall"
//             DETECT_COLLISION PLAYER 0 PLAYER 1 48.0 ALERT General "Players collided"
// =============================================================================
void ScriptManager::Cmd_DetectCollision(const std::vector<std::string>& args, int line)
{
    if (args.size() < 5) {
        SetError(line, "DETECT_COLLISION requires typeA idxA typeB idxB <action>");
        return;
    }

    CollisionRule rule;
    rule.entityTypeA  = ToUpper(args[0]);
    rule.entityIndexA = SafeStoi(args[1]);
    rule.entityTypeB  = ToUpper(args[2]);
    rule.entityIndexB = SafeStoi(args[3]);

    size_t actionStart = 4;

    // Optional 5th numeric argument is the collision radius
    if (args.size() > 5) {
        bool isNumeric = !args[4].empty() &&
            (std::isdigit(static_cast<unsigned char>(args[4][0])) || args[4][0] == '.');
        if (isNumeric) {
            rule.collisionRadius = SafeStof(args[4]);
            actionStart = 5;
        }
    }

    // Remaining tokens form the action string
    std::string action;
    for (size_t i = actionStart; i < args.size(); ++i) {
        if (i > actionStart) action += ' ';
        action += args[i];
    }
    rule.onTriggerAction = action;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_collisionRules.push_back(std::move(rule));
    }

    debug.Log("[ScriptManager] DETECT_COLLISION registered: " +
              args[0] + "[" + args[1] + "] vs " +
              args[2] + "[" + args[3] + "]");
}

// =============================================================================
// Command: VAR <type> <Name> = <value>[;]
// Declares and initialises a typed global variable.
// =============================================================================
void ScriptManager::Cmd_VarDecl(const std::vector<std::string>& args, int line)
{
    // args: [type, name, "=", value]  — the "=" may be its own token or absent
    if (args.size() < 2) {
        SetError(line, "VAR: syntax — VAR <type> <name> = <value>");
        return;
    }

    const std::string typeName = ToUpper(args[0]);
    const std::string varName  = ToUpper(args[1]);

    // Locate value token: skip optional "=" token
    size_t valIdx = 2;
    if (args.size() > valIdx && args[valIdx] == "=") ++valIdx;
    std::string valueStr = (args.size() > valIdx) ? args[valIdx] : "";

    // Strip any stray trailing semicolon
    if (!valueStr.empty() && valueStr.back() == ';') valueStr.pop_back();

    ScriptVar var;
    if (typeName == "INT") {
        var.varType = ScriptVar::Type::INT;
        var.intVal  = SafeStoi(valueStr);
    }
    else if (typeName == "BOOL") {
        var.varType = ScriptVar::Type::BOOL;
        var.boolVal = SafeStob(valueStr);
    }
    else if (typeName == "FLOAT") {
        var.varType  = ScriptVar::Type::FLOAT;
        var.floatVal = SafeStof(valueStr);
    }
    else if (typeName == "STRING") {
        var.varType = ScriptVar::Type::STRING;
        var.strVal  = std::wstring(valueStr.begin(), valueStr.end());
    }
    else {
        SetError(line, "VAR: unknown type '" + args[0] + "' — use int, bool, float, or string");
        return;
    }

    m_variables[varName] = var;
    debug.Log("[ScriptManager] VAR " + args[0] + " " + varName + " = " + valueStr);
}

// =============================================================================
// Command: FOR <var> = <start> TO <end> [STEP <n>] DO
// Sets the loop variable, checks the initial condition, and pushes a loop frame.
// =============================================================================
void ScriptManager::Cmd_ForLoop(const ScriptCommand& cmd, int cmdIdx)
{
    if (cmd.args.size() < 4) {
        SetError(cmd.lineNumber, "FOR: internal parse error");
        return;
    }
    if (cmd.blockPeer < 0) {
        SetError(cmd.lineNumber, "FOR: no matching END");
        return;
    }

    const std::string varName = ToUpper(cmd.args[0]);
    const float startVal = SafeStof(cmd.args[1]);
    const float endVal   = SafeStof(cmd.args[2]);
    const float stepVal  = SafeStof(cmd.args[3], 1.0f);
    const bool isForward = (startVal < endVal);

    // Ensure variable exists; auto-create as int if not declared
    if (m_variables.find(varName) == m_variables.end()) {
        ScriptVar v;
        v.varType = ScriptVar::Type::INT;
        v.intVal  = 0;
        m_variables[varName] = v;
    }

    SetVarFromFloat(varName, startVal);

    // Skip entire loop if start already satisfies the exit condition
    const bool shouldSkip = isForward ? (startVal >= endVal) : (startVal <= endVal);
    if (shouldSkip) {
        m_jumpTarget = cmd.blockPeer + 1;
        debug.Log("[ScriptManager] FOR " + cmd.args[0] + " skipped (start already at/past end)");
        return;
    }

    // Find body start: skip BEGIN_BLOCK if it immediately follows the FOR command
    int bodyStart = cmdIdx + 1;
    if (bodyStart < static_cast<int>(m_commands.size()) &&
        m_commands[bodyStart].type == ScriptCmdType::BEGIN_BLOCK)
        ++bodyStart;

    LoopFrame frame;
    frame.varName      = varName;
    frame.endVal       = endVal;
    frame.step         = stepVal;
    frame.isForward    = isForward;
    frame.bodyStartIdx = bodyStart;
    frame.endIdx       = cmd.blockPeer;
    m_loopStack.push_back(frame);

    debug.Log("[ScriptManager] FOR " + cmd.args[0] +
              " = " + cmd.args[1] + " TO " + cmd.args[2] +
              " STEP " + cmd.args[3] + (isForward ? " [fwd]" : " [rev]"));
}

// =============================================================================
// Command: BEGIN
// Structural marker — no runtime action.
// =============================================================================
void ScriptManager::Cmd_BeginBlock()
{
    // No-op
}

// =============================================================================
// Command: END
// Updates the loop counter and either jumps back to the body or exits.
// =============================================================================
void ScriptManager::Cmd_EndBlock(const ScriptCommand& cmd)
{
    if (m_loopStack.empty()) {
        SetError(cmd.lineNumber, "END without an active FOR loop");
        return;
    }

    LoopFrame& frame = m_loopStack.back();

    const float newVal = GetVarAsFloat(frame.varName) +
                         (frame.isForward ? frame.step : -frame.step);
    SetVarFromFloat(frame.varName, newVal);

    const bool continueLoop = frame.isForward ? (newVal < frame.endVal)
                                               : (newVal > frame.endVal);
    if (continueLoop) {
        m_jumpTarget = frame.bodyStartIdx;
    } else {
        m_loopStack.pop_back();
        debug.Log("[ScriptManager] FOR loop completed");
    }
}

// =============================================================================
// Variable helpers
// =============================================================================
float ScriptManager::GetVarAsFloat(const std::string& nameUpper) const
{
    auto it = m_variables.find(nameUpper);
    if (it == m_variables.end()) return 0.0f;
    switch (it->second.varType) {
        case ScriptVar::Type::INT:    return static_cast<float>(it->second.intVal);
        case ScriptVar::Type::BOOL:   return it->second.boolVal ? 1.0f : 0.0f;
        case ScriptVar::Type::FLOAT:  return it->second.floatVal;
        case ScriptVar::Type::STRING: return 0.0f;
    }
    return 0.0f;
}

void ScriptManager::SetVarFromFloat(const std::string& nameUpper, float val)
{
    auto it = m_variables.find(nameUpper);
    if (it == m_variables.end()) {
        ScriptVar v;
        v.varType = ScriptVar::Type::INT;
        v.intVal  = static_cast<int>(val);
        m_variables[nameUpper] = v;
        return;
    }
    switch (it->second.varType) {
        case ScriptVar::Type::INT:    it->second.intVal   = static_cast<int>(val); break;
        case ScriptVar::Type::BOOL:   it->second.boolVal  = (val != 0.0f);         break;
        case ScriptVar::Type::FLOAT:  it->second.floatVal = val;                   break;
        case ScriptVar::Type::STRING: break;
    }
}

// =============================================================================
// Loop map — built once after parsing; cross-links FOR_LOOP and END_BLOCK
// commands via their blockPeer field so jumps can be resolved in O(1).
// =============================================================================
void ScriptManager::BuildLoopMap()
{
    std::vector<int> stack;
    const int count = static_cast<int>(m_commands.size());

    for (int i = 0; i < count; ++i) {
        if (m_commands[i].type == ScriptCmdType::FOR_LOOP) {
            stack.push_back(i);
        }
        else if (m_commands[i].type == ScriptCmdType::END_BLOCK) {
            if (stack.empty()) {
                SetError(m_commands[i].lineNumber, "END without a matching FOR loop");
                continue;
            }
            const int forIdx = stack.back();
            stack.pop_back();
            m_commands[forIdx].blockPeer = i;
            m_commands[i].blockPeer      = forIdx;
        }
    }

    for (int idx : stack)
        SetError(m_commands[idx].lineNumber, "FOR loop has no matching END");
}

// =============================================================================
// Label map — built once after parsing, maps upper-case label names to
// their index in m_commands so GOTO can redirect the execution loop.
// =============================================================================
void ScriptManager::BuildLabelMap()
{
    m_labelMap.clear();
    for (size_t idx = 0; idx < m_commands.size(); ++idx) {
        if (m_commands[idx].type == ScriptCmdType::LABEL &&
            !m_commands[idx].args.empty())
        {
            const std::string& name = m_commands[idx].args[0]; // already upper-case
            if (m_labelMap.count(name)) {
                SetError(m_commands[idx].lineNumber,
                    "Duplicate LABEL name: " + name + " — first definition kept.");
            } else {
                m_labelMap[name] = idx;
            }
        }
    }
}

// =============================================================================
// Command: LABEL <Name>[:]
// Marks a named jump target.  No runtime action — purely a position marker.
// =============================================================================
void ScriptManager::Cmd_Label(const std::vector<std::string>& args)
{
    if (!args.empty())
        debug.Log("[ScriptManager] LABEL reached: " + args[0]);
}

// =============================================================================
// Command: GOTO <Name>
// Redirects execution to the command immediately after the matching LABEL.
// The execution loop in RunCommands picks up m_jumpTarget after dispatch.
// =============================================================================
void ScriptManager::Cmd_Goto(const std::vector<std::string>& args, int line)
{
    if (args.empty()) {
        SetError(line, "GOTO requires a label name.");
        return;
    }

    const std::string& name = args[0]; // already upper-case from ParseLine
    auto it = m_labelMap.find(name);
    if (it == m_labelMap.end()) {
        SetError(line, "GOTO: undefined label: " + name);
        return;
    }

    // Jump to the command AFTER the LABEL marker so the label itself is skipped
    m_jumpTarget = static_cast<int>(it->second) + 1;
    debug.Log("[ScriptManager] GOTO " + name +
              " -> command index " + std::to_string(m_jumpTarget));
}

// =============================================================================
// Command: WAIT(seconds)
// Pauses script execution for the given duration.  Checks m_stopRequested
// every 50 ms so the wait can be interrupted by StopExecution().
// =============================================================================
void ScriptManager::Cmd_Wait(float seconds)
{
    if (seconds <= 0.0f) return;

    using namespace std::chrono;
    const auto end = steady_clock::now() +
        duration_cast<nanoseconds>(duration<float>(seconds));

    debug.Log("[ScriptManager] WAIT " + std::to_string(seconds) + "s");

    while (steady_clock::now() < end) {
        if (m_stopRequested.load()) return;
        std::this_thread::sleep_for(milliseconds(50));
    }
}

// =============================================================================
// Execute function registry
// =============================================================================
void ScriptManager::RegisterExecuteFunctions()
{
    // ----- FXManager -----

    m_execRegistry["FADETOBLACK"] = [this](const std::vector<std::string>& p) {
        if (m_fx) m_fx->FadeToBlack(
            SafeStof(p.size() > 0 ? p[0] : "1.0"),
            SafeStof(p.size() > 1 ? p[1] : "0.0"));
    };
    m_execRegistry["FADETOWHITE"] = [this](const std::vector<std::string>& p) {
        if (m_fx) m_fx->FadeToWhite(
            SafeStof(p.size() > 0 ? p[0] : "1.0"),
            SafeStof(p.size() > 1 ? p[1] : "0.0"));
    };
    // FadeToImage / FaderIntoImage — both map to FXManager::FadeToImage
    auto fnFadeToImage = [this](const std::vector<std::string>& p) {
        if (m_fx) m_fx->FadeToImage(
            SafeStof(p.size() > 0 ? p[0] : "1.0"),
            SafeStof(p.size() > 1 ? p[1] : "0.0"));
    };
    m_execRegistry["FADETOIMAGE"]    = fnFadeToImage;
    m_execRegistry["FADEREINTOIMAGE"] = fnFadeToImage;
    m_execRegistry["FADERTOIMAGE"]   = fnFadeToImage;

    m_execRegistry["FADETOCOLOR"] = [this](const std::vector<std::string>& p) {
        if (!m_fx) return;
        XMFLOAT4 col{
            SafeStof(p.size() > 0 ? p[0] : "0"),
            SafeStof(p.size() > 1 ? p[1] : "0"),
            SafeStof(p.size() > 2 ? p[2] : "0"),
            SafeStof(p.size() > 3 ? p[3] : "1")
        };
        m_fx->FadeToColor(col,
            SafeStof(p.size() > 4 ? p[4] : "1.0"),
            SafeStof(p.size() > 5 ? p[5] : "0.0"));
    };

    m_execRegistry["STOPALLFX"] = [this](const std::vector<std::string>&) {
        if (m_fx) m_fx->StopAllFX();
    };
    m_execRegistry["CANCELEFFECT"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty()) m_fx->CancelEffect(SafeStoi(p[0]));
    };
    m_execRegistry["RESTARTEFFECT"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty()) m_fx->RestartEffect(SafeStoi(p[0]));
    };
    m_execRegistry["CHAINEFFECT"] = [this](const std::vector<std::string>& p) {
        if (m_fx && p.size() >= 2) m_fx->ChainEffect(SafeStoi(p[0]), SafeStoi(p[1]));
    };

    m_execRegistry["CREATESTARFIELD"] = [this](const std::vector<std::string>& p) {
        if (!m_fx) return;
        m_fx->CreateStarfield(
            SafeStoi(p.size() > 0 ? p[0] : "200"),
            SafeStof(p.size() > 1 ? p[1] : "400"),
            SafeStof(p.size() > 2 ? p[2] : "-500"),
            XMFLOAT3(
                SafeStof(p.size() > 3 ? p[3] : "0"),
                SafeStof(p.size() > 4 ? p[4] : "0"),
                SafeStof(p.size() > 5 ? p[5] : "0")),
            SafeStob(p.size() > 6 ? p[6] : "false"));
    };
    m_execRegistry["STOPSTARFIELD"] = [this](const std::vector<std::string>&) {
        if (m_fx) m_fx->StopStarfield();
    };

    m_execRegistry["STARTSCROLLEFFECT"] = [this](const std::vector<std::string>& p) {
        if (!m_fx || p.size() < 4) return;
        const std::string dir = ToUpper(p.size() > 1 ? p[1] : "SCROLLRIGHT");
        FXSubType fxDir = FXSubType::ScrollRight;
        if      (dir == "SCROLLLEFT")        fxDir = FXSubType::ScrollLeft;
        else if (dir == "SCROLLUP")          fxDir = FXSubType::ScrollUp;
        else if (dir == "SCROLLDOWN")        fxDir = FXSubType::ScrollDown;
        else if (dir == "SCROLLUPLEFT")      fxDir = FXSubType::ScrollUpAndLeft;
        else if (dir == "SCROLLUPRIGHT")     fxDir = FXSubType::ScrollUpAndRight;
        else if (dir == "SCROLLDOWNLEFT")    fxDir = FXSubType::ScrollDownAndLeft;
        else if (dir == "SCROLLDOWNRIGHT")   fxDir = FXSubType::ScrollDownAndRight;
        m_fx->StartScrollEffect(
            static_cast<BlitObj2DIndexType>(SafeStoi(p[0])),
            fxDir,
            SafeStoi(p.size() > 2 ? p[2] : "2"),
            SafeStoi(p.size() > 3 ? p[3] : "320"),
            SafeStoi(p.size() > 4 ? p[4] : "240"),
            SafeStof(p.size() > 5 ? p[5] : "0"));
    };
    m_execRegistry["STOPSCROLLEFFECT"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty())
            m_fx->StopScrollEffect(static_cast<BlitObj2DIndexType>(SafeStoi(p[0])));
    };
    m_execRegistry["PAUSESCROLL"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty())
            m_fx->PauseScroll(static_cast<BlitObj2DIndexType>(SafeStoi(p[0])));
    };
    m_execRegistry["RESUMESCROLL"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty())
            m_fx->ResumeScroll(static_cast<BlitObj2DIndexType>(SafeStoi(p[0])));
    };
    m_execRegistry["UPDATESCROLLSPEED"] = [this](const std::vector<std::string>& p) {
        if (m_fx && p.size() >= 2)
            m_fx->UpdateScrollSpeed(
                static_cast<BlitObj2DIndexType>(SafeStoi(p[0])),
                SafeStoi(p[1]));
    };

    m_execRegistry["CREATEPARTICLEEXPLOSION"] = [this](const std::vector<std::string>& p) {
        if (m_fx) m_fx->CreateParticleExplosion(
            SafeStoi(p.size() > 0 ? p[0] : "0"),
            SafeStoi(p.size() > 1 ? p[1] : "0"),
            SafeStoi(p.size() > 2 ? p[2] : "80"),
            SafeStoi(p.size() > 3 ? p[3] : "120"));
    };

    m_execRegistry["STOPTEXTSCROLLER"]  = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty()) m_fx->StopTextScroller(SafeStoi(p[0]));
    };
    m_execRegistry["PAUSETEXTSCROLLER"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty()) m_fx->PauseTextScroller(SafeStoi(p[0]));
    };
    m_execRegistry["RESUMETEXTSCROLLER"] = [this](const std::vector<std::string>& p) {
        if (m_fx && !p.empty()) m_fx->ResumeTextScroller(SafeStoi(p[0]));
    };

    m_execRegistry["SAVEANDSUSPENDFXFORSCENE"] = [this](const std::vector<std::string>&) {
        if (m_fx) m_fx->SaveAndSuspendFXForScene();
    };
    m_execRegistry["RESTOREFXAFTERSCENE"] = [this](const std::vector<std::string>&) {
        if (m_fx) m_fx->RestoreFXAfterScene();
    };
    m_execRegistry["DISCARDSAVEDFXSTATE"] = [this](const std::vector<std::string>&) {
        if (m_fx) m_fx->DiscardSavedFXState();
    };

    m_execRegistry["INITWARPDOTTUNNEL"] = [this](const std::vector<std::string>& p) {
        if (!m_fx || p.size() < 6) return;
        const std::string spin = ToUpper(p.size() > 6 ? p[6] : "CLOCKWISE");
        TunnelSpinCycle sc = TunnelSpinCycle::Clockwise;
        if      (spin == "NONE")          sc = TunnelSpinCycle::None;
        else if (spin == "ANTICLOCKWISE") sc = TunnelSpinCycle::AntiClockwise;
        m_fx->Init3DWarpDOTTunnel(
            SafeStof(p[0]), SafeStof(p[1]), SafeStof(p[2]),
            SafeStof(p[3]), SafeStof(p[4]),
            sc,
            SafeStoi(p.size() > 5 ? p[5] : "3"),
            SafeStob(p.size() > 7 ? p[7] : "false"),
            SafeStoi(p.size() > 8 ? p[8] : "24"),
            SafeStoi(p.size() > 9 ? p[9] : "3"));
    };
    m_execRegistry["STOPWARPDOTTUNNEL"] = [this](const std::vector<std::string>&) {
        if (m_fx) m_fx->StopWarpDotTunnel();
    };

    // ----- Camera -----

    m_execRegistry["JUMPTO"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.size() < 3) return;
        m_camera->JumpTo(
            SafeStof(p[0]), SafeStof(p[1]), SafeStof(p[2]),
            SafeStoi(p.size() > 3 ? p[3] : "1"),
            SafeStob(p.size() > 4 ? p[4] : "false"));
    };
    m_execRegistry["JUMPTOWITHYAWPITCH"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.size() < 5) return;
        m_camera->JumpToWithYawPitch(
            SafeStof(p[0]), SafeStof(p[1]), SafeStof(p[2]),
            SafeStof(p[3]), SafeStof(p[4]),
            SafeStoi(p.size() > 5 ? p[5] : "1"),
            SafeStob(p.size() > 6 ? p[6] : "false"));
    };
    m_execRegistry["JUMPBACKHISTORY"] = [this](const std::vector<std::string>& p) {
        if (m_camera) m_camera->JumpBackHistory(SafeStoi(p.size() > 0 ? p[0] : "1"));
    };
    m_execRegistry["ROTATEX"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.empty()) return;
        m_camera->RotateX(SafeStof(p[0]),
            SafeStoi(p.size() > 1 ? p[1] : "1"),
            SafeStob(p.size() > 2 ? p[2] : "false"));
    };
    m_execRegistry["ROTATEY"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.empty()) return;
        m_camera->RotateY(SafeStof(p[0]),
            SafeStoi(p.size() > 1 ? p[1] : "1"),
            SafeStob(p.size() > 2 ? p[2] : "false"));
    };
    m_execRegistry["ROTATEZ"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.empty()) return;
        m_camera->RotateZ(SafeStof(p[0]),
            SafeStoi(p.size() > 1 ? p[1] : "1"),
            SafeStob(p.size() > 2 ? p[2] : "false"));
    };
    m_execRegistry["ROTATEXYZ"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.size() < 3) return;
        m_camera->RotateXYZ(
            SafeStof(p[0]), SafeStof(p[1]), SafeStof(p[2]),
            SafeStoi(p.size() > 3 ? p[3] : "1"),
            SafeStob(p.size() > 4 ? p[4] : "false"));
    };
    m_execRegistry["STOPROTATING"] = [this](const std::vector<std::string>&) {
        if (m_camera) m_camera->StopRotating();
    };
    m_execRegistry["PAUSEROTATION"] = [this](const std::vector<std::string>&) {
        if (m_camera) m_camera->PauseRotation();
    };
    m_execRegistry["RESUMEROTATION"] = [this](const std::vector<std::string>&) {
        if (m_camera) m_camera->ResumeRotation();
    };
    m_execRegistry["ROTATETOOPPOSITESIDE"] = [this](const std::vector<std::string>& p) {
        if (m_camera) m_camera->RotateToOppositeSide(SafeStoi(p.size() > 0 ? p[0] : "1"));
    };
    m_execRegistry["MOVEAROUNDTARGET"] = [this](const std::vector<std::string>& p) {
        if (!m_camera || p.size() < 3) return;
        m_camera->MoveAroundTarget(
            SafeStob(p[0]), SafeStob(p[1]), SafeStob(p[2]),
            SafeStob(p.size() > 3 ? p[3] : "false"));
    };
    m_execRegistry["SETFIELDOFVIEW"] = [this](const std::vector<std::string>& p) {
        if (m_camera && !p.empty()) m_camera->SetFieldOfView(SafeStof(p[0]));
    };
    m_execRegistry["SETNEARFAR"] = [this](const std::vector<std::string>& p) {
        if (m_camera && p.size() >= 2)
            m_camera->SetNearFar(SafeStof(p[0]), SafeStof(p[1]));
    };
    m_execRegistry["CANCELJUMP"] = [this](const std::vector<std::string>&) {
        if (m_camera) m_camera->CancelJump();
    };
    m_execRegistry["CLEARJUMPHISTORY"] = [this](const std::vector<std::string>&) {
        if (m_camera) m_camera->ClearJumpHistory();
    };
    m_execRegistry["SETROTATIONSPEED"] = [this](const std::vector<std::string>& p) {
        if (m_camera && !p.empty()) m_camera->SetRotationSpeed(SafeStof(p[0]));
    };

    // ----- SoundManager -----

    m_execRegistry["PLAYIMMEDIATESFX"] = [this](const std::vector<std::string>& p) {
        if (m_sound && !p.empty())
            m_sound->PlayImmediateSFX(static_cast<SFX_ID>(SafeStoi(p[0])));
    };
    m_execRegistry["SETGLOBALVOLUME"] = [this](const std::vector<std::string>& p) {
        if (m_sound && !p.empty()) m_sound->SetGlobalVolume(SafeStof(p[0]));
    };
    m_execRegistry["SETCOOLDOWN"] = [this](const std::vector<std::string>& p) {
        if (m_sound && p.size() >= 2)
            m_sound->SetCooldown(static_cast<SFX_ID>(SafeStoi(p[0])), SafeStof(p[1]));
    };
    m_execRegistry["CLEARCOOLDOWN"] = [this](const std::vector<std::string>& p) {
        if (m_sound && !p.empty())
            m_sound->ClearCooldown(static_cast<SFX_ID>(SafeStoi(p[0])));
    };
    m_execRegistry["STARTPLAYBACKTHREAD"] = [this](const std::vector<std::string>&) {
        if (m_sound) m_sound->StartPlaybackThread();
    };
    m_execRegistry["STOPPLAYBACKTHREAD"] = [this](const std::vector<std::string>&) {
        if (m_sound) m_sound->StopPlaybackThread();
    };

    // ----- SceneManager -----

    m_execRegistry["SETGOTOSCENE"] = [this](const std::vector<std::string>& p) {
        if (m_scene && !p.empty())
            m_scene->SetGotoScene(static_cast<SceneType>(SafeStoi(p[0])));
    };
    m_execRegistry["INITIATESCENE"] = [this](const std::vector<std::string>&) {
        if (m_scene) m_scene->InitiateScene();
    };

    // ----- GamePlayer -----

    m_execRegistry["SETPLAYERSTATE"] = [this](const std::vector<std::string>& p) {
        if (m_player && p.size() >= 2)
            m_player->SetPlayerState(
                SafeStoi(p[0]),
                static_cast<PlayerState>(SafeStoi(p[1])));
    };
    m_execRegistry["STARTPLAYERTIMER"] = [this](const std::vector<std::string>& p) {
        if (m_player && !p.empty()) m_player->StartPlayerTimer(SafeStoi(p[0]));
    };
    m_execRegistry["STOPPLAYERTIMER"] = [this](const std::vector<std::string>& p) {
        if (m_player && !p.empty()) m_player->StopPlayerTimer(SafeStoi(p[0]));
    };
    m_execRegistry["RESETALLPLAYERSTATS"] = [this](const std::vector<std::string>&) {
        if (m_player) m_player->ResetAllPlayerStats();
    };

    // ----- GUIManager -----

    m_execRegistry["CREATEALERTWINDOW"] = [this](const std::vector<std::string>& p) {
        if (!m_gui || p.empty()) return;
        std::wstring msg(p[0].begin(), p[0].end());
        m_gui->CreateAlertWindow(msg);
    };
    m_execRegistry["REMOVEWINDOW"] = [this](const std::vector<std::string>& p) {
        if (m_gui && !p.empty()) m_gui->RemoveWindow(p[0]);
    };
    m_execRegistry["SETWINDOWVISIBILITY"] = [this](const std::vector<std::string>& p) {
        if (m_gui && p.size() >= 2)
            m_gui->SetWindowVisibility(p[0], SafeStob(p[1]));
    };
}

// =============================================================================
// Utilities
// =============================================================================
void ScriptManager::SetError(int line, const std::string& msg)
{
    m_hasError  = true;
    m_lastError = (line > 0 ? "[Line " + std::to_string(line) + "] " : "") + msg;
    debug.Log("[ScriptManager] ERROR: " + m_lastError);
}

/*static*/ std::string ScriptManager::SceneTypeToName(SceneType t)
{
    auto it = kSceneNames.find(static_cast<int>(t));
    return (it != kSceneNames.end()) ? it->second : "SCENE_UNKNOWN";
}

/*static*/ float ScriptManager::SafeStof(const std::string& s, float fallback)
{
    try   { return std::stof(s); }
    catch (...) { return fallback; }
}

/*static*/ int ScriptManager::SafeStoi(const std::string& s, int fallback)
{
    try   { return std::stoi(s); }
    catch (...) { return fallback; }
}

/*static*/ bool ScriptManager::SafeStob(const std::string& s, bool fallback)
{
    const std::string u = ToUpper(s);
    if (u == "TRUE"  || u == "1" || u == "YES") return true;
    if (u == "FALSE" || u == "0" || u == "NO")  return false;
    return fallback;
}

/*static*/ std::string ScriptManager::ToUpper(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

#endif // __USE_SCRIPT_MANAGER__
