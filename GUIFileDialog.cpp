
/* ---------------------------------------------------------------------------------------------------------
Description: GUIFileDialog.cpp

Implements Windows 11-style Load (Open) and Save dialog windows for the CPGE GUI system.
Centered on screen, fully 3D-rendered using Panel / TextInput / ListBox / ComboBox controls,
with Quick Access sidebar, filesystem navigation (back / forward / up), file-type filtering,
and filename editing.

Usage:
    guiManager.CreateLoadDialog(L"Open File", L"C:\\", filters, onConfirm, onCancel);
    guiManager.CreateSaveDialog(L"Save File As", L"C:\\", L"untitled.txt", filters, onConfirm, onCancel);

where filters = { { L"All Files (*.*)", L"*.*" },
                  { L"Text Files (*.txt)", L"*.txt" } }

--------------------------------------------------------------------------------------------------------- */

#include "Includes.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#if defined(PLATFORM_WINDOWS)
    #include <shlobj.h>     // SHGetFolderPathW, CSIDL_* constants
#endif

#if defined(__USE_OPENGL__)
    #include "OpenGLFXManager.h"
#elif defined(__USE_VULKAN__)
    #include "VULKAN_FXManager.h"
#else
    #include "DX_FXManager.h"
#endif

#include "GUIManager.h"
#include "Debug.h"
#include "SoundManager.h"

extern std::shared_ptr<Renderer> renderer;
extern Debug debug;
extern SoundManager soundManager;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal window name constants
// ---------------------------------------------------------------------------
static const std::string LOAD_WIN_NAME = "LoadFileDialog";
static const std::string SAVE_WIN_NAME = "SaveFileDialog";

// ---------------------------------------------------------------------------
// Control ID constants — used by lambdas to find controls by id
// ---------------------------------------------------------------------------
static const std::string FD_FILELIST   = "fd_filelist";
static const std::string FD_FILENAME   = "fd_filename";
static const std::string FD_FILTER     = "fd_filter";
static const std::string FD_ADDRESS    = "fd_address";
static const std::string FD_BACK       = "fd_nav_back";
static const std::string FD_FWD        = "fd_nav_fwd";
static const std::string FD_UP         = "fd_nav_up";
static const std::string FD_CONFIRM    = "fd_btn_confirm";
static const std::string FD_CANCEL     = "fd_btn_cancel";

// ---------------------------------------------------------------------------
// FileDialogState — shared between all lambdas via shared_ptr
// ---------------------------------------------------------------------------
struct FileDialogState {
    fs::path                                    currentPath;
    std::vector<fs::path>                       history;          // navigation history entries
    int                                         historyIdx = -1;  // current position in history
    std::vector<std::pair<std::wstring, bool>>  entries;          // {display_name, is_directory}
    std::vector<std::pair<std::wstring,std::wstring>> filters;    // {label, pattern}
    int                                         filterIdx = 0;
    bool                                        isLoadMode;
    std::string                                 winName;
    std::function<void(const std::wstring&)>    onConfirm;
    std::function<void()>                       onCancel;
};

// ---------------------------------------------------------------------------
// Helper: case-insensitive pattern match for simple *.ext style filters
// ---------------------------------------------------------------------------
static bool MatchesFilter(const std::wstring& name, const std::wstring& pattern)
{
    if (pattern == L"*.*" || pattern == L"*") return true;
    // Pattern must start with "*." for extension match
    if (pattern.size() >= 2 && pattern[0] == L'*' && pattern[1] == L'.') {
        std::wstring ext = pattern.substr(1);   // ".ext"
        auto pos = name.rfind(L'.');
        if (pos == std::wstring::npos) return ext == L".*"; // pattern *.* caught above
        std::wstring nameExt = name.substr(pos);
        // Case-insensitive compare
        for (auto& c : nameExt)  c = static_cast<wchar_t>(towlower(c));
        for (auto& c : ext)      c = static_cast<wchar_t>(towlower(c));
        return nameExt == ext;
    }
    return name == pattern;
}

// ---------------------------------------------------------------------------
// Helper: scan a directory; returns sorted {name, isDir} pairs
// Directories come first, then files, both alphabetically (case-insensitive).
// Hidden/system entries and access-denied paths are silently skipped.
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::wstring, bool>> ScanDirectory(const fs::path& dir)
{
    std::vector<std::pair<std::wstring, bool>> out;
    try {
        for (auto& entry : fs::directory_iterator(dir,
                fs::directory_options::skip_permission_denied)) {
            try {
                bool isDir = entry.is_directory();
                std::wstring name = entry.path().filename().wstring();
                if (name.empty()) continue;
                // Skip hidden files on Windows (starting with dot or with FILE_ATTRIBUTE_HIDDEN)
                #if defined(PLATFORM_WINDOWS)
                    DWORD attr = GetFileAttributesW(entry.path().c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES &&
                        (attr & FILE_ATTRIBUTE_HIDDEN) &&
                        (attr & FILE_ATTRIBUTE_SYSTEM)) continue;
                #endif
                out.push_back({ name, isDir });
            }
            catch (...) {}
        }
    }
    catch (...) {}

    // Sort: directories first, then files; both alphabetically (case-insensitive)
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        std::wstring aLow = a.first, bLow = b.first;
        for (auto& c : aLow) c = static_cast<wchar_t>(towlower(c));
        for (auto& c : bLow) c = static_cast<wchar_t>(towlower(c));
        return aLow < bLow;
    });
    return out;
}

