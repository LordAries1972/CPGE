# ITPlayer Class - Usage Guide

## Overview

`ITPlayer` plays Impulse Tracker `.it` modules through the same DirectSound-backed playback model used by `XMMODPlayer`, `S3MPlayer`, and `MPTMPlayer`.

The loader reads the native `IMPM` container, IT order tables, IT instrument/sample pointer tables, packed IT patterns, internal IT instruments, and internal PCM sample data. It supports both uncompressed and compressed IT samples, including the 8-bit and 16-bit block-compression layouts used by Impulse Tracker 2.14/2.15 files.

## Basic Usage

```cpp
#include "ITPlayer.h"

void PlayITExample() {
    ITPlayer player;

    if (player.Play(L"Assets\\music.it")) {
        player.SetVolume(48);
        player.SetGlobalVolume(64);

        while (player.IsPlaying()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

## Engine Selection

Enable the player by defining `__USE_ITPLAYER__` in `Includes.h` and disabling the other music-player selection macros.

```cpp
//#define __USE_XMPLAYER__
//#define __USE_S3MPLAYER__
//#define __USE_MPTMPLAYER__
#define __USE_ITPLAYER__
```

## Supported Playback Features

- 44.1 kHz, 16-bit stereo DirectSound output on Windows.
- IT `IMPM` headers, order lists, pointer tables, packed pattern rows, sample headers, and sample data.
- 8-bit and 16-bit PCM sample playback.
- 8-bit and 16-bit IT compressed sample decoding.
- Mono and stereo source samples mixed to engine stereo output.
- Forward sample loops and sample sustain loops.
- Instrument note/sample map lookup.
- Fixed 256-voice IT virtual voice pool with host-channel separation.
- Instrument NNA and duplicate note checks/actions.
- Volume and panning envelopes with sustain and loop handling.
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
- `Mxx` channel volume approximated onto the active voice
- `Nxy` channel volume slide approximated onto the active voice
- `Oxx` sample offset with `SAy` high-offset support
- `Pxy` panning slide
- `Qxy` retrigger note with volume change
- `Rxy` tremolo
- Extended `Sxy` commands: `S3x`, `S4x`, `S5x`, `S6x`, `S8x`, `SAy`, `SBx`, `SCx`, `SDx`, `SEx`
- `Txx` tempo
- `Uxy` fine vibrato
- `Vxx` global volume
- `Wxy` global volume slide
- `Xxx` panning
- `Yxy` panbrello

## Known Limits

`ITPlayer` now separates 64 IT host channels from a fixed 256-entry virtual voice pool, so old notes can continue, fade, or release when new notes arrive. Remaining fidelity gaps are per-voice resonant filters, MIDI macro execution, pitch/filter envelope evaluation, bidirectional loops, and some extended `S7x` envelope-control commands. Unsupported commands are accepted as no-ops where failing playback would be worse than ignoring tracker metadata.