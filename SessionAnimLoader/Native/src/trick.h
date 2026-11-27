// P6: tricks, stances and move variants (shared DLL / editor).
//
// The game names its flip tricks "<STANCE>_<Name>" (Name property of FlipTrickDefinition): RGS_KickFlip, SWS_KickFlip,
// FKS_Kickflip, NLS_Heelflip, RGS_BS_ShoveIt_360... STANCE : RGS normal, SWS switch, FKS fakie, NLS nollie.
// A clip targets a trick by its base name (without the prefix, case-insensitive), a set of stances
// and the skater's feet (regular / goofy, IsSkatingGoofy). A "figure" is the original animation of ONE variant:
// file clips\_figures\<key>.json, key = "<Base>_<STANCE>_<regular|goofy>" (e.g. KickFlip_RGS_regular, Manual_SWS_goofy).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum Stance { STANCE_Normal = 0, STANCE_Switch = 1, STANCE_Fakie = 2, STANCE_Nollie = 3, STANCE_Count = 4 };
constexpr uint8_t kAllStances = 0x0F;

const char* StancePrefix(int s);   // "RGS", "SWS", "FKS", "NLS"
const char* StanceLabel(int s);    // "Normal", "Switch", "Fakie", "Nollie"
int StanceFromPrefix(const std::string& p);   // -1 if unknown (case-insensitive)
int StanceFromLabel(const std::string& l);    // "normal" / "switch" / "fakie" / "nollie" (case-insensitive), -1 otherwise

// "RGS_KickFlip" -> base "KickFlip", stance 0. Also accepts "TRICK_RGS_KickFlip". false without a stance prefix.
bool SplitTrickName(const std::string& full, std::string& base, int& stance);
std::string Lower(const std::string& s);
// hash (FNV-1a 32 bits) of the base name in lower case; 0 reserved for "no trick / any trick"
uint32_t TrickHash(const std::string& base);

// trigger states of a clip (bits of Clip::states)
enum StateBit : uint32_t {
    ST_Manual = 1, ST_NoseManual = 2,
    ST_Trick = 4,            // flip trick (ollie, flips, shove-its, late tricks...); "Kickflip" of v0.7 clips = ST_Trick + trick KickFlip
    ST_Kickflip = ST_Trick,  // old name
    ST_Grind = 8, ST_Powerslide = 16, ST_Grab = 32,
    // P6 lot 4: base poses (each one excludes the others; see nSetState)
    ST_OnFoot = 64,     // off the board, on the legs
    ST_Riding = 128,    // standing on the board, rolling (no other state)
    ST_Crouch = 256,    // crouched before a pop (IsCranking)
    ST_Landing = 512,   // just after a landing (one shot)
    ST_Walking = 1024,  // off the board, moving (OnFoot = standing still)
    ST_Pushing = 2048,  // on the board, pushing (Riding = standing on the board, not pushing)
    ST_Running = 4096,  // 2026-11-25: off the board, running (Speed above the walk; Walking = walking pace)
    ST_Dismount = 8192, // 2026-11-25: the feet just left the board (one shot). Recorded as a figure; played through the
                        // dismount part of a riding clip (Clip::dismount), not as a clip trigger of its own
};
// every state, in the order the clip file writes them
inline constexpr uint32_t kStateOrder[] = {ST_Manual, ST_NoseManual, ST_Trick, ST_Grind, ST_Powerslide, ST_Grab, ST_OnFoot, ST_Riding, ST_Crouch, ST_Landing, ST_Walking, ST_Pushing, ST_Running, ST_Dismount};
bool StateAllStances(uint32_t bit);           // the 4 stances (normal, switch, fakie, nollie) exist for this state
const char* StateName(uint32_t bit);         // "Manual", "NoseManual", "Trick", "Grind", "Powerslide", "Grab"
uint32_t StateFromName(const std::string& n); // 0 if unknown ("Kickflip" -> ST_Trick)
bool StateLoops(uint32_t bit);                // all but ST_Trick and ST_Landing

// A move variant: what is edited in the editor, and what the game records.
struct FigureVariant {
    uint32_t state = ST_Manual;   // a single bit
    std::string trick;            // trick base name (ST_Trick), as the game writes it; grind name for ST_Grind ("" = all)
    int stance = STANCE_Normal;
    bool goofy = false;
    bool operator==(const FigureVariant& o) const { return state == o.state && Lower(trick) == Lower(o.trick) && stance == o.stance && goofy == o.goofy; }
    bool operator!=(const FigureVariant& o) const { return !(*this == o); }
};
std::string FigureKey(const FigureVariant& v);          // "Manual_RGS_regular", "KickFlip_SWS_goofy", "Grind-Crooked_RGS_regular"
bool StateNamed(uint32_t bit);                          // 2026-11-25: the state carries a name (trick, grind)
bool ParseFigureKey(const std::string& key, FigureVariant& out);
// v0.27: the names the game shows (FlipTrickDefinition.DisplayName), read at the pop. One per full name, because the
// game names each stance on its own. Written by the mod to clips\_trick_names.json, read by the editor.
class TrickNames {
public:
    static constexpr size_t kMaxBytes = 80;
    // cleans the name (no control characters, trimmed, kMaxBytes); false if nothing usable or nothing changed
    bool Set(const std::string& full, const std::string& shown);
    const std::string* Find(const std::string& full) const;   // any case; nullptr if unknown
    size_t Size() const { return byLower_.size(); }
    std::string ToJson() const;
    // adds the file's names without replacing those already here; false (nothing changed) if the file is unreadable
    bool Merge(const std::string& text);
private:
    std::map<std::string, std::pair<std::string, std::string>> byLower_;   // lower full -> (full as written, shown)
};
// a late flip ("Late_Front_KickFlip", "Late_BackFoot_Heelflip"...; not the late ollies)
bool IsLateFlip(const std::string& base);
// 2026-11-27: the same move in another stance, as the game shows it. In switch and fakie the game shows a late flip
// with the other foot than its internal name ("SWS_Late_BackFoot_KickFlip" = "Switch Late Front Foot Flip"), so going
// between {normal, nollie} and {switch, fakie} takes the other foot's trick. Every other move keeps its name.
FigureVariant WithStance(const FigureVariant& v, int stance);
// "Kickflip · Switch · regular" (for the interface). Trick name: read in game (read may be null), else derived from
// the internal name.
std::string VariantTitle(const FigureVariant& v, const TrickNames* read = nullptr);
// readable name of a trick: "BS_ShoveIt_360" -> "BS ShoveIt 360"
std::string TrickDisplay(const std::string& base);
// title of a trick's card: the name read for the normal stance, else TrickDisplay (the names of the other stances hold
// the stance word, they would mislabel the card)
std::string TrickCardTitle(const std::string& base, const TrickNames* read);

// Catalogue of the known flip tricks of the game (seen in the logs of Session, ExpandedTricks and the P5b probe):
// one name per trick and per stance, as the game writes it. This is the editor's whole trick list (2026-11-27): a trick
// that is not here has no card, so add it here (recordings and clips of it are kept on disk).
struct CatalogTrick { const char* full; const char* category; };
const std::vector<CatalogTrick>& TrickCatalog();
