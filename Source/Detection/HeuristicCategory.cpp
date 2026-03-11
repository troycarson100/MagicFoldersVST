#include "HeuristicCategory.h"
#include "HeuristicConstants.h"
#include <vector>

namespace HeuristicCategory
{
    namespace
    {
        juce::String categoryFromFilename(const juce::String& nameNoExt)
        {
            const juce::String lower = nameNoExt.toLowerCase();
            if (lower.contains("kick") || lower.contains("kik") || lower.contains("bd ") || lower.contains("bd_"))
                return "Kicks";
            if (lower.contains("snare")
                || lower.contains("snr")
                || lower.contains("sn_")
                || lower.contains("sn-")
                || lower.contains("sd")
                || lower.contains("sd_")
                || lower.contains("sd-")
                || lower.contains("clap"))
                return "Snares";
            if (lower.contains("hihat") || lower.contains("hi-hat") || lower.contains(" hat ") || lower.contains("hh "))
                return "Hi-Hats";
            if (lower.contains("perc") || lower.contains("rim") || lower.contains("tom"))
                return "Percussion";
            if (lower.contains("bass") || lower.contains("808") || lower.contains("sub"))
                return "Bass";
            if (lower.contains("gtr") || lower.contains("guitar") || lower.contains("riff") || lower.contains("chug"))
                return "Guitar";
            if (lower.contains("piano") || lower.contains("keys") || lower.contains("rhodes") || lower.contains("pad"))
                return "Melodic";
            if (lower.contains("lead") && (lower.contains("synth") || lower.contains("melod")))
                return "Melodic";
            if (lower.contains("fx") || lower.contains("riser") || lower.contains("impact") || lower.contains("sweep"))
                return "FX";
            if (lower.contains("texture") || lower.contains("atmo") || lower.contains("ambient") || lower.contains("drone"))
                return "Textures";
            return juce::String();
        }

        bool isDrumCategory(const juce::String& cat)
        {
            return cat == "Kicks" || cat == "Snares" || cat == "Hi-Hats" || cat == "Percussion";
        }

        bool isNoisyTextureCandidate(bool hasSharpAttack, double duration, float centroidF, float zcrF, float rolloffF, int onsetCount)
        {
            const bool veryFewOnsets = onsetCount <= Heuristic::kTextureOnsetMax;
            const bool quiteLong = duration >= Heuristic::kTextureDurationMin;
            const bool darkAndSoft = !hasSharpAttack && centroidF < Heuristic::kTextureCentroidMax && zcrF < Heuristic::kTextureZcrMax;
            // noisySwish was catching hi-hat loops (high ZCR + bright) as Textures.
            // Guard: if the loop has multiple onsets it is rhythmic (hi-hat/perc loop),
            // not a texture — require very few onsets before calling it a swish texture.
            const bool rhythmicOnsets = onsetCount > Heuristic::kTextureOnsetMax;
            const bool noisySwish = !rhythmicOnsets && zcrF > Heuristic::kTextureNoisyZcrMin && rolloffF > Heuristic::kTextureNoisyRolloffMin;
            return (quiteLong && veryFewOnsets && darkAndSoft) || noisySwish;
        }

        bool isGuitarLikeLoop(bool isTonal, bool hasSharpAttack, double duration, float centroidF, float zcrF, int onsetCount)
        {
            if (!isTonal) return false;
            if (duration < Heuristic::kGuitarDurationMin || duration > Heuristic::kGuitarDurationMax) return false;
            if (onsetCount < Heuristic::kGuitarOnsetMin || onsetCount > Heuristic::kGuitarOnsetMax) return false;
            const bool rhythmGuitar = hasSharpAttack
                && centroidF >= Heuristic::kGuitarCentroidMin && centroidF <= Heuristic::kGuitarCentroidMax
                && zcrF < Heuristic::kGuitarZcrMaxRhythm;
            const bool pickedGuitar = !hasSharpAttack && onsetCount >= 4
                && centroidF >= Heuristic::kGuitarCentroidMinPicked && centroidF <= Heuristic::kGuitarCentroidMaxPicked
                && zcrF < Heuristic::kGuitarZcrMaxPicked;
            return rhythmGuitar || pickedGuitar;
        }

