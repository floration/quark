// Audio: a small low-latency mixer on top of the OS audio API (WinMM on Windows, CoreAudio on
// macOS, ALSA on Linux loaded at runtime). All sounds are synthesized or decoded at startup (audio.cpp),
// so there are no sound files (but players' mods/sounds/ and the owner's overrides, docs/ASSETS.md).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../common/qmath.h"
#include "devices.h"

// Announcer voices (Settings > VOICE): recorded lines (voice_recorded.h sets, in this order), generated for the rest.
constexpr int kAnnouncerVoices = 7;
inline const char* announcerVoiceName(int v) {
    static const char* n[kAnnouncerVoices] = {"MALE", "FEMALE", "BRITISH", "OLD MAN", "E-GIRL", "INDIA", "SPY"};
    return n[v < 0 || v >= kAnnouncerVoices ? 0 : v];
}

enum SoundId {
    SND_HIT, SND_KILL, SND_HURT,
    SND_STEP, SND_JUMP, SND_LAND,
    SND_RIFLE, SND_ROCKET, SND_KNIFE, SND_SNIPER, SND_LASER, SND_SHOTGUN, SND_STICKY, SND_CROSSBOW, SND_BUBBLE, SND_SYRINGE, // (in weapon id order: SND_RIFLE + weapon)
    SND_EXPLOSION, SND_STICKY_ATTACH, SND_BOLT_HIT,
    SND_DASH, SND_GRAPPLE_FIRE, SND_GRAPPLE_HIT,
    SND_DEATH, SND_STREAK, SND_TYPING,   // your death thud, someone else's killstreak sting, someone typing (a key click)
    SND_CHECKPOINT, SND_FINISH,          // race: you passed a checkpoint (a ding), you crossed the finish (a rising chime)
    // the hitsound's heavier tiers (SND_HIT is the lightest; hitSoundFor() picks one by the hit's damage). Added after
    // the rest so the recorded kits' ids (sfx_recorded.h) and everyone's HIT overrides keep their meaning.
    SND_HIT_MED, SND_HIT_HEAVY, SND_HIT_BRUTAL,
    // movement tech and the locator (owner, 1.17.0): a super (dash jump), a hyper (super dash), the locator's ping
    SND_SUPER, SND_HYPER, SND_LOCATOR,
    // the announcer (recorded lines from voice_data.h, in its order; the same in every sound pack)
    SND_VOICE_THREE, SND_VOICE_TWO, SND_VOICE_ONE, SND_VOICE_GO, SND_VOICE_BOMB_PLANTED, SND_VOICE_BOMB_DEFUSED,
    SND_VOICE_HILL_TAKEN, SND_VOICE_HILL_LOST, SND_VOICE_POINT_TAKEN, SND_VOICE_POINT_LOST, SND_VOICE_ROUND_WON,
    SND_VOICE_ROUND_LOST, SND_VOICE_OVERTIME, SND_VOICE_YOU_WIN, SND_VOICE_YOU_LOSE, SND_VOICE_HIGH_SCORE, SND_VOICE_NEW_LEADER,
    SND_VOICE_YOU_DIED, SND_VOICE_HEADSHOT, SND_VOICE_AIR_ROCKET,
    SND_VOICE_SPREE, SND_VOICE_RAMPAGE, SND_VOICE_UNSTOPPABLE, SND_VOICE_GODLIKE,
    SND_VOICE_ONE_MINUTE, SND_VOICE_THIRTY_SECONDS, SND_VOICE_ROUND_OVER,
    SND_COUNT
};

// Quips (the QUIP key): each voice's own set of lines, never a gameplay call-out, played as ids SND_QUIP + k (k: the
// quip in the listener's set, Audio::quipFor). Overridable per voice as voices/<voice>/quips/<NAME>.<ext> (docs/ASSETS.md):
// any override files there form the set (sorted by name), replacing the default one: kDefaultQuips' lines in that voice.
constexpr int SND_QUIP = SND_COUNT;
constexpr int kMaxQuips = 64;
// the default set (no recorded quips yet): the voice's hype lines; never a countdown, objective, round or time call
constexpr int kDefaultQuips[] = {SND_VOICE_HEADSHOT, SND_VOICE_AIR_ROCKET, SND_VOICE_SPREE, SND_VOICE_RAMPAGE,
                                 SND_VOICE_UNSTOPPABLE, SND_VOICE_GODLIKE, SND_VOICE_NEW_LEADER, SND_VOICE_HIGH_SCORE};

