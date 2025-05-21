/* -------------------------------------------------------------- */
// RenderScene.cpp
// 
// DO NOT INCLUDE THIS FILE, THE PROJECT SCOPES THIS FILE ITSELF!
/* -------------------------------------------------------------- */
#include "Includes.h"
#include "IOStreamThread.h"
#include "ThreadManager.h"
#include "WinSystem.h"
#include "Models.h"
#include "Lights.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "DX11Renderer.h"

using namespace SoundSystem;

// Music Playback
#if defined(__USE_MP3PLAYER__)
#include "WinMediaPlayer.h"
extern MediaPlayer player;
#elif defined(__USE_XMPLAYER__)
#include "XMMODPlayer.h"
extern XMMODPlayer xmPlayer;
#endif

// Other required external references.
extern ThreadManager threadManager;
extern SystemUtils sysUtils;
extern SceneManager scene;
extern SoundManager soundManager;
extern Model models[MAX_MODELS];
extern LightsManager lightsManager;
extern bool bResizing;