        void applyFilenameHints(const juce::File& file, Result& result)
        {
            const juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();
            const bool nameSuggestsGuitar = lowerName.contains("gtr") || lowerName.contains("guitar") || lowerName.contains("egtr") || lowerName.contains("agtr")
                || lowerName.contains("eg_") || lowerName.contains("ag_") || lowerName.contains("strum") || lowerName.contains("strummed")
                || lowerName.contains("riff") || lowerName.contains("chug") || lowerName.contains("palm") || lowerName.contains("powerchord")
                || lowerName.contains("power_chord") || lowerName.contains("rhythm") || lowerName.contains("pluck") || lowerName.contains("chord");
            const bool nameSuggestsBass = lowerName.contains("bass") || lowerName.contains("808") || lowerName.contains("sub") || lowerName.contains("lowend")
                || lowerName.contains("bs_") || lowerName.contains("bss") || lowerName.contains("subdrop") || lowerName.contains("sub_drop") || lowerName.contains("slide");
            const bool nameSuggestsTexture = lowerName.contains("texture") || lowerName.contains("atmo") || lowerName.contains("atmos") || lowerName.contains("ambience")
                || lowerName.contains("ambient") || lowerName.contains("drone") || lowerName.contains("noise") || lowerName.contains("fx");
            const bool nameSuggestsMelodic = lowerName.contains("melod") || lowerName.contains("lead") || lowerName.contains("keys") || lowerName.contains("piano")
                || lowerName.contains("synth") || lowerName.contains("arp") || lowerName.contains("pluck") || lowerName.contains("hook");
            const bool nameSuggestsPad = lowerName.contains("pad");

            if (result.type != "Loop")
            {
                // For one-shots: if the name clearly suggests a melodic/piano sound, allow overriding Bass as well.
                if ((nameSuggestsMelodic || nameSuggestsPad)
                    && (result.category == "Other" || result.category == "Percussion" || result.category == "Bass"))
                {
                    result.category = "Melodic";
                    return;
                }

                if (result.category != "Other" && result.category != "Percussion")
                    return;
                if (nameSuggestsGuitar) { result.category = "Guitar"; return; }
                if (nameSuggestsBass)   { result.category = "Bass"; return; }
                if (nameSuggestsMelodic || nameSuggestsPad) { result.category = "Melodic"; return; }
                if (nameSuggestsTexture) { result.category = "Textures"; return; }
                return;
            }
            if (result.category == "Textures" || result.category == "Melodic" || result.category == "Other" || result.category == "Loops")
            {
                // Don't override Textures → Guitar/Bass when the filename also
                // suggests an atmospheric/ambient sound — the texture label wins.
                if (nameSuggestsGuitar && !nameSuggestsTexture) { result.category = "Guitar"; return; }
                if (nameSuggestsBass   && !nameSuggestsTexture) { result.category = "Bass"; return; }
            }
            if ((result.category == "Textures" || result.category == "Other" || result.category == "Loops") && nameSuggestsMelodic && !nameSuggestsTexture)
            {
                result.category = "Melodic";
                return;
            }
            if (result.category == "Textures" && nameSuggestsTexture && !nameSuggestsMelodic && !nameSuggestsGuitar && !nameSuggestsBass)
                return;
            if (result.category == "Textures" && (nameSuggestsGuitar || nameSuggestsBass || nameSuggestsMelodic)
                && !nameSuggestsTexture)  // atmospheric keywords anchor the Textures label
            {
                if (nameSuggestsGuitar)      result.category = "Guitar";
                else if (nameSuggestsBass)  result.category = "Bass";
                else                        result.category = "Melodic";
            }
        }
    }