// The hitsound for one hit you dealt (an EV_HIT: that hit's damage in points, a batch of laser ticks or a whole
// shotgun blast is one hit): four tiers, each heavier and louder. < 10 light, 10 to < 25 medium, 25 to 50 heavy
// (a sniper's 50 is heavy), > 50 brutal (51+: a crossbow headshot, a direct rocket plus splash).
inline int hitSoundFor(float dmg) {
    return dmg < 10.0f ? SND_HIT : dmg < 25.0f ? SND_HIT_MED : dmg <= 50.0f ? SND_HIT_HEAVY : SND_HIT_BRUTAL;
}
inline int hitTier(int id) { return id == SND_HIT ? 0 : id >= SND_HIT_MED && id <= SND_HIT_BRUTAL ? id - SND_HIT_MED + 1 : -1; }
// each tier's loudness, x today's single hitsound (1.0 = the level it had before the tiers)
constexpr float kHitTierGain[4] = {0.7f, 0.85f, 1.0f, 1.15f};
// Several hits in one moment (a blast hitting two players, two victims' batched laser ticks) make one hitsound, not
// a stack of them: within kWindow seconds of the last, only a heavier tier plays.
struct HitSoundGate {
    static constexpr double kWindow = 0.04;
    double at = -1e9;
    int tier = -1;
    bool pass(double now, int id) {
        int t = hitTier(id);
        if (now - at < kWindow && t <= tier) return false;
        at = now; tier = t;
        return true;
    }
};

class Audio {
public:
    static constexpr int kRate = 48000;
    Audio();
    bool start();                 // opens the device; false = no sound (game keeps running)
    void startOffline() { synthesize(); running_ = true; } // tests: mix() without a device
    void stop();
    ~Audio() { stop(); }

