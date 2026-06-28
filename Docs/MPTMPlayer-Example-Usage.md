# MPTMPlayer Class - Usage Guide

## Overview

`MPTMPlayer` plays OpenMPT / ModPlug Tracker `.mptm` modules through the same DirectSound-backed playback model used by `XMMODPlayer` and `S3MPlayer`.

MPTM is an OpenMPT extended IT-family format, so the player loads the `IMPM` container, IT packed patterns, IT samples, optional instrument note/sample maps, and the IT / OpenMPT command set.

## Basic Usage

```cpp
#include "MPTMPlayer.h"

void PlayMPTMExample() {
    MPTMPlayer player;

    if (player.Play(L"Assets\\music.mptm")) {
        player.SetVolume(48);
        player.SetGlobalVolume(64);

        while (player.IsPlaying()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

## Controls

```cpp
MPTMPlayer player;

player.Play(L"Assets\\song.mptm");
player.Pause();
player.Resume();
player.SetFadeIn(1000);
player.SetFadeOut(1000);
player.GotoSequenceID(4);
player.Stop();
player.Shutdown();
```

## Supported Playback Features

- 44.1 kHz, 16-bit stereo DirectSound output on Windows.
- IT / MPTM `IMPM` headers, order lists, packed pattern rows, sample headers, and sample data.
- 8-bit and 16-bit PCM sample playback.
- Mono and stereo source samples, mixed to engine stereo output.
- Forward sample loops.
- Instrument note/sample map lookup for normal IT/MPTM instruments.
- External sample references (OpenMPT "Samples" folder layout, e.g. `.\Samples\Name.flac`),
  decoded through Media Foundation and resolved relative to the module file.
- Per-channel volume, panning, global volume, fade-in, fade-out, pause, hard pause, resume, and pattern navigation.

## Implemented Effects

- `Axx` speed
- `Bxx` position jump
- `Cxx` pattern break
- `Dxy` volume slide
- `Exx` portamento down, including fine/extra-fine variants
- `Fxx` portamento up, including fine/extra-fine variants
- `Gxx` tone portamento
- `Hxy` vibrato
- `Ixy` tremor
- `Jxy` arpeggio
- `Kxy` vibrato + volume slide
- `Lxy` tone portamento + volume slide
- Mxx channel volume approximated onto the active voice
- `Nxy` channel volume slide approximated onto the active voice
- `Oxx` sample offset with `SAy` high-offset support
- `Pxy` panning slide
- `Qxy` retrigger note with volume change
- `Rxy` tremolo
- Extended `Sxy` commands: `S3x`/`S4x`/`S5x` set vibrato/tremolo/panbrello waveform,
  `S6x` fine pattern delay (ticks), `S8x` panning, `SAy` high sample offset,
  `SBx` pattern loop, `SCx` note cut, `SDx` note delay, `SEx` pattern delay (rows)
- `Txx` tempo
- `Uxy` fine vibrato
- `Vxx` global volume
- `Wxy` global volume slide
- `Xxx` panning
- `Yxy` panbrello

## OpenMPT-Specific No-Ops

Some MPTM commands require engine services that this player does not expose, such as VST/plugin parameters, MIDI macros, alternate resampling modes, or editor display markers. The player accepts these commands and ignores them rather than failing load/playback.

Ignored command slots include OpenMPT-specific extended commands such as `Zxx`, `\xx`, `:xy`, `#xx`, `+xx`, `*xx`, and sample cue volume-column commands. The `S9x` sound-control sub-commands (surround, reverb, reverse playback) and `S7x` NNA/envelope sub-commands are accepted but treated as no-ops.

## Notes on IT Note Encoding

MPTM/IT pattern notes are 0-based (note value `60` == C-5). A stored value of `0` is treated as an empty cell, and `253`/`254`/`255` are the note-fade/cut/off sentinels. Volume-column slide and pitch sub-commands (values 85-124, 193-202) are not yet interpreted.

## Notes

This class is intentionally independent of `XMMODPlayer` and `S3MPlayer`. Those players were used only as local style and audio-buffer references.
