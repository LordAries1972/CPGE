package com.ultimanium.cpge;

import android.content.Context;
import android.os.Bundle;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;

import java.util.ArrayList;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

// CpgeTts — Java side of the CPGE Android TTS backend (Phase D step 3,
// 24 Nov 2026).
//
// Thin wrapper over android.speech.tts.TextToSpeech: the native TTSManager
// (TTSManager.cpp, Android section) drives it through JNI static calls.
// Synthesis on Android is asynchronous, so the native side tracks
// completion through the utterance callback, which reports back via the
// nativeOnUtteranceDone JNI method (implemented in TTSManager.cpp).
//
// All methods are static and synchronized where they touch the shared
// TextToSpeech instance. TextToSpeech construction is itself async
// (onInit fires later, usually on the main thread), so init() returns
// immediately and isReady() reports when the engine is usable; the native
// side waits briefly for it during Initialize().
//
// Rate / pitch / volume use the platform scales (setSpeechRate / setPitch
// are 1.0 = normal; the Bundle KEY_PARAM_VOLUME is 0.0..1.0). The native
// side maps the engine's -10..+10 rate/pitch and 0.0..1.0 volume onto
// these. There is no speaker-channel API in Android TTS: channel is a
// no-op on this backend (stored on the native side only).
public final class CpgeTts {
    private static TextToSpeech s_tts = null;
    private static final AtomicBoolean s_ready = new AtomicBoolean(false);
    private static final AtomicBoolean s_speaking = new AtomicBoolean(false);
    // Last voice set, as a BCP-47 tag (e.g. "en-US"). The native side uses
    // the same tag as the voice identifier for GetAvailableVoices /
    // SetVoiceByName, so the two sides always agree on names.
    private static String s_voiceTag = "en-US";
    private static float s_rate = 1.0f;
    private static float s_pitch = 1.0f;
    private static float s_volume = 0.8f;

    private CpgeTts() {
    }

    // Create the TextToSpeech engine for this activity's context. Safe to
    // call again: a live engine is reused. Returns true when an engine
    // object exists (not yet when it is ready — see isReady()).
    public static synchronized boolean init(Context context) {
        if (s_tts != null) {
            return true;
        }
        s_ready.set(false);
        s_tts = new TextToSpeech(context, status -> {
            if (status == TextToSpeech.SUCCESS) {
                s_tts.setSpeechRate(s_rate);
                s_tts.setPitch(s_pitch);
                setLanguageInternal(s_voiceTag);
                // onError(String) is deprecated in API 26 in favour of
                // onError(String, int), but it is still the abstract method
                // the listener must implement: the API-26 overload's
                // default implementation forwards to it, so overriding
                // only this one covers both call paths.
                @SuppressWarnings("deprecation")
                UtteranceProgressListener listener = new UtteranceProgressListener() {
                    @Override
                    public void onStart(String utteranceId) {
                        s_speaking.set(true);
                    }

                    @Override
                    public void onDone(String utteranceId) {
                        s_speaking.set(false);
                        nativeOnUtteranceDone(utteranceId);
                    }

                    @Override
                    public void onError(String utteranceId) {
                        s_speaking.set(false);
                        nativeOnUtteranceDone(utteranceId);
                    }

                    @Override
                    public void onStop(String utteranceId, boolean interrupted) {
                        s_speaking.set(false);
                        nativeOnUtteranceDone(utteranceId);
                    }
                };
                s_tts.setOnUtteranceProgressListener(listener);
                s_ready.set(true);
            } else {
                // Engine data missing or the service failed: stay
                // not-ready so the native side reports TTS unavailable
                // instead of silently dropping speech.
                s_ready.set(false);
            }
        });
        return true;
    }

    public static synchronized boolean isReady() {
        return s_ready.get() && s_tts != null;
    }

    // Speak one utterance, flushing anything queued. Returns false when
    // the engine is not ready or the call was rejected. The utteranceId
    // is echoed back through the listener so the native side can tell a
    // late callback for a flushed utterance from the current one.
    public static synchronized boolean speak(String text, String utteranceId) {
        if (!isReady() || text == null || text.isEmpty()) {
            return false;
        }
        Bundle params = new Bundle();
        // KEY_PARAM_VOLUME is best-effort: some TTS engines ignore it.
        params.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, s_volume);
        int result = s_tts.speak(text, TextToSpeech.QUEUE_FLUSH, params, utteranceId);
        return result == TextToSpeech.SUCCESS;
    }

    public static synchronized void stop() {
        if (s_tts != null) {
            s_tts.stop();
        }
        s_speaking.set(false);
    }

    public static synchronized void shutdown() {
        if (s_tts != null) {
            s_tts.stop();
            s_tts.shutdown();
            s_tts = null;
        }
        s_ready.set(false);
        s_speaking.set(false);
    }

    public static synchronized boolean isSpeaking() {
        return s_tts != null && s_tts.isSpeaking();
    }

    public static synchronized void setRate(float rate) {
        s_rate = rate;
        if (s_tts != null) {
            s_tts.setSpeechRate(rate);
        }
    }

    public static synchronized void setPitch(float pitch) {
        s_pitch = pitch;
        if (s_tts != null) {
            s_tts.setPitch(pitch);
        }
    }

    public static synchronized void setVolume(float volume) {
        s_volume = volume;
    }

    // Set the voice by BCP-47 tag (e.g. "en-US", "en-GB"). Returns true
    // only when the language is available; on failure the previous
    // language is restored so a bad name cannot silence later speech.
    public static synchronized boolean setLanguage(String tag) {
        if (!isReady() || tag == null || tag.isEmpty()) {
            return false;
        }
        String previous = s_voiceTag;
        int result = setLanguageInternal(tag);
        if (result == TextToSpeech.LANG_AVAILABLE
                || result == TextToSpeech.LANG_COUNTRY_AVAILABLE
                || result == TextToSpeech.LANG_COUNTRY_VAR_AVAILABLE) {
            s_voiceTag = tag;
            return true;
        }
        setLanguageInternal(previous);
        return false;
    }

    private static int setLanguageInternal(String tag) {
        return s_tts.setLanguage(Locale.forLanguageTag(tag));
    }

    // BCP-47 tags of the voices the engine reports (getVoices, API 21+).
    // Empty when the engine is not ready. The native side returns these
    // as the available voice names.
    public static synchronized String[] getVoiceNames() {
        if (!isReady()) {
            return new String[0];
        }
        Set<Voice> voices = s_tts.getVoices();
        ArrayList<String> tags = new ArrayList<>();
        if (voices != null) {
            for (Voice voice : voices) {
                Locale locale = voice.getLocale();
                if (locale != null) {
                    tags.add(locale.toLanguageTag());
                }
            }
        }
        return tags.toArray(new String[0]);
    }

    public static synchronized String getVoiceTag() {
        return s_voiceTag;
    }

    // Called from the utterance listener when synthesis finishes (or
    // fails). Implemented in native code (TTSManager.cpp) — it wakes the
    // synchronous Play() wait and clears the speaking state.
    private static native void nativeOnUtteranceDone(String utteranceId);
}