    // 2D (UI / your own sounds) and 3D (positioned, panned and attenuated from the listener).
    // pack: which sound pack to voice it with (-1 = yours; other players' sounds use the pack they picked)
    void play(int id, float volume = 1.0f, float pitch = 1.0f, int pack = -1);
    void play3D(int id, const Vec3& pos, float volume = 1.0f, float pitch = 1.0f, int pack = -1);
    bool hasSound(int id) const { return id >= 0 && id < SND_QUIP + kMaxQuips && !samples(0, id).empty(); }
    // (game thread: play3D's copy, plus one handed to the mixer, which re-pans every playing 3D sound with it each period)
    void setListener(const Vec3& pos, float yaw);
    void setMasterVolume(float v) { master_ = v; }
    // VIDEO + AUDIO > MORE 5 (1.19.4). The output's channels (0 stereo, 1 mono, 2 left and right swapped); how wide a
    // sound pans (0..1), how much sounds behind you are dipped and muffled (x the default), the distance past which
    // nothing is heard; the limiter (0 clean, 1 normal, 2 night: a compressor). All read by the mixer each period.
    void setChannels(int c) { channels_ = c; }
    static void setSpatial(float width, float behind, float range);
    void setDynamics(int d) { dynamics_ = d; }
    // AUDIO LATENCY: 0 low, 1 normal, 2 safe (more queued); the output is opened again with it
    void setLatency(int l) { if (latency_.exchange(l) != l) reopen_ = true; }
    // CUE SOUNDS / ANNOUNCER CALLS: these ids never start (a bit per id below 64)
    void setMuted(uint64_t mask) { muted_ = mask; }
    // ANNOUNCER DUCKING (1.19.4 batch 4): while an announcer line plays, every sound effect x (1 - d)
    void setDuck(float d) { duck_ = d; }
    // SOUND OCCLUSION: from (the listener) to (the sound) -> 0 clear .. 1 behind a wall, asked once as a 3D sound
    // starts (the game thread); empty: off
    std::function<float(const Vec3& from, const Vec3& to)> occluder;
    // VISUALIZE SOUNDS: the 3D sounds started (game thread), kept while on; the game takes them each frame
    struct Heard { int id; Vec3 at; };
    bool keepHeard = false;
    std::vector<Heard> heard;
    // Two more volumes on top of the master (Settings > SOUND, 0..1): every sound effect (the hitsound, weapons,
    // steps...) and the announcer's lines. Applied by the mixer each period, to sounds already playing too.
    void setEffectsVolume(float v) { fxVolume_ = v; }
    void setAnnouncerVolume(float v) { annVolume_ = v; }
    // QUIPS / QUIPS VOLUME (Settings > SOUND): quips are their own category, apart from the announcer's. Off: a quip
    // doesn't start, and one playing goes silent from the next period.
    void setQuips(bool on) { quipsOn_ = on; }
    void setQuipVolume(float v) { quipVolume_ = v; }
    // the gain the mixer gives a sound of this id (its category's volume; the master comes after the mix)
    float categoryGain(int id) const {
        return id < SND_VOICE_THREE ? fxVolume_.load() : id < SND_QUIP ? annVolume_.load() : quipsOn_ ? quipVolume_.load() : 0.0f;
    }
    // the current voice's quips (game thread): how many, each one's name (as its file: "HEADSHOT"), and the one a
    // sender's pick (EV_QUIP's byte, the same for every listener) means here: pick modulo this set's size, -1 for none
    int quipCount() const { return announcerLines_ ? (int)announcerLines_->quip.size() : 0; }
    std::string quipName(int k) const { return k >= 0 && k < quipCount() ? announcerLines_->quipName[k] : std::string(); }
    int quipFor(int pick) const { int n = quipCount(); return n > 0 ? (pick < 0 ? -pick : pick) % n : -1; }
    // Settings > DEVICES. The output: "" = the system default; else a device id from listDevices(false). Reopens the
    // output when it changes (the same path a device change or EXCLUSIVE AUDIO takes: the same buffers, no added
    // latency). A saved device that isn't there opens the default instead and outputMissing() says so.
    void setOutputDevice(const std::string& id);
    // the chosen output wasn't there when the output was last opened (the default plays); takeOutputMissing: once
    bool outputMissing() const { return outMissing_; }
    bool takeOutputMissing() { return outMissingNew_.exchange(false); }
    // REFRESH: try the chosen output again if it was missing (plugged back in); nothing otherwise
    void retryOutputDevice() { if (outMissing_) reopen_ = true; }
    // The OS's sound devices (input: microphones), by name. Asks the OS (a few ms): only when the DEVICES page opens
    // and on its REFRESH, never per frame. Empty where the OS gives no list (the default is always there besides).
    static std::vector<DeviceEntry> listDevices(bool input);
    // EXCLUSIVE AUDIO (Windows): the sound device to the game alone, past the system mixer (lower latency; other programs
    // are silent meanwhile). Reopens the output when it changes; falls back to shared when the device can't.
    void setExclusive(bool on) { if (exclusive_.exchange(on) != on) reopen_ = true; }
    // what exclusive got: off (not asked, or not opened yet), on, busy (another program has the device), unsupported
    // (the device takes none of our formats, or Windows doesn't allow exclusive use of it)
    enum ExclusiveState { kExclOff, kExclOn, kExclBusy, kExclUnsupported };
    int exclusiveState() const { return exclusiveState_; }
    bool exclusiveActive() const { return exclusiveState_ == kExclOn; }
    void setAnnouncer(int set) { if (set != announcer_) { if (running_) loadAnnouncer(set); else announcer_ = set; } } // 0 male, 1 female
    // Sound kits: DEFAULT (synthesized), HEAVY METAL, MAGICAL, WOODEN (recorded CC0 clips, sfx_recorded.h), RETRO (DEFAULT, chiptuned).
    // All packs are built at startup, so switching is free and every player can sound different.
    static constexpr int kPacks = 5;
    enum { kPackDefault, kPackMetal, kPackMagical, kPackRetro, kPackWooden };
    void setPack(int pack) { pack_ = (pack < 0 || pack >= kPacks) ? 0 : pack; }
    int pack() const { return pack_; }
    bool ok() const { return running_; }
    int voicesPlaying(int id); // tests (what the next mix would play: the sounds started since the last one included)
    const std::vector<float>& samples(int pack, int id) const { // tests / tools (the game thread: announcerLines_' only writer)
        static const std::vector<float> none;
        if (id < SND_VOICE_THREE) return sounds_[pack][id];
        if (id >= SND_QUIP) return announcerLines_ && id - SND_QUIP < (int)announcerLines_->quip.size() ? announcerLines_->quip[id - SND_QUIP] : none;
        return announcerLines_ ? announcerLines_->line[id - SND_VOICE_THREE] : none;
    }
    // names as files use them (mods/, the asset export, overrides): "ROCKET" / "GO", "heavy_metal", "old_man"
    static const char* soundName(int id);
    static const char* kitDir(int pack);
    static const char* voiceDir(int voice);
    // loudness as heard (low end weighted down, like the ear): the hitsound tiers are levelled with it
    static float heardLoudness(const std::vector<float>& v);

