// One-time conversion of a clips folder to the landing sync (2026-11-25):
//   asn_tests --convert-landing <clips dir> [--apply]
// Every trick clip gets the markers of its recorded figure; keys after the ground (old lot 5 landing part, or a clip
// longer than its air part) move to a landing clip of that trick (Landing-<Trick>_<stance>_<feet>.sanim).
// Without --apply: report only. With --apply: each changed file is backed up first (Documents\SessionStudio\backups).
#include "check.h"
#include "../src/clip.h"
#include "../src/figure.h"
#include "../src/trick_sync.h"
#include "../../../SessionStudio/model/editor_doc.h"
#include "../../../SessionStudio/model/workspace.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
bool ReadText(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
bool WriteText(const std::filesystem::path& p, const std::string& s) {
    const std::filesystem::path tmp = p.wstring() + L".tmp";
    { std::ofstream f(tmp, std::ios::binary); if (!f) return false; f << s; if (!f) return false; }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}
} // namespace

int ConvertLandingMain(const char* dirArg, bool apply) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::u8path(dirArg);
    const std::wstring backupRoot = DefaultBackupRoot();
    const std::string stamp = BackupStamp();
    int changed = 0, landings = 0, skipped = 0;
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir)) if (e.is_regular_file() && e.path().extension() == ".sanim") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& p : files) {
        const std::string name = p.filename().string();
        std::string text, err;
        Clip c;
        if (!ReadText(p, text) || !ParseClip(text, name, c, err)) { std::printf("%-34s illisible : %s\n", name.c_str(), err.c_str()); continue; }
        if (!(c.states & ST_Trick)) continue;
        const FigureVariant v = VariantOfClip(c);
        std::string ft;
        FigureRec fig;
        TrickMarkers m;
        if (ReadText(dir / "_figures" / fs::u8path(FigureKey(v) + ".json"), ft) && ParseFigure(ft, fig, err)) m = MarkersFromPhases(fig.phases, fig.times);
        if (!m.ok) { std::printf("%-34s move %s missing or without steps: left as it is\n", name.c_str(), FigureKey(v).c_str()); skipped++; continue; }
        Clip air, land;
        bool hasLand = false;
        if (!ConvertForLandingSync(c, m, air, land, hasLand)) { std::printf("%-34s already up to date\n", name.c_str()); continue; }
        std::printf("%-34s markers air %.3f catch %.3f landing %.3f ground %.3f | duration %.3f -> %.3f%s\n", name.c_str(), m.air, m.catchT,
                    m.land, m.ground, c.duration, air.duration, c.landing > 0.f ? " | old landing part removed" : "");
        std::string landFile;
        if (hasLand) {
            FigureVariant lv = v; lv.state = ST_Landing;
            const std::string stem = FigureKey(lv);
            landFile = stem + ".sanim";
            for (int i = 2; fs::exists(dir / fs::u8path(landFile)) && i < 100; i++) landFile = stem + "_" + std::to_string(i) + ".sanim";
            land.name = landFile.substr(0, landFile.size() - 6);
            land.file = landFile;
            if (ClipHasNoEffect(land)) { std::printf("%-34s   keys after the ground have no effect: no landing clip\n", ""); hasLand = false; }
            else std::printf("%-34s   -> landing clip %s (%.3f s, %d tracks)\n", "", landFile.c_str(), land.duration, (int)land.tracks.size());
        }
        changed++;
        if (!apply) continue;
        std::wstring written;
        if (!BackupClip(p.wstring(), backupRoot, stamp, 20, &written, err)) { std::printf("   cannot back up (%s): file not changed\n", err.c_str()); continue; }
        const std::string out = ClipToJson(air);
        Clip check;
        if (!ParseClip(out, name, check, err) || !WriteText(p, out)) { std::printf("   write refused (%s)\n", err.c_str()); continue; }
        if (hasLand) {
            const std::string lt = ClipToJson(land);
            if (ParseClip(lt, landFile, check, err) && WriteText(dir / fs::u8path(landFile), lt)) landings++;
            else std::printf("   landing clip refused (%s)\n", err.c_str());
        }
    }
    std::printf("%s: %d trick clip(s) %s, %d landing clip(s) %s, %d left as they are\n", apply ? "DONE" : "PREVIEW", changed,
                apply ? "converted" : "to convert", apply ? landings : 0, apply ? "created" : "(see above)", skipped);
    return 0;
}
