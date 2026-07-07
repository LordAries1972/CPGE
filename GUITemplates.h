#pragma once

//-------------------------------------------------------------------------------------------------
// GUITemplates.h - Reusable GUIWindow visual templates ("chrome")
//-------------------------------------------------------------------------------------------------
//
// A window template supplies pure visual styling (background colour, border, title bar) for a
// GUIWindow via its onCustomRender hook. Templates never add GUIControl entries — callers remain
// free to add whatever controls they need (text areas, inputs, buttons, etc.) on top of the
// chrome a template installs.
//
// Usage: pass a GUIWindowTemplateType to GUIManager::CreateMyWindow(), or call
// ApplyGUIWindowTemplate() directly on an existing GUIWindow.
//-------------------------------------------------------------------------------------------------

#include <memory>
#include <string>

class GUIWindow;

// Available window visual templates — extend this enum as new chrome styles are added.
enum class GUIWindowTemplateType {
    None,       // No template — caller is responsible for all visual chrome.
    Tech1,      // Military/tech HUD terminal look: black body, cyan-teal border frame,
                // red alert-style title bar with centred caption text.
    QuitWindow, // Modal confirmation look (lifted from CreateQuitConfirmDialog): semi-transparent
                // black body, magenta→purple gradient title bar with left-aligned yellow caption.
    Swarve1,    // Flat dark-navy app-window look (Windscribe-style): near-black navy body with
                // soft rounded corners and a thin lighter navy edge border. Frame only — no
                // title bar band or caption drawn; callers add any header content themselves.
    MultiSegment1, // "Restricted vehicle" placard look: four stacked horizontal bands — amber top
                   // band, amber body band, near-black info band, orange status band. Structure
                   // only — no icons, captions, or controls drawn; callers overlay their own.
    BasicCurved1,  // Dark rounded-corner panel (terminal/log-panel style): flat near-black body
                   // with all four corners properly rounded (drawn via DrawCurve) and a subtle
                   // border outline. Structure only — no captions, icons, or controls drawn;
                   // callers overlay their own content.
};

// Applies the chrome for the requested template to an already-created GUIWindow.
// Sets the window's background colour and installs an onCustomRender hook that draws the
// border/frame and title bar; does not add any GUIControl entries.
//
// titleText — optional caption drawn centred in the template's title bar (ignored by templates
//             that don't have one, e.g. GUIWindowTemplateType::None).
void ApplyGUIWindowTemplate(const std::shared_ptr<GUIWindow>& window,
                             GUIWindowTemplateType templateType,
                             const std::wstring& titleText = L"");
