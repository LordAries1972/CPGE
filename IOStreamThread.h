#pragma once

#include "Renderer.h"
//------------------------------------------
// XM / MP3 Modules
//------------------------------------------
#if defined(__USE_MP3PLAYER__)
inline const std::filesystem::path mp3FilePlaylist[] = { L"game1.mp3" };
inline const std::filesystem::path SingleMP3Filename = "game1.mp3";
inline const int MAX_MP3_MODULES = ARRAYSIZE(mp3FilePlaylist);
#elif defined(__USE_XMPLAYER__)
inline const std::filesystem::path xmFilePlaylist[] = { L"thevoid.xm", L"electro2.xm", L"battle.xm"};
inline const std::filesystem::path SingleXMFilename = "todie4.xm";
inline const std::filesystem::path IntroXMFilename = "thevoid.xm";
inline const int MAX_XM_MODULES = ARRAYSIZE(xmFilePlaylist);
#endif

