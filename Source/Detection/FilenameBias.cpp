#include "FilenameBias.h"

namespace Detection
{
    namespace
    {
        void boost(std::array<float, kNumClasses>& logits, Class cls, float delta)
        {
            auto idx = static_cast<int>(cls);
            if (idx >= 0 && idx < kNumClasses)
                logits[(size_t) idx] += delta;
        }
    }

    void FilenameBias::apply(const juce::String& originalName,
                             std::array<float, kNumClasses>& logits)
    {
        juce::String lower = originalName.toLowerCase();

        // ── Drum / loop detection ─────────────────────────────────────────────
        // When the filename includes "loop" AND a drum keyword, apply a stronger
        // boost because loops have diluted per-window energy.
        const bool isLoop = lower.contains("loop") || lower.contains("lp_") || lower.contains("_lp");

        const float drumBoost = isLoop ? 0.25f : 0.15f;

        if (lower.contains("kick") || lower.contains(" bd") || lower.contains("bd_") || lower.contains("kik"))
            boost(logits, Class::Kick, drumBoost);
        else if (isLoop && (lower.contains("drum") || lower.contains("beat") || lower.contains("break")))
            boost(logits, Class::Kick, 0.10f); // generic drum loop: mild kick hint

        if (lower.contains("snare") || lower.contains(" snr") || lower.contains("sd_") || lower.contains("sn_"))
            boost(logits, Class::Snare, drumBoost);

        if (lower.contains("hihat") || lower.contains("hi-hat") || lower.contains(" hat") || lower.contains("hh"))
            boost(logits, Class::HiHat, drumBoost);

        if (lower.contains("perc") || lower.contains("clap") || lower.contains("rim") || lower.contains("tom"))
            boost(logits, Class::Perc, isLoop ? 0.20f : 0.12f);

        // Bass
        if (lower.contains("bass") || lower.contains("808") || lower.contains("sub"))
            boost(logits, Class::Bass, 0.15f);

        // Guitar
        if (lower.contains("gtr") || lower.contains("guitar") || lower.contains("riff") || lower.contains("chug"))
            boost(logits, Class::Guitar, 0.18f);

        // Keys / piano / rhodes
        if (lower.contains("piano") || lower.contains("keys") || lower.contains("rhodes"))
            boost(logits, Class::Keys, 0.18f);

        // Pad / lead / vocal
        if (lower.contains("pad"))
            boost(logits, Class::Pad, 0.18f);

        // Lead: cover common synth/lead naming conventions
        if (lower.contains("lead") || lower.contains("synth") || lower.contains("synth")
            || lower.contains(" ld") || lower.contains("ld_") || lower.contains("_ld")
            || lower.contains("ld ") || lower.contains("sawtooth") || lower.contains("wavetable")
            || lower.contains("reese") || lower.contains("supersawv") || lower.contains("supersaw"))
            boost(logits, Class::Lead, 0.22f);

        if (lower.contains("vocal") || lower.contains(" vox") || lower.contains("_vox")
            || lower.contains("voice") || lower.contains("choir") || lower.contains("sing"))
            boost(logits, Class::Vocal, 0.20f);

        // FX / textures / atmospheres
        if (lower.contains("fx") || lower.contains("riser") || lower.contains("impact") || lower.contains("sweep")
            || lower.contains("texture") || lower.contains("atmos") || lower.contains("atmo"))
            boost(logits, Class::FX, 0.15f), boost(logits, Class::TextureAtmos, 0.10f);
    }
} // namespace Detection

