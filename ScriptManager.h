// =============================================================================
// ScriptManager.h - Scene Script Management System
// Written:  2026-05-10
// Version:  1.0
// Author:   Daniel J. Hobson of Australia 2026
//
// Loads, parses and executes ASCII scene script files (.cgs — CPGE Game Script).
// Scripts reside in Scripts/<SCENE_NAME>.cgs and are detected automatically
// during scene initialisation.  Each script file carries its own version header.
//
// Commands supported (v1.0):
//   Execute  FunctionName(arg, ...)   — invoke a registered engine API call
//   QUIT                              — full engine shutdown
//   ALERT    <severity> "<message>"   — on-screen alert (General/Error/CRITICAL)
//   STOP     MUSIC|EFFECTS|MOUSE|GAMEPAD
//   POSITION <x> <y> <z> <yaw> <pitch>
//   PLAY_POSITION <x> <y> <z>
//   GET_READY
//   RESET
//   SAVE
//   LOAD
//   DETECT_COLLISION <typeA> <idxA> <typeB> <idxB> <action…>
// =============================================================================
#pragma once

#include "Includes.h"
#include "DX_FXManager.h"
#include "ThreadManager.h"
#include "SoundManager.h"
#include "GUIManager.h"
#include "SceneManager.h"
#include "GamePlayer.h"
#include "Physics.h"
#include "Joystick.h"
#include "DXCamera.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

using namespace SoundSystem;

// =============================================================================
// Alert severity
// =============================================================================
enum class AlertSeverity {
    General,    // Informational notice
    Error,      // Recoverable error
    CRITICAL    // Fatal — auto-triggers QUIT after display
};

// =============================================================================
// Targets for the STOP command
// =============================================================================
enum class StopTarget {
    MUSIC,
    EFFECTS,
    MOUSE,
    GAMEPAD
};

// =============================================================================
// Internal command type tokens
// =============================================================================
enum class ScriptCmdType {
    EXECUTE,
    QUIT,
    ALERT,
    STOP,
    POSITION,
    PLAY_POSITION,
    GET_READY,
    RESET,
    SAVE,
    LOAD,
    DETECT_COLLISION,
    WAIT,
    LABEL,
    GOTO,
    COMMENT,
    UNKNOWN
};

// =============================================================================
// A single parsed command line
// =============================================================================
struct ScriptCommand {
    ScriptCmdType            type       = ScriptCmdType::UNKNOWN;
    std::vector<std::string> args;       // Tokenised arguments (quotes stripped)
    std::string              raw;        // Original source line for error reporting
    int                      lineNumber = 0;
};

// =============================================================================
// Versioned header block from the top of a .cgs file
// =============================================================================
struct ScriptFileHeader {
    std::string scriptVersion = "1.0";
    std::string written;                 // ISO date the script was authored
    std::string scene;                   // Target scene name
    std::string author;
    std::string description;
};

// =============================================================================
// A collision rule registered by DETECT_COLLISION
// =============================================================================
struct CollisionRule {
    std::string entityTypeA;            // "PLAYER", "ENEMY", "OBJECT", "ZONE"
    int         entityIndexA = 0;
    std::string entityTypeB;
    int         entityIndexB = 0;
    std::string onTriggerAction;        // Raw command string executed on trigger
    float       collisionRadius = 32.0f;// Default sphere radius for overlap test
    bool        triggered  = false;     // Has fired this session
    bool        oneShot    = true;      // Fire once then retire
};

// =============================================================================
// ScriptManager
// =============================================================================
class ScriptManager {
public:
    ScriptManager();
    ~ScriptManager();

    // Bind engine subsystem pointers (call once, before LoadSceneScript)
    void Initialize(
        FXManager*      fx,
        ThreadManager*  threads,
        SoundManager*   sound,
        GUIManager*     gui,
        SceneManager*   sceneMgr,
        GamePlayer*     player,
        Physics*        physics,
        Joystick*       joystick,
        Camera*         camera
    );

    void Cleanup();

    // Resolve Scripts/<sceneName>.cgs and load it
    bool LoadSceneScript(SceneType sceneType);
    // Load from an explicit file path
    bool LoadScriptFromFile(const std::string& filePath);

