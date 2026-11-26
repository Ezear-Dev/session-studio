// Small test framework of AnimStudioNative (no external dependency).
#pragma once
#include <cstdio>
#include <cmath>
#include <functional>
#include <vector>
#include <string>

struct TestReg { const char* name; void (*fn)(); };
std::vector<TestReg>& TestList();
extern int g_checks, g_fails;
struct TestAdder { TestAdder(const char* n, void (*f)()) { TestList().push_back({n, f}); } };

#define TEST(name) static void name(); static TestAdder add_##name(#name, &name); static void name()
#define CHECK(expr) do { g_checks++; if (!(expr)) { g_fails++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define CHECK_NEAR(a, b, eps) do { g_checks++; const double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (eps))) { g_fails++; std::printf("FAIL %s:%d %s ~ %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

// skater skeleton bones (taken from the P0 probe): names, parents, reference local positions
struct RefBone { const char* name; int parent; float x, y, z; };
const std::vector<RefBone>& RefSkeleton();
std::vector<std::string> RefNames();
std::vector<int> RefParents();
std::vector<float> RefPositions();   // 3 floats per bone
struct MirrorMap;
MirrorMap RefMirror();               // BuildMirrorMap on the reference skeleton