    Result run(const Features& f, const juce::File& file)
    {
        Result result;
        result.type = f.type;
        result.category = "Other";

        // Kick: three-level rule to handle sub, clean, and punchy/808-style kicks.
        // Level 0 — sub-bass rolloff (< 250 Hz, one-shots only): when 85% of spectral
        //   energy is below 250 Hz AND the sound has a sharp attack, it is virtually
        //   always a kick drum. This catches layered or noisy house/trap kicks whose
        //   ZCR is elevated (failing Level 1/2) but whose low-end rolloff is unmistakable.
        //   Guitar/snare/hihat/FX cannot have rolloff this low; rolloff > 0 guard skips
        //   samples where the rolloff algorithm returns no valid result (e.g. silent tail).
        // Level 1 — very low centroid (< 600 Hz): unambiguously a kick even when
        //   a pitch-sweep gives it a detectable musical key (e.g. 808 sub).
        //   Restricted to One-Shots only: melodic loops (keys, pads) can also have
        //   centroid < 600 Hz with a sharp onset — CNN14 handles kick loop detection.
        // Level 2 — moderate centroid (600–1400 Hz): require !isTonal so that
        //   guitar/bass plucks (tonal + low centroid) don't match.
        if (f.hasSharpAttack && f.type != "Loop" && f.rolloffF > 0.0f && f.rolloffF < Heuristic::kKickRolloffMax)
            result.category = "Kicks";
        else if (f.hasSharpAttack && f.type != "Loop" && f.centroidF < Heuristic::kKickCentroidLow && f.zcrF < Heuristic::kKickZcrMax)
            result.category = "Kicks";
        else if (f.hasSharpAttack && !f.isTonal && f.centroidF < Heuristic::kKickCentroidMax && f.zcrF < Heuristic::kKickZcrMax)
            result.category = "Kicks";
        else if (f.zcrF > Heuristic::kHiHatZcrMin && f.rolloffF > Heuristic::kHiHatRolloffMin
                 && f.centroidF > Heuristic::kHiHatCentroidMin && f.duration < Heuristic::kHiHatDurationMax)
            result.category = "Hi-Hats";
        // Snare rule — require a ZCR floor so that bright/punchy electronic kicks
        // (high centroid but compact, low-noise transient: ZCR < 0.08) don't land
        // here. Real snares and claps have broadband high-frequency noise → ZCR ≥ 0.08.
        else if (f.hasSharpAttack && f.centroidF > Heuristic::kSnareCentroidMin && f.centroidF < Heuristic::kSnareCentroidMax
                 && f.zcrF >= Heuristic::kSnareZcrMin)
            result.category = "Snares";
        else if (f.centroidF < Heuristic::kBassCentroidMax && !f.hasSharpAttack && f.mfcc1 < Heuristic::kBassMfcc1Max)
            result.category = "Bass";
        else if (f.zcrF > Heuristic::kFxZcrMin && f.rolloffF > Heuristic::kFxRolloffMin)
        {
            const float brightness = f.centroidF > 0.0f ? f.rolloffF / f.centroidF : 0.0f;
            if (brightness >= Heuristic::kFxBrightnessMin)
                result.category = "FX";
        }
        // Percussion: short + sharp attack that didn't match any specific drum type.
        // Also catch very short transients (< 0.3 s) even without a "sharp attack"
        // score — clicks, ticks, and micro-perc sounds always belong here rather
        // than "Other", which is a dead-end bucket users find confusing.
        else if ((f.hasSharpAttack && f.duration < Heuristic::kPercDurationMax)
                 || (!f.isTonal && f.duration < 0.3))
            result.category = "Percussion";
        else if (f.type == "Loop" && isGuitarLikeLoop(f.isTonal, f.hasSharpAttack, f.duration, f.centroidF, f.zcrF, f.onsetCount))
            result.category = "Guitar";
        else if (f.isTonal && f.centroidF >= Heuristic::kGuitarTonalCentroidMin && f.centroidF <= Heuristic::kGuitarTonalCentroidMax
                 && f.zcrF < Heuristic::kGuitarZcrMaxRhythm  // atmospheric pads/textures have very low ZCR and still fail this
                 && f.onsetCount >= 2                         // sustained drones with zero onsets are not guitar
                 && (!f.hasSharpAttack || (f.type == "Loop" && f.onsetCount >= Heuristic::kGuitarTonalOnsetMin && f.onsetCount <= Heuristic::kGuitarTonalOnsetMax)))
            result.category = "Guitar";
        else if (!f.hasSharpAttack && f.isTonal && f.mfcc2 > 0.0f)
            result.category = "Melodic";
        else if (f.type == "Loop" && f.isTonal && f.centroidF >= Heuristic::kGuitarTonalCentroidMin && f.centroidF <= Heuristic::kGuitarTonalCentroidMax
                 && f.zcrF < Heuristic::kGuitarTonalZcrMax && f.onsetCount >= Heuristic::kGuitarTonalOnsetMin && f.onsetCount <= Heuristic::kGuitarTonalOnsetMax2
                 && f.zcrF < Heuristic::kGuitarZcrMaxRhythm  // exclude slow atmospheric loops (pads/textures have ZCR near 0)
                 && f.onsetCount >= 2)                        // at least 2 onsets — sustained drone loops are not guitar
            result.category = "Guitar";
        else if (f.type == "Loop")
        {
            if (f.isTonal && f.duration >= Heuristic::kSongstarterDurationMin && f.onsetCount >= Heuristic::kSongstarterOnsetMin
                && f.onsetCount <= Heuristic::kSongstarterOnsetMax && f.bpm > 0)
                result.category = "Songstarter";
            else if (!f.isTonal && isNoisyTextureCandidate(f.hasSharpAttack, f.duration, f.centroidF, f.zcrF, f.rolloffF, f.onsetCount))
                result.category = "Textures";
            else if (f.isTonal)
                result.category = "Melodic";
            else
                // Non-tonal, non-texture loop (e.g. drum loop that slipped through).
                // Percussion is a better default than "Other" since it keeps it
                // visible and CNN14 can still refine it to a specific drum category.
                result.category = "Percussion";
        }
        else
        {
            // One-shot that cleared every specific rule.
            // Rather than dumping everything into "Other" (a dead-end bucket),
            // make a best-effort guess: tonal → Melodic, non-tonal → Percussion.
            // CNN14 will still override this if it has a confident alternative.
            result.category = f.isTonal ? "Melodic" : "Percussion";
        }

        const juce::String catFromFile = categoryFromFilename(file.getFileNameWithoutExtension());
        if (catFromFile.isNotEmpty())
        {
            if (result.category == "Other")
                result.category = catFromFile;
            else if (isDrumCategory(result.category) && isDrumCategory(catFromFile) && catFromFile != result.category)
                result.category = catFromFile;
        }

        if (result.category == "Melodic" && result.melodicVibe.isEmpty())
        {
            if (!f.hasSharpAttack && f.duration > Heuristic::kMelodicPadDurationMin && f.onsetCount <= Heuristic::kMelodicPadOnsetMax)
                result.melodicVibe = "Pad";
            else if (f.hasSharpAttack && (f.duration < Heuristic::kMelodicPluckDurationMax || f.onsetCount > Heuristic::kMelodicPluckOnsetMin))
                result.melodicVibe = "Pluck";
            else if (f.centroidF > Heuristic::kMelodicLeadCentroidMin)
                result.melodicVibe = "Lead";
            else
                result.melodicVibe = "Keys";
        }

        applyFilenameHints(file, result);
        return result;
    }
}