    // Run the loaded command list on the calling thread (blocks)
    void ExecuteScript();
    // Run on a detached background thread (returns immediately)
    void ExecuteScriptAsync();
    // Signal the async execution to stop at the next command boundary
    void StopExecution();

    // Call each frame — evaluates registered DETECT_COLLISION rules
    void Update(float deltaTime);

    // --- State queries ---
    bool IsExecuting()  const { return m_executing.load(); }
    bool IsLoaded()     const { return m_loaded; }
    bool HasError()     const { return m_hasError; }
    bool IsMouseStopped()   const { return m_mouseStopped; }
    bool IsGamepadStopped() const { return m_gamepadStopped; }

    const std::string&    GetLastError() const { return m_lastError; }
    const ScriptFileHeader& GetHeader()  const { return m_header; }

private:
    // --- Engine subsystem pointers (non-owning) ---
    FXManager*      m_fx        = nullptr;
    ThreadManager*  m_threads   = nullptr;
    SoundManager*   m_sound     = nullptr;
    GUIManager*     m_gui       = nullptr;
    SceneManager*   m_scene     = nullptr;
    GamePlayer*     m_player    = nullptr;
    Physics*        m_physics   = nullptr;
    Joystick*       m_joystick  = nullptr;
    Camera*         m_camera    = nullptr;

    // --- Script state ---
    ScriptFileHeader            m_header;
    std::vector<ScriptCommand>  m_commands;
    std::vector<CollisionRule>  m_collisionRules;

    std::atomic<bool>   m_executing{false};
    std::atomic<bool>   m_stopRequested{false};
    std::mutex          m_mutex;

    bool        m_loaded        = false;
    bool        m_hasError      = false;
    bool        m_mouseStopped  = false;
    bool        m_gamepadStopped= false;
    std::string m_lastError;

    // --- Parsing ---
    bool ParseFile(const std::string& path);
    bool ParseHeaderField(const std::string& key, const std::string& value);
    ScriptCommand ParseLine(const std::string& line, int lineNum);

    // Tokenise a line respecting double-quoted strings
    std::vector<std::string> Tokenise(const std::string& line);

    // --- Execution ---
    void RunCommands(const std::vector<ScriptCommand>& cmds);
    void DispatchCommand(const ScriptCommand& cmd);

    // --- Command handlers ---
    void Cmd_Execute(const std::vector<std::string>& args, int line);
    void Cmd_Quit();
    void Cmd_Alert(const std::vector<std::string>& args, int line);
    void Cmd_Stop(const std::vector<std::string>& args, int line);
    void Cmd_Position(const std::vector<std::string>& args, int line);
    void Cmd_PlayPosition(const std::vector<std::string>& args, int line);
    void Cmd_GetReady();
    void Cmd_Reset();
    void Cmd_Save();
    void Cmd_Load();
    void Cmd_DetectCollision(const std::vector<std::string>& args, int line);
    void Cmd_Wait(float seconds);
    void Cmd_Label(const std::vector<std::string>& args);
    void Cmd_Goto(const std::vector<std::string>& args, int line);

    // --- Execute sub-function registry ---
    using ExecFn = std::function<void(const std::vector<std::string>&)>;
    std::unordered_map<std::string, ExecFn> m_execRegistry;
    void RegisterExecuteFunctions();

    // --- Label / GOTO support ---
    std::unordered_map<std::string, size_t> m_labelMap;  // label name (upper) → m_commands index
    int m_jumpTarget = -1;                                // -1 = no jump pending
    void BuildLabelMap();
    static constexpr int kMaxExecutionSteps = 1'000'000; // infinite-loop guard

    // --- Collision evaluation ---
    bool EvaluateCollisionRule(const CollisionRule& rule);
    void TriggerCollisionAction(const std::string& actionStr);

    // --- Utilities ---
    void SetError(int line, const std::string& msg);
    static std::string SceneTypeToName(SceneType t);
    static float  SafeStof(const std::string& s, float  fallback = 0.0f);
    static int    SafeStoi(const std::string& s, int    fallback = 0);
    static bool   SafeStob(const std::string& s, bool   fallback = false);
    static std::string ToUpper(const std::string& s);
};

// Global instance declared alongside all other managers in main.cpp
extern ScriptManager scriptManager;
