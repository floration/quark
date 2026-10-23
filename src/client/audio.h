// Audio: a small low-latency mixer on top of the OS audio API (WinMM on Windows, CoreAudio on
// macOS, ALSA on Linux loaded at runtime). All sounds are synthesized or decoded at startup (audio.cpp),
// so there are no sound files (but players' mods/sounds/ and the owner's overrides, docs/ASSETS.md).
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../common/qmath.h"

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
    SND_VOICE_YOU_DIED, SND_VOICE_HEADSHOT, SND_VOICE_AIRBORNE, SND_VOICE_AIR_ROCKET,
    SND_VOICE_SPREE, SND_VOICE_RAMPAGE, SND_VOICE_UNSTOPPABLE, SND_VOICE_GODLIKE,
    SND_VOICE_ONE_MINUTE, SND_VOICE_THIRTY_SECONDS, SND_VOICE_TEN_SECONDS, SND_VOICE_FIVE_SECONDS, SND_VOICE_ROUND_OVER,
    SND_COUNT
};

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
    bool start();                 // opens the device; false = no sound (game keeps running)
    void startOffline() { synthesize(); running_ = true; } // tests: mix() without a device
    void stop();
    ~Audio() { stop(); }

    // 2D (UI / your own sounds) and 3D (positioned, panned and attenuated from the listener).
    // pack: which sound pack to voice it with (-1 = yours; other players' sounds use the pack they picked)
    void play(int id, float volume = 1.0f, float pitch = 1.0f, int pack = -1);
    void play3D(int id, const Vec3& pos, float volume = 1.0f, float pitch = 1.0f, int pack = -1);
    bool hasSound(int id) const { return id >= 0 && id < SND_COUNT && !samples(0, id).empty(); }
    // (game thread only, as play / play3D, the only readers: the mixer never reads it, so no lock against it)
    void setListener(const Vec3& pos, float yaw) { listener_ = pos; listenerYaw_ = yaw; }
    void setMasterVolume(float v) { master_ = v; }
    void setAnnouncer(int set) { if (set != announcer_) { if (running_) loadAnnouncer(set); else announcer_ = set; } } // 0 male, 1 female
    // Sound kits: DEFAULT (synthesized), HEAVY METAL, MAGICAL, WOODEN (recorded CC0 clips, sfx_recorded.h), RETRO (DEFAULT, chiptuned).
    // All packs are built at startup, so switching is free and every player can sound different.
    static constexpr int kPacks = 5;
    enum { kPackDefault, kPackMetal, kPackMagical, kPackRetro, kPackWooden };
    void setPack(int pack) { pack_ = (pack < 0 || pack >= kPacks) ? 0 : pack; }
    int pack() const { return pack_; }
    bool ok() const { return running_; }
    const std::vector<float>& samples(int pack, int id) const { // tests / tools (the game thread: announcerLines_' only writer)
        static const std::vector<float> none;
        if (id < SND_VOICE_THREE) return sounds_[pack][id];
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
    struct Voice { int id, pack; float pos; float step; float gainL, gainR; bool spatial; Vec3 at; float volume; };
    void synthesize();
    void loadRecordedKits();
    void loadModSounds();     // players' own sounds: <game folder>/mods/sounds/<kit or all>/<SOUND>.wav
    void loadOverrideSounds(); // the owner's: sounds/<kit>/<SOUND>.* overrides (docs/ASSETS.md)
    void loadOverrideVoice(int set, std::vector<float>* lines, int count); // voices/<voice>/<LINE>.*
    void loadAnnouncer(int set);
    int announcer_ = 0;
    // the announcer's lines, the same in every pack: built off the mixer's lock and swapped in as one pointer (a
    // voice change used to copy every line into all 5 packs while holding the lock the audio thread mixes under)
    struct AnnouncerLines { std::vector<float> line[SND_COUNT - SND_VOICE_THREE]; };
    std::shared_ptr<const AnnouncerLines> announcerLines_;   // (written under mu_, by the game thread only)
    std::atomic<int> pack_{0};
    void voiceGains(Voice& v);
    void backendThread();

    std::vector<float> sounds_[kPacks][SND_COUNT];   // (announcer ids stay empty: announcerLines_)
    std::vector<Voice> voices_;
    std::mutex mu_;
    Vec3 listener_;
    float listenerYaw_ = 0;
    std::atomic<float> master_{0.8f};
    std::atomic<bool> running_{false}, quit_{false};
    std::thread thread_;
    void* backend_ = nullptr;
    void* waveEvent_ = nullptr;   // Windows: signalled when the device finishes a buffer
};
