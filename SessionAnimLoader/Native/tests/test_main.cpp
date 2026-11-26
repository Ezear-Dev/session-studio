// asn_tests: tests outside the game of the SessionAnimLoaderNative logic.
//   asn_tests                      -> runs every test
//   asn_tests --check-clips <dir>  -> loads a clips folder with the reference skeleton
#include "check.h"
#include <cstring>

std::vector<TestReg>& TestList() { static std::vector<TestReg> v; return v; }
int g_checks = 0, g_fails = 0;
int CheckClipsMain(const char* dir);   // test_clipset.cpp
int WriteTestDataMain(const char* dir); // test_editor.cpp: test _skeleton.json + _figures\*.json for the editor
int ShoulderMain(const char* dir, const char* clip, bool fix);   // test_shoulder.cpp
int ReplayStatesMain(const char* dir, const char* log, const char* from, const char* to);   // replay_states.cpp
int ConvertLandingMain(const char* dir, bool apply);   // convert_landing.cpp (2026-11-25 landing sync)

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--check-clips") == 0) return CheckClipsMain(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "--write-test-data") == 0) return WriteTestDataMain(argv[2]);
    if (argc >= 4 && std::strcmp(argv[1], "--shoulder") == 0) return ShoulderMain(argv[2], argv[3], argc >= 5 && std::strcmp(argv[4], "--fix") == 0);
    if (argc >= 6 && std::strcmp(argv[1], "--replay-states") == 0) return ReplayStatesMain(argv[2], argv[3], argv[4], argv[5]);
    if (argc >= 3 && std::strcmp(argv[1], "--convert-landing") == 0) return ConvertLandingMain(argv[2], argc >= 4 && std::strcmp(argv[3], "--apply") == 0);
    for (auto& t : TestList()) {
        const int before = g_fails;
        t.fn();
        if (g_fails != before) std::printf("  ^ in %s\n", t.name);
    }
    if (g_fails) { std::printf("%d FAIL out of %d checks\n", g_fails, g_checks); return 1; }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}
