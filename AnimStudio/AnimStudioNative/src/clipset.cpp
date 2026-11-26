#include "clipset.h"
#include "json.h"
#include "net.h"
#include <windows.h>
#include <cstdio>

namespace {

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool ReadAll(const std::wstring& path, std::string& out) {
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, k);
        if (out.size() > (8u << 20)) { fclose(f); return false; }   // 8 MB: this is not a clip
    }
    fclose(f);
    return true;
}

uint64_t Fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

uint64_t Stamp(const WIN32_FIND_DATAW& fd) { return ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime; }
uint64_t Size(const WIN32_FIND_DATAW& fd) { return ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; }

bool EndsWith(const std::wstring& s, const wchar_t* suf) {
    const size_t n = wcslen(suf);
    if (s.size() < n) return false;
    return _wcsicmp(s.c_str() + s.size() - n, suf) == 0;
}

} // namespace

ClipLibrary::ClipLibrary(std::wstring dir) : dir_(std::move(dir)) {
    ClipSet* empty = new ClipSet();
    cur_.store(empty, std::memory_order_release);
}

ClipLibrary::~ClipLibrary() {
    delete cur_.load();
    for (auto& r : retired_) delete r.set;
}

std::vector<std::string> ClipLibrary::SetRig(std::vector<std::string> names) {
    names_ = std::move(names);
    haveRig_ = !names_.empty();
    return Publish();
}

std::vector<std::string> ClipLibrary::Scan() {
    std::vector<std::string> log;
    bool changed = false;
    for (auto& kv : files_) kv.second.seen = false;
    bool studioNow = false;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir_ + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring wname = fd.cFileName;
            const std::string name = Utf8(wname);
            if (_wcsicmp(wname.c_str(), L"_studio.json") == 0) {
                studioNow = true;
                if (studioSeen_ && Stamp(fd) == studioStamp_ && Size(fd) == studioSize_) continue;
                studioSeen_ = true; studioStamp_ = Stamp(fd); studioSize_ = Size(fd);
                changed = true;
                std::string text, err;
                JValue v;
                std::map<std::string, int> conv;
                bool ok = ReadAll(dir_ + L"\\" + wname, text);
                if (!ok) { studioSeen_ = false; err = "cannot read (file open elsewhere?)"; }   // will retry
                if (ok) ok = JParse(text, v, err);
                if (ok && v.t != JValue::Obj) { ok = false; err = "an object is expected"; }
                const JValue* mc = ok ? v.get("mirrorConv") : nullptr;
                if (ok && mc) {
                    if (mc->t != JValue::Obj) { ok = false; err = "mirrorConv: object { \"bone\": \"I|X|Y|Z\" } expected"; }
                    else for (auto& kv : mc->o) {
                        const int c = (kv.second.t == JValue::Str) ? ConvFromName(kv.second.s) : -1;
                        if (c < 0) { ok = false; err = "mirrorConv." + kv.first + ": \"I\", \"X\", \"Y\" or \"Z\" expected"; break; }
                        conv[kv.first] = c;
                    }
                }
                bool lpp = true;
                const JValue* lp = ok ? v.get("legPostPass") : nullptr;
                if (ok && lp) { if (lp->t != JValue::Bool) { ok = false; err = "legPostPass: true or false expected"; } else lpp = lp->b; }
                if (ok) {
                    if (conv != convByName_) { convByName_ = conv; mirrorStamp_++; }
                    legPostPass_ = lpp;
                    log.push_back("_studio.json: " + std::to_string(conv.size()) + " mirror override(s)");
                } else log.push_back("_studio.json: " + (err.empty() ? std::string("unreadable") : err) + " -- keeping the previous settings");
                continue;
            }
            if (!EndsWith(wname, L".sanim")) continue;
            Entry& e = files_[name];
            e.seen = true;
            // the CONTENT is compared (clips are small): a modification date can stay identical
            // between two close saves of the same size
            std::string text, err;
            if (!ReadAll(dir_ + L"\\" + wname, text)) {
                if (!e.readFail) log.push_back(name + ": cannot read (file open elsewhere?) -- retrying every second");
                e.readFail = true;
                continue;   // previous version kept as it is
            }
            e.readFail = false;
            const uint64_t h = Fnv1a(text);
            if (e.hashed && e.hash == h) continue;
            e.hashed = true;
            e.hash = h;
            changed = true;
            Clip c;
            bool ok = ParseClip(text, name, c, err);
            if (ok && haveRig_) { Clip probe = c; ok = BindClip(probe, names_, err); }
            if (ok) {
                c.stamp = ++clipStamp_;
                c.netId = NetId(name);           // 2026-11-26 multiplayer: same file + same bytes = same clip on every PC
                c.netHash = (uint32_t)h;
                e.good = std::move(c);
                e.hasGood = true;
                log.push_back(name + ": loaded (\"" + e.good.name + "\", " + std::to_string(e.good.tracks.size()) + " track(s))");
            } else if (e.hasGood) log.push_back(name + ": " + err + " -- keeping the previous version");
            else log.push_back(name + ": " + err + " -- refused");
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (auto it = files_.begin(); it != files_.end();) {
        if (!it->second.seen) { log.push_back(it->first + ": removed (file deleted)"); it = files_.erase(it); changed = true; }
        else ++it;
    }
    if (studioSeen_ && !studioNow) {
        studioSeen_ = false;
        if (!convByName_.empty()) { convByName_.clear(); mirrorStamp_++; }
        legPostPass_ = true;
        changed = true;
        log.push_back("_studio.json: deleted -- default mirror");
    }
    if (changed) {
        auto more = Publish();
        log.insert(log.end(), more.begin(), more.end());
    }
    return log;
}

std::vector<std::string> ClipLibrary::Publish() {
    std::vector<std::string> log;
    ClipSet* s = new ClipSet();
    s->generation = ++gen_;
    s->mirrorStamp = mirrorStamp_;
    s->legPostPass = legPostPass_;
    if (haveRig_) {
        for (auto& kv : convByName_) {
            int idx = -1;
            for (size_t i = 0; i < names_.size(); i++) if (names_[i] == kv.first) { idx = (int)i; break; }
            if (idx < 0) log.push_back("_studio.json: mirrorConv: unknown bone " + kv.first + " -- ignored");
            else s->convOverride.emplace_back(idx, kv.second);
        }
        for (auto& kv : files_) {   // std::map: sorted by file name
            if (!kv.second.hasGood) continue;
            Clip c = kv.second.good;
            std::string err;
            if (!BindClip(c, names_, err)) { log.push_back(kv.first + ": " + err + " -- refused by this skeleton"); continue; }
            s->clips.push_back(std::move(c));
        }
    }
    ClipSet* old = cur_.exchange(s, std::memory_order_acq_rel);
    if (old) retired_.push_back(Retired{old, GetTickCount64()});
    return log;
}

void ClipLibrary::CollectRetired() {
    const uint64_t now = GetTickCount64();
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (now - it->at > 2000) { delete it->set; it = retired_.erase(it); }
        else ++it;
    }
}