    // Mixes `frames` stereo frames into out (interleaved int16). Used by the backends and tests.
    void mix(int16_t* out, int frames);

private:
    // pos: where in the sound, 32.32 fixed point (exact over long clips, where a float's step went coarse past ~2.7 s);
    // step: the pitch; gainL / gainR: the gains the last period ended on (a 3D sound ramps from them to the new ones)
    struct Voice { int id, pack; uint64_t pos; float step; float gainL, gainR; bool spatial; Vec3 at; float volume; uint32_t seq = 0;
                   float lp = 1, lpY = 0, occl = 0; };   // (occl: SOUND OCCLUSION's 0..1, fixed as it starts)   // (X8: behind you, a one-pole low-pass: its coefficient, 1 = off, and its state)
    // full: the oldest sound effect makes room (an announcer line only if all are)
    static void addVoice(std::vector<Voice>& list, const Voice& v, size_t cap);
    uint32_t voiceSeq_ = 0;    // (under mu_) start order: the mixer's swap-and-pop doesn't keep voices_ in it
    void synthesize();
    void loadRecordedKits();
    void loadModSounds();     // players' own sounds: <game folder>/mods/sounds/<kit or all>/<SOUND>.wav
    void loadOverrideSounds(); // the owner's: sounds/<kit>/<SOUND>.* overrides (docs/ASSETS.md)
    void loadOverrideVoice(int set, std::vector<float>* lines, int count); // voices/<voice>/<LINE>.*
    struct AnnouncerLines;
    void loadQuips(int set, AnnouncerLines& a);   // the voice's quip set: its voices/<voice>/quips/ overrides, else the defaults
    void loadAnnouncer(int set);
    int announcer_ = 0;
    // the announcer's lines, the same in every pack: built off the mixer's lock and handed over as one pointer (a
    // voice change used to copy every line into all 5 packs while holding the lock the audio thread mixes under)
    struct AnnouncerLines {
        std::vector<float> line[SND_COUNT - SND_VOICE_THREE];
        std::vector<std::vector<float>> quip;   // (SND_QUIP + k; at most kMaxQuips)
        std::vector<std::string> quipName;
    };
    std::shared_ptr<const AnnouncerLines> announcerLines_;   // (the game thread's: samples())
    // (under mu_) the handover: nextLines_ for the mixer to take (linesNew_), retiredLines_ the ones it let go of, freed
    // by the game thread (a voice's lines are megabytes: never freed on the audio thread, never while it reads them)
    std::shared_ptr<const AnnouncerLines> nextLines_, retiredLines_;
    bool linesNew_ = false;
    std::shared_ptr<const AnnouncerLines> mixLines_;   // (the mixer's)
    std::atomic<int> pack_{0};
    static void voiceGains(Voice& v, const Vec3& listener, const Vec3& right);   // right: the listener's right
    void takePending();                    // (the mixer, under mixMu_) the sounds started since, the listener, the lines
    void render(float* buf, int frames);   // mix() before it is made 16-bit (1 = full scale; the limiter lets the loudest past it)
    void backendThread();
    void wasapiThread(std::atomic<int>* started);   // Windows: WASAPI output; started: 1 running, -1 unavailable (WinMM then)

    std::vector<float> sounds_[kPacks][SND_COUNT];   // (announcer ids stay empty: announcerLines_)
    // The game thread never waits for a mix pass: play / play3D only push onto pending_ (mu_ held just for that), and
    // the mixer swaps it for its empty incoming_ at the top of each period, then mixes voices_ (its own) with mu_ free.
    std::vector<Voice> voices_, incoming_;   // (the mixer's, under mixMu_)
    std::vector<Voice> pending_;             // (under mu_)
    std::mutex mu_, mixMu_;   // (mixMu_: the mixer's for the whole pass; only the tests' voicesPlaying ever waits on it)
    Vec3 listener_;           // (the game thread's: play3D's audible check)
    float listenerYaw_ = 0;
    Vec3 listenerNext_;       // (under mu_) for the mixer
    float listenerYawNext_ = 0;
    Vec3 mixListener_;        // (the mixer's)
    float mixListenerYaw_ = 0;
    std::atomic<float> master_{0.8f};
    std::atomic<float> fxVolume_{1.0f}, annVolume_{1.0f}, quipVolume_{1.0f};
    std::atomic<bool> quipsOn_{true};
    std::atomic<int> channels_{0}, dynamics_{1}, latency_{1};
    std::atomic<uint64_t> muted_{0};
    std::atomic<float> duck_{0.0f};
    float env_ = 0;   // (the mixer's: NIGHT's level follower)
    std::atomic<bool> running_{false}, quit_{false};
    std::atomic<bool> exclusive_{false}, reopen_{false};
    // the chosen output (under devMu_: the game thread sets it, the backend reads it when it opens a device)
    std::mutex devMu_;
    std::string outDevice_;
    std::string outputDevice() { std::lock_guard<std::mutex> l(devMu_); return outDevice_; }
    void noteOutput(bool missing) { if (missing && !outMissing_) outMissingNew_ = true; outMissing_ = missing; }
    std::atomic<bool> outMissing_{false}, outMissingNew_{false};
#if defined(__APPLE__)
    bool openQueue();    // macOS: the AudioQueue on the chosen device (setOutputDevice: closed and opened again)
    void closeQueue();
#endif
    std::atomic<int> exclusiveState_{kExclOff};
    float rateScale_ = 1.0f;   // (audio thread: our rate over the device's)
    std::thread thread_;
    void* backend_ = nullptr;
    void* waveEvent_ = nullptr;   // Windows: signalled when the device finishes a buffer
};