// ---------------------------------------------------------------------------
// Find a control by id inside a window (returns nullptr if not found)
// ---------------------------------------------------------------------------
static GUIControl* FindControl(std::shared_ptr<GUIWindow>& win, const std::string& id)
{
    for (auto& c : win->controls)
        if (c.id == id) return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Refresh the file list and address bar after any navigation change.
// Also updates back/forward button enabled colours.
// ---------------------------------------------------------------------------
static void RefreshFileList(std::shared_ptr<GUIWindow> win,
                            std::shared_ptr<FileDialogState> state)
{
    if (!win || win->bWindowDestroy) return;

    // Re-scan current directory
    state->entries = ScanDirectory(state->currentPath);
    const std::wstring& pat = state->filters[state->filterIdx].second;

    // Build display strings for the ListBox
    std::vector<std::wstring> items;
    for (auto& [name, isDir] : state->entries) {
        if (isDir) {
            items.push_back(L"► " + name);   // ► folder
        } else if (MatchesFilter(name, pat)) {
            items.push_back(L"    " + name);       // indent for files
        }
    }

    // Update ListBox
    if (auto* lb = FindControl(win, FD_FILELIST)) {
        lb->items            = items;
        lb->selectedIndex    = -1;
        lb->listScrollOffset = 0;
    }

    // Update address bar
    if (auto* addr = FindControl(win, FD_ADDRESS)) {
        addr->label = state->currentPath.wstring();
    }

    // Update nav button text colours (greyed when unavailable)
    MyColor enabledCol(200, 210, 230, 255);
    MyColor disabledCol(70, 75, 95, 255);

    if (auto* back = FindControl(win, FD_BACK))
        back->txtColor = (state->historyIdx > 0) ? enabledCol : disabledCol;
    if (auto* fwd = FindControl(win, FD_FWD))
        fwd->txtColor = (state->historyIdx < (int)state->history.size() - 1)
                        ? enabledCol : disabledCol;
}

// ---------------------------------------------------------------------------
// Navigate to a new path, pushing it onto the history stack
// ---------------------------------------------------------------------------
static void NavigateTo(std::shared_ptr<GUIWindow> win,
                       std::shared_ptr<FileDialogState> state,
                       const fs::path& newPath)
{
    if (!fs::is_directory(newPath)) return;

    // Truncate forward history beyond current position
    if (state->historyIdx < (int)state->history.size() - 1)
        state->history.erase(state->history.begin() + state->historyIdx + 1,
                             state->history.end());

    state->history.push_back(newPath);
    state->historyIdx = static_cast<int>(state->history.size()) - 1;
    state->currentPath = newPath;
    RefreshFileList(win, state);
}

// ---------------------------------------------------------------------------
// Quick-access item helper
// ---------------------------------------------------------------------------
static fs::path QuickAccessPath(const std::wstring& label)
{
    #if defined(PLATFORM_WINDOWS)
        wchar_t buf[MAX_PATH] = {};
        if (label == L"Desktop")
            SHGetFolderPathW(nullptr, CSIDL_DESKTOP,    nullptr, 0, buf);
        else if (label == L"Documents")
            SHGetFolderPathW(nullptr, CSIDL_PERSONAL,   nullptr, 0, buf);
        else if (label == L"Downloads") {
            // Downloads folder lives inside the user profile directory
            wchar_t profile[MAX_PATH] = {};
            SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile);
            return fs::path(std::wstring(profile) + L"\\Downloads");
        }
        else if (label == L"Pictures")
            SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, buf);
        else if (label == L"Music")
            SHGetFolderPathW(nullptr, CSIDL_MYMUSIC,    nullptr, 0, buf);
        else if (label == L"This PC")
            return fs::path(L"C:\\");
        return fs::path(buf[0] ? buf : L"C:\\");
    #else
        // Non-Windows fallback — map labels to XDG home subdirectories
        const char* home = getenv("HOME");
        if (!home) home = "/";
        if (label == L"Desktop")   return fs::path(std::string(home) + "/Desktop");
        if (label == L"Documents") return fs::path(std::string(home) + "/Documents");
        if (label == L"Downloads") return fs::path(std::string(home) + "/Downloads");
        if (label == L"Pictures")  return fs::path(std::string(home) + "/Pictures");
        if (label == L"Music")     return fs::path(std::string(home) + "/Music");
        return fs::path("/");
    #endif
}

// ---------------------------------------------------------------------------
// Shared implementation body — called by both CreateLoadDialog and CreateSaveDialog
// ---------------------------------------------------------------------------
static void CreateFileDialogImpl(GUIManager* mgr,
                                 const std::string& winName,
                                 const std::wstring& title,
                                 const std::wstring& initialPath,
                                 const std::wstring& defaultFilename,
                                 const std::vector<std::pair<std::wstring,std::wstring>>& filters,
                                 bool isLoadMode,
                                 std::function<void(const std::wstring&)> onConfirm,
                                 std::function<void()> onCancel)
{
    if (!renderer) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateFileDialog - renderer is null");
        return;
    }

    // Guard against duplicate open dialogs
    if (mgr->GetWindow(winName)) {
        mgr->BringWindowToFront(winName);
        return;
    }

    // --- Window dimensions and centred position ---
    const float WW = 720.0f;
    const float WH = 480.0f;
    const float WX = std::floor((static_cast<float>(renderer->iOrigWidth)  - WW) * 0.5f);
    const float WY = std::floor((static_cast<float>(renderer->iOrigHeight) - WH) * 0.5f);

    mgr->CreateMyWindow(winName, GUIWindowType::Dialog,
        Vector2(WX, WY), Vector2(WW, WH),
        MyColor(30, 34, 48, 255),
        int(BlitObj2DIndexType::NONE));

    auto win = mgr->GetWindow(winName);
    if (!win) return;

    win->isVisible = false;     // hide until all controls are added
    win->isModal   = true;

    // --- Shared state ---
    auto state            = std::make_shared<FileDialogState>();
    state->isLoadMode     = isLoadMode;
    state->winName        = winName;
    state->onConfirm      = std::move(onConfirm);
    state->onCancel       = std::move(onCancel);
    state->filters        = filters.empty()
                            ? std::vector<std::pair<std::wstring,std::wstring>>{{ L"All Files (*.*)", L"*.*" }}
                            : filters;
    state->filterIdx      = 0;

    // Resolve initial path
    fs::path startPath;
    try { startPath = fs::path(initialPath); }
    catch (...) { startPath = fs::path(L"C:\\"); }
    if (!fs::is_directory(startPath))
        startPath = startPath.parent_path();
    if (!fs::is_directory(startPath))
        startPath = fs::path(L"C:\\");

    state->currentPath = startPath;
    state->history.push_back(startPath);
    state->historyIdx  = 0;

    std::weak_ptr<GUIWindow> weakWin  = win;
    std::weak_ptr<FileDialogState> weakState = state;

    // =======================================================================
    // Layout coordinates (all relative to WX/WY)
    // =======================================================================
    //   0–28    : title bar + close button
    //  28–56    : navigation bar (back, forward, up, address label)
    //  56–378   : sidebar (150px) + file list (570px)
    //             56–80 : column headers + sidebar header
    //             80–378: file list rows + sidebar items
    // 378–380   : separator line
    // 380–406   : filename row  (label + TextInput)
    // 406–432   : file type row (label + ComboBox)
    // 446–476   : buttons row   (Confirm + Cancel)
    // =======================================================================

    const float SIDE_W = 152.0f;    // sidebar width including its right border
    const float LIST_X = WX + SIDE_W;
    const float LIST_W = WW - SIDE_W - 2.0f;   // 2px right margin inside window

    // =======================================================================
    // 3D BACKGROUND CHROME PANELS (rendered first = behind everything else)
    // =======================================================================

    // Outer window drop-shadow (simulated by a slightly-offset dark semi-transparent panel)
    {
        GUIControl shadow;
        shadow.type      = GUIControlType::Panel;
        shadow.id        = "fd_win_shadow";
        shadow.position  = Vector2(WX + 6.0f, WY + 8.0f);
        shadow.size      = Vector2(WW, WH);
        shadow.bgColor   = MyColor(0, 0, 0, 95);
        shadow.sliderValue = 1.0f;   // "raised" so it uses drop-shadow path (but colour makes it dark)
        shadow.isVisible = true;
        win->AddControl(shadow);
    }

    // Navigation bar panel
    {
        GUIControl navPanel;
        navPanel.type      = GUIControlType::Panel;
        navPanel.id        = "fd_nav_panel";
        navPanel.position  = Vector2(WX, WY + 28.0f);
        navPanel.size      = Vector2(WW, 28.0f);
        navPanel.bgColor   = MyColor(38, 42, 58, 255);
        navPanel.sliderValue = 1.0f;  // raised
        navPanel.isVisible = true;
        win->AddControl(navPanel);
    }

    // Left sidebar panel (raised)
    {
        GUIControl sidePanel;
        sidePanel.type     = GUIControlType::Panel;
        sidePanel.id       = "fd_side_panel";
        sidePanel.position = Vector2(WX, WY + 56.0f);
        sidePanel.size     = Vector2(SIDE_W - 2.0f, WH - 56.0f - 2.0f);
        sidePanel.bgColor  = MyColor(28, 32, 44, 255);
        sidePanel.sliderValue = 1.0f;  // raised
        sidePanel.isVisible = true;
        win->AddControl(sidePanel);
    }

    // File list sunken panel (to provide visual depth for the list box)
    {
        GUIControl listPanel;
        listPanel.type     = GUIControlType::Panel;
        listPanel.id       = "fd_list_panel";
        listPanel.position = Vector2(LIST_X - 2.0f, WY + 56.0f);
        listPanel.size     = Vector2(LIST_W + 4.0f, WH - 56.0f - 102.0f);
        listPanel.bgColor  = MyColor(14, 17, 26, 255);
        listPanel.sliderValue = 0.0f;  // sunken
        listPanel.isVisible = true;
        win->AddControl(listPanel);
    }

    // Bottom section panel (raised — file name, filter, buttons)
    {
        GUIControl botPanel;
        botPanel.type     = GUIControlType::Panel;
        botPanel.id       = "fd_bot_panel";
        botPanel.position = Vector2(WX, WY + WH - 102.0f);
        botPanel.size     = Vector2(WW, 102.0f);
        botPanel.bgColor  = MyColor(32, 36, 52, 255);
        botPanel.sliderValue = 1.0f;  // raised
        botPanel.isVisible = true;
        win->AddControl(botPanel);
    }

    // Column header strip (slightly raised)
    {
        GUIControl hdrPanel;
        hdrPanel.type     = GUIControlType::Panel;
        hdrPanel.id       = "fd_hdr_panel";
        hdrPanel.position = Vector2(LIST_X, WY + 56.0f);
        hdrPanel.size     = Vector2(LIST_W, 24.0f);
        hdrPanel.bgColor  = MyColor(36, 40, 55, 255);
        hdrPanel.sliderValue = 1.0f;  // raised
        hdrPanel.isVisible = true;
        win->AddControl(hdrPanel);
    }

    // Thin vertical separator between sidebar and file list
    {
        GUIControl sep;
        sep.type     = GUIControlType::Panel;
        sep.id       = "fd_sep_vert";
        sep.position = Vector2(WX + SIDE_W - 2.0f, WY + 28.0f);
        sep.size     = Vector2(2.0f, WH - 130.0f);
        sep.bgColor  = MyColor(10, 12, 20, 255);
        sep.sliderValue = 0.0f;  // sunken (dark divider)
        sep.isVisible = true;
        win->AddControl(sep);
    }

    // =======================================================================
    // TITLE BAR
    // =======================================================================
    {
        GUIControl titleBar;
        titleBar.type             = GUIControlType::TitleBar;
        titleBar.id               = "fd_titlebar";
        titleBar.position         = Vector2(WX, WY);
        titleBar.size             = Vector2(WW - (CLOSEWINBUTTON_SIZE + 6.0f), TITLEBAR_HEIGHT);
        titleBar.bgColor          = MyColor(20, 24, 38, 255);
        titleBar.txtColor         = MyColor(210, 220, 240, 255);
        titleBar.bgTextureId      = int(BlitObj2DIndexType::IMG_TITLEBAR1);
        titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
        titleBar.label            = title;
        titleBar.lblFontSize      = 14.0f;
        titleBar.lblCenterH       = false;   // left-aligned title like Windows 11
        titleBar.isVisible        = true;

        std::weak_ptr<GUIWindow> weakWin2 = win;
        titleBar.onMouseBtnDown = [weakWin2]() {
            if (auto w = weakWin2.lock()) w->isDragging = true;
        };
        titleBar.onMouseBtnUp = [weakWin2]() {
            if (auto w = weakWin2.lock()) w->isDragging = false;
        };
        win->AddControl(titleBar);
    }

    // Close [X] button
    {
        GUIControl btnClose;
        btnClose.type             = GUIControlType::Button;
        btnClose.id               = "fd_close";
        btnClose.position         = Vector2(WX + WW - (CLOSEWINBUTTON_SIZE + 4.0f), WY + 4.0f);
        btnClose.size             = Vector2(CLOSEWINBUTTON_SIZE, CLOSEWINBUTTON_SIZE);
        btnClose.bgColor          = MyColor(120, 20, 20, 255);
        btnClose.txtColor         = MyColor(80, 0, 0, 255);
        btnClose.bgTextureId      = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
        btnClose.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
        btnClose.label            = L"";
        btnClose.lblFontSize      = 8.0f;
        btnClose.isVisible        = true;
        btnClose.onMouseBtnDown = [mgr, winName, weakState]() {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            if (auto st = weakState.lock(); st && st->onCancel)
                st->onCancel();
            mgr->RemoveWindow(winName);
        };
        win->AddControl(btnClose);
    }

    // =======================================================================
    // NAVIGATION BAR
    // =======================================================================
    const float NAV_Y     = WY + 28.0f;
    const float NAV_BTN_H = 20.0f;
    const float NAV_BTN_W = 26.0f;

    // Back button ◄
    {
        GUIControl btnBack;
        btnBack.type             = GUIControlType::Button;
        btnBack.id               = FD_BACK;
        btnBack.position         = Vector2(WX + 4.0f, NAV_Y + 4.0f);
        btnBack.size             = Vector2(NAV_BTN_W, NAV_BTN_H);
        btnBack.bgColor          = MyColor(28, 32, 48, 255);
        btnBack.hoverColor       = MyColor(50, 58, 82, 255);
        btnBack.txtColor         = MyColor(70, 75, 95, 255);   // starts disabled
        btnBack.bgTextureId      = int(BlitObj2DIndexType::NONE);
        btnBack.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        btnBack.label            = L"◄";
        btnBack.lblFontSize      = 10.0f;
        btnBack.isVisible        = true;
        btnBack.onMouseBtnDown   = [mgr, weakWin, weakState, winName]() {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2) return;
            if (state2->historyIdx > 0) {
                --state2->historyIdx;
                state2->currentPath = state2->history[state2->historyIdx];
                RefreshFileList(win2, state2);
            }
        };
        win->AddControl(btnBack);
    }

    // Forward button ►
    {
        GUIControl btnFwd;
        btnFwd.type             = GUIControlType::Button;
        btnFwd.id               = FD_FWD;
        btnFwd.position         = Vector2(WX + 4.0f + NAV_BTN_W + 2.0f, NAV_Y + 4.0f);
        btnFwd.size             = Vector2(NAV_BTN_W, NAV_BTN_H);
        btnFwd.bgColor          = MyColor(28, 32, 48, 255);
        btnFwd.hoverColor       = MyColor(50, 58, 82, 255);
        btnFwd.txtColor         = MyColor(70, 75, 95, 255);   // starts disabled
        btnFwd.bgTextureId      = int(BlitObj2DIndexType::NONE);
        btnFwd.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        btnFwd.label            = L"►";
        btnFwd.lblFontSize      = 10.0f;
        btnFwd.isVisible        = true;
        btnFwd.onMouseBtnDown   = [mgr, weakWin, weakState, winName]() {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2) return;
            if (state2->historyIdx < (int)state2->history.size() - 1) {
                ++state2->historyIdx;
                state2->currentPath = state2->history[state2->historyIdx];
                RefreshFileList(win2, state2);
            }
        };
        win->AddControl(btnFwd);
    }

    // Up button ▲
    {
        GUIControl btnUp;
        btnUp.type             = GUIControlType::Button;
        btnUp.id               = FD_UP;
        btnUp.position         = Vector2(WX + 4.0f + (NAV_BTN_W + 2.0f) * 2.0f, NAV_Y + 4.0f);
        btnUp.size             = Vector2(NAV_BTN_W, NAV_BTN_H);
        btnUp.bgColor          = MyColor(28, 32, 48, 255);
        btnUp.hoverColor       = MyColor(50, 58, 82, 255);
        btnUp.txtColor         = MyColor(200, 210, 230, 255);
        btnUp.bgTextureId      = int(BlitObj2DIndexType::NONE);
        btnUp.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        btnUp.label            = L"▲";
        btnUp.lblFontSize      = 10.0f;
        btnUp.isVisible        = true;
        btnUp.onMouseBtnDown   = [mgr, weakWin, weakState, winName]() {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2) return;
            fs::path parent = state2->currentPath.parent_path();
            if (parent != state2->currentPath)
                NavigateTo(win2, state2, parent);
        };
        win->AddControl(btnUp);
    }

    // Address bar (read-only label, sunken TextArea)
    {
        float addrX = WX + 4.0f + (NAV_BTN_W + 2.0f) * 3.0f + 4.0f;
        float addrW = WW - (addrX - WX) - 8.0f;

        GUIControl addr;
        addr.type             = GUIControlType::TextArea;
        addr.id               = FD_ADDRESS;
        addr.position         = Vector2(addrX, NAV_Y + 3.0f);
        addr.size             = Vector2(addrW, 22.0f);
        addr.bgColor          = MyColor(12, 15, 24, 220);
        addr.hoverColor       = MyColor(12, 15, 24, 220);
        addr.bgTextureId      = int(BlitObj2DIndexType::NONE);
        addr.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        addr.txtColor         = MyColor(175, 185, 210, 255);
        addr.lblFontSize      = 11.0f;
        addr.label            = state->currentPath.wstring();
        addr.isVisible        = true;
        win->AddControl(addr);
    }

    // =======================================================================
    // QUICK ACCESS SIDEBAR (left of file list)
    // =======================================================================
    static const wchar_t* QUICK_LABELS[] = {
        L"Desktop", L"Documents", L"Downloads",
        L"Pictures", L"Music", L"This PC"
    };
    constexpr int QUICK_COUNT = 6;
    const float SIDE_ITEM_H = 26.0f;
    const float SIDE_ITEM_X = WX + 4.0f;
    const float SIDE_ITEM_W = SIDE_W - 10.0f;

    // "Quick access" header label
    {
        GUIControl hdr;
        hdr.type      = GUIControlType::TextArea;
        hdr.id        = "fd_qa_hdr";
        hdr.position  = Vector2(WX + 6.0f, WY + 58.0f);
        hdr.size      = Vector2(SIDE_W - 12.0f, 18.0f);
        hdr.bgColor   = MyColor(0, 0, 0, 0);
        hdr.hoverColor= MyColor(0, 0, 0, 0);
        hdr.bgTextureId = int(BlitObj2DIndexType::NONE);
        hdr.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        hdr.txtColor  = MyColor(155, 165, 190, 255);
        hdr.lblFontSize = 9.5f;
        hdr.label     = L"Quick access";
        hdr.isVisible = true;
        win->AddControl(hdr);
    }

    for (int qi = 0; qi < QUICK_COUNT; ++qi) {
        GUIControl qa;
        qa.type             = GUIControlType::Button;
        qa.id               = "fd_qa_" + std::to_string(qi);
        qa.position         = Vector2(SIDE_ITEM_X, WY + 80.0f + qi * SIDE_ITEM_H);
        qa.size             = Vector2(SIDE_ITEM_W, SIDE_ITEM_H - 2.0f);
        qa.bgColor          = MyColor(0, 0, 0, 0);
        qa.hoverColor       = MyColor(45, 52, 72, 200);
        qa.bgTextureId      = int(BlitObj2DIndexType::NONE);
        qa.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        qa.txtColor         = MyColor(185, 195, 218, 255);
        qa.lblFontSize      = 11.0f;
        qa.lblCenterH       = false;
        qa.label            = std::wstring(L"  ") + QUICK_LABELS[qi];
        qa.isVisible        = true;

        std::wstring lbl = QUICK_LABELS[qi];
        qa.onMouseBtnDown = [mgr, weakWin, weakState, lbl]() {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2) return;
            fs::path dest = QuickAccessPath(lbl);
            NavigateTo(win2, state2, dest);
        };
        win->AddControl(qa);
    }

    // =======================================================================
    // COLUMN HEADERS (Name | Date Modified | Type | Size)
    // =======================================================================
    struct ColDef { const wchar_t* label; float xOff; float w; };
    static const ColDef COLS[] = {
        { L"  Name",          0.0f,  280.0f },
        { L"Date Modified", 282.0f,  140.0f },
        { L"Type",          424.0f,   80.0f },
        { L"Size",          506.0f,   56.0f },
    };
    for (auto& col : COLS) {
        GUIControl hc;
        hc.type      = GUIControlType::TextArea;
        hc.id        = "fd_col_" + std::string(col.label[0] == L' ' ? col.label + 2 : col.label,
                                                col.label + wcslen(col.label));
        hc.position  = Vector2(LIST_X + col.xOff, WY + 58.0f);
        hc.size      = Vector2(col.w, 20.0f);
        hc.bgColor   = MyColor(0, 0, 0, 0);
        hc.hoverColor= MyColor(0, 0, 0, 0);
        hc.bgTextureId = int(BlitObj2DIndexType::NONE);
        hc.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        hc.txtColor  = MyColor(165, 175, 200, 255);
        hc.lblFontSize = 10.5f;
        hc.label     = col.label;
        hc.isVisible = true;
        win->AddControl(hc);
    }

    // =======================================================================
    // FILE LIST (ListBox)
    // =======================================================================
    {
        GUIControl lb;
        lb.type             = GUIControlType::ListBox;
        lb.id               = FD_FILELIST;
        lb.position         = Vector2(LIST_X, WY + 80.0f);
        lb.size             = Vector2(LIST_W, WH - 80.0f - 102.0f - 2.0f);
        lb.bgColor          = MyColor(14, 17, 27, 255);
        lb.hoverColor       = MyColor(14, 17, 27, 255);
        lb.bgTextureId      = int(BlitObj2DIndexType::NONE);
        lb.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        lb.txtColor         = MyColor(195, 200, 218, 255);
        lb.lblFontSize      = 11.5f;
        lb.listItemHeight   = 22;
        lb.selectedIndex    = -1;
        lb.isVisible        = true;

        // On selection: if it's a directory navigate into it on double-logic
        // (single click populates the filename bar)
        lb.onSelectionChanged = [weakWin, weakState](int idx) {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2 || idx < 0) return;

            auto* lb2 = FindControl(win2, FD_FILELIST);
            if (!lb2 || idx >= (int)lb2->items.size()) return;

            // Strip the ► prefix to get the raw name
            std::wstring raw = lb2->items[idx];
            bool isDir = (!raw.empty() && raw[0] == L'\u25BA');  // U+25BA BLACK RIGHT-POINTING POINTER
            // Remove prefix chars
            while (!raw.empty() && (raw[0] == L'\u25BA' || raw[0] == L' '))
                raw.erase(raw.begin());

            if (isDir) {
                // Navigate into the directory on selection
                NavigateTo(win2, state2, state2->currentPath / raw);
            } else {
                // Populate the filename TextInput with the selected file name
                if (auto* fi = FindControl(win2, FD_FILENAME)) {
                    fi->inputText = raw;
                    fi->cursorPos = static_cast<int>(raw.size());
                    if (fi->onTextChanged) fi->onTextChanged(raw);
                }
            }
        };

        // Perform initial directory scan
        state->entries = ScanDirectory(state->currentPath);
        const std::wstring& pat = state->filters[state->filterIdx].second;
        for (auto& [name, isDir2] : state->entries) {
            if (isDir2)
                lb.items.push_back(L"► " + name);
            else if (MatchesFilter(name, pat))
                lb.items.push_back(L"    " + name);
        }

        win->AddControl(lb);
    }

    // =======================================================================
    // BOTTOM SECTION
    // =======================================================================
    const float BOT_Y   = WY + WH - 100.0f;
    const float LBL_X   = WX + 8.0f;
    const float LBL_W   = 106.0f;
    const float FIELD_X = LBL_X + LBL_W + 4.0f;
    const float FIELD_W = WW - (FIELD_X - WX) - 10.0f;

    // "File name:" label
    {
        GUIControl lbl;
        lbl.type     = GUIControlType::TextArea;
        lbl.id       = "fd_lbl_name";
        lbl.position = Vector2(LBL_X, BOT_Y + 8.0f);
        lbl.size     = Vector2(LBL_W, 24.0f);
        lbl.bgColor  = MyColor(0, 0, 0, 0);
        lbl.hoverColor = MyColor(0, 0, 0, 0);
        lbl.bgTextureId = int(BlitObj2DIndexType::NONE);
        lbl.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        lbl.txtColor = MyColor(195, 205, 225, 255);
        lbl.lblFontSize = 12.0f;
        lbl.label    = L"File name:";
        lbl.isVisible = true;
        win->AddControl(lbl);
    }

    // Filename TextInput
    {
        GUIControl fi;
        fi.type             = GUIControlType::TextInput;
        fi.id               = FD_FILENAME;
        fi.position         = Vector2(FIELD_X, BOT_Y + 6.0f);
        fi.size             = Vector2(FIELD_W, 26.0f);
        fi.bgColor          = MyColor(14, 17, 27, 255);
        fi.hoverColor       = MyColor(14, 17, 27, 255);
        fi.bgTextureId      = int(BlitObj2DIndexType::NONE);
        fi.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        fi.txtColor         = MyColor(215, 222, 240, 255);
        fi.lblFontSize      = 12.0f;
        fi.inputText        = defaultFilename;
        fi.cursorPos        = static_cast<int>(defaultFilename.size());
        fi.isFocused        = true;   // default focus on filename field
        fi.maxInputLength   = 260;
        fi.placeholder      = L"Enter filename...";
        fi.isVisible        = true;
        win->AddControl(fi);
    }

    // "Files of type:" label
    {
        GUIControl lbl;
        lbl.type     = GUIControlType::TextArea;
        lbl.id       = "fd_lbl_type";
        lbl.position = Vector2(LBL_X, BOT_Y + 38.0f);
        lbl.size     = Vector2(LBL_W, 24.0f);
        lbl.bgColor  = MyColor(0, 0, 0, 0);
        lbl.hoverColor = MyColor(0, 0, 0, 0);
        lbl.bgTextureId = int(BlitObj2DIndexType::NONE);
        lbl.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        lbl.txtColor = MyColor(195, 205, 225, 255);
        lbl.lblFontSize = 12.0f;
        lbl.label    = L"Files of type:";
        lbl.isVisible = true;
        win->AddControl(lbl);
    }

    // File type ComboBox
    {
        GUIControl cb;
        cb.type             = GUIControlType::ComboBox;
        cb.id               = FD_FILTER;
        cb.position         = Vector2(FIELD_X, BOT_Y + 36.0f);
        cb.size             = Vector2(FIELD_W, 26.0f);
        cb.bgColor          = MyColor(14, 17, 27, 255);
        cb.hoverColor       = MyColor(22, 26, 40, 255);
        cb.bgTextureId      = int(BlitObj2DIndexType::NONE);
        cb.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        cb.txtColor         = MyColor(205, 212, 232, 255);
        cb.lblFontSize      = 11.5f;
        cb.dropdownMaxRows  = static_cast<int>(std::min((int)state->filters.size(), 8));
        cb.listItemHeight   = 22;
        cb.isVisible        = true;

        for (auto& [label, _pattern] : state->filters)
            cb.items.push_back(label);
        cb.selectedIndex = state->filterIdx;

        cb.onSelectionChanged = [weakWin, weakState](int idx) {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2) return;
            state2->filterIdx = idx;
            RefreshFileList(win2, state2);
        };
        win->AddControl(cb);
    }

    // =======================================================================
    // CONFIRM (Open / Save) BUTTON
    // =======================================================================
    const std::wstring confirmLabel = isLoadMode ? L"Open" : L"Save";
    const float BTN_Y  = BOT_Y + 68.0f;
    const float BTN_W  = 100.0f;
    const float BTN_H  = 28.0f;

    {
        GUIControl btnConfirm;
        btnConfirm.type             = GUIControlType::Button;
        btnConfirm.id               = FD_CONFIRM;
        btnConfirm.position         = Vector2(WX + WW - (BTN_W + 4.0f) * 2.0f - 4.0f, BTN_Y);
        btnConfirm.size             = Vector2(BTN_W, BTN_H);
        btnConfirm.bgColor          = MyColor(30, 75, 155, 255);   // primary blue
        btnConfirm.hoverColor       = MyColor(45, 100, 190, 255);
        btnConfirm.txtColor         = MyColor(230, 238, 255, 255);
        btnConfirm.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTONUP1);
        btnConfirm.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
        btnConfirm.label            = confirmLabel;
        btnConfirm.lblFontSize      = 13.0f;
        btnConfirm.bold             = true;
        btnConfirm.isVisible        = true;

        btnConfirm.onMouseBtnDown = [mgr, weakWin, weakState, winName]() {
            auto win2  = weakWin.lock();
            auto state2= weakState.lock();
            if (!win2 || !state2) return;

            // Read the filename from the TextInput
            std::wstring fname;
            if (auto* fi = FindControl(win2, FD_FILENAME))
                fname = fi->inputText;

            if (fname.empty()) return;  // require a name

            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // Build full path
            fs::path fullPath = state2->currentPath / fname;
            std::wstring result = fullPath.wstring();

            // Fire callback, then close
            if (state2->onConfirm) state2->onConfirm(result);
            mgr->RemoveWindow(winName);
        };
        win->AddControl(btnConfirm);
    }

    // =======================================================================
    // CANCEL BUTTON
    // =======================================================================
    {
        GUIControl btnCancel;
        btnCancel.type             = GUIControlType::Button;
        btnCancel.id               = FD_CANCEL;
        btnCancel.position         = Vector2(WX + WW - BTN_W - 8.0f, BTN_Y);
        btnCancel.size             = Vector2(BTN_W, BTN_H);
        btnCancel.bgColor          = MyColor(38, 42, 60, 255);
        btnCancel.hoverColor       = MyColor(55, 62, 88, 255);
        btnCancel.txtColor         = MyColor(200, 208, 225, 255);
        btnCancel.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTONUP1);
        btnCancel.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
        btnCancel.label            = L"Cancel";
        btnCancel.lblFontSize      = 13.0f;
        btnCancel.isVisible        = true;

        btnCancel.onMouseBtnDown = [mgr, weakState, winName]() {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            if (auto st = weakState.lock(); st && st->onCancel)
                st->onCancel();
            mgr->RemoveWindow(winName);
        };
        win->AddControl(btnCancel);
    }

    // =======================================================================
    // KEYBOARD ROUTING
    // Char input and backspace route to the focused TextInput (filename field).
    // Enter key fires the Confirm action.
    // =======================================================================
    win->onCharInput = [weakWin](wchar_t ch) {
        auto win2 = weakWin.lock();
        if (!win2) return;
        for (auto& ctrl : win2->controls) {
            if (ctrl.type != GUIControlType::TextInput || !ctrl.isFocused) continue;
            if ((int)ctrl.inputText.size() >= ctrl.maxInputLength) return;
            ctrl.inputText.insert(ctrl.inputText.begin() + ctrl.cursorPos, ch);
            ++ctrl.cursorPos;
            if (ctrl.onTextChanged) ctrl.onTextChanged(ctrl.inputText);
            return;
        }
    };

    win->onBackspace = [weakWin]() {
        auto win2 = weakWin.lock();
        if (!win2) return;
        for (auto& ctrl : win2->controls) {
            if (ctrl.type != GUIControlType::TextInput || !ctrl.isFocused) continue;
            if (ctrl.cursorPos > 0 && !ctrl.inputText.empty()) {
                ctrl.inputText.erase(ctrl.inputText.begin() + ctrl.cursorPos - 1);
                --ctrl.cursorPos;
                if (ctrl.onTextChanged) ctrl.onTextChanged(ctrl.inputText);
            }
            return;
        }
    };

    win->onEnter = [mgr, weakWin, weakState, winName]() {
        auto win2  = weakWin.lock();
        auto state2= weakState.lock();
        if (!win2 || !state2) return;

        std::wstring fname;
        if (auto* fi = FindControl(win2, FD_FILENAME))
            fname = fi->inputText;
        if (fname.empty()) return;

        fs::path fullPath = state2->currentPath / fname;
        if (state2->isLoadMode && fs::is_directory(fullPath)) {
            // Enter on a directory name in load mode = navigate into it
            NavigateTo(win2, state2, fullPath);
            if (auto* fi = FindControl(win2, FD_FILENAME))
                fi->inputText.clear();
            return;
        }

        if (state2->onConfirm) state2->onConfirm(fullPath.wstring());
        mgr->RemoveWindow(winName);
    };

    // All controls added — now show the window
    win->isVisible = true;

    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"CreateFileDialog - %s dialog created ('%s') with %d controls",
        isLoadMode ? L"Load" : L"Save",
        title.c_str(),
        static_cast<int>(win->controls.size()));
}

// ---------------------------------------------------------------------------
// Public API — GUIManager methods
// ---------------------------------------------------------------------------

void GUIManager::CreateLoadDialog(
    const std::wstring& title,
    const std::wstring& initialPath,
    const std::vector<std::pair<std::wstring,std::wstring>>& filters,
    std::function<void(const std::wstring&)> onConfirm,
    std::function<void()> onCancel)
{
    CreateFileDialogImpl(this, LOAD_WIN_NAME, title, initialPath, L"",
                         filters, true,
                         std::move(onConfirm), std::move(onCancel));
}

void GUIManager::CreateSaveDialog(
    const std::wstring& title,
    const std::wstring& initialPath,
    const std::wstring& defaultFilename,
    const std::vector<std::pair<std::wstring,std::wstring>>& filters,
    std::function<void(const std::wstring&)> onConfirm,
    std::function<void()> onCancel)
{
    CreateFileDialogImpl(this, SAVE_WIN_NAME, title, initialPath, defaultFilename,
                         filters, false,
                         std::move(onConfirm), std::move(onCancel));
}
