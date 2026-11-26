// Skeleton file (_skeleton.json)
#include "check.h"
#include "../src/skeleton.h"
#include "../src/mirror.h"

namespace {
SkeletonFile Ref() {
    SkeletonFile s;
    s.names = RefNames();
    s.parents = RefParents();
    s.refPos = RefPositions();
    s.conv = RefMirror().conv;
    return s;
}
} // namespace

TEST(skeleton_roundtrip) {
    const SkeletonFile s = Ref();
    SkeletonFile r; std::string err;
    CHECK(ParseSkeleton(SkeletonToJson(s), r, err));
    CHECK(err.empty());
    CHECK(r.version == 1 && r.names == s.names && r.parents == s.parents && r.conv == s.conv);
    CHECK(r.refPos.size() == s.refPos.size());
    for (size_t i = 0; i < s.refPos.size() && i < r.refPos.size(); i++) CHECK_NEAR(r.refPos[i], s.refPos[i], 1e-3);
}

TEST(skeleton_rejects_bad_files) {
    SkeletonFile r; std::string err;
    {   SkeletonFile s = Ref(); s.version = 2; err.clear(); CHECK(!ParseSkeleton(SkeletonToJson(s), r, err)); CHECK(!err.empty()); }
    {   SkeletonFile s = Ref(); s.parents.pop_back(); err.clear(); CHECK(!ParseSkeleton(SkeletonToJson(s), r, err)); CHECK(!err.empty()); }
    {   SkeletonFile s = Ref(); s.parents[3] = 5; err.clear(); CHECK(!ParseSkeleton(SkeletonToJson(s), r, err)); CHECK(!err.empty()); }
    {   SkeletonFile s = Ref(); s.conv[10] = 4; err.clear(); CHECK(!ParseSkeleton(SkeletonToJson(s), r, err)); CHECK(!err.empty()); }
    {   SkeletonFile s = Ref(); s.refPos.pop_back(); err.clear(); CHECK(!ParseSkeleton(SkeletonToJson(s), r, err)); CHECK(!err.empty()); }
    {   const std::string t = SkeletonToJson(Ref()); err.clear(); CHECK(!ParseSkeleton(t.substr(0, t.size() / 2), r, err)); CHECK(!err.empty()); }
    {   err.clear(); CHECK(!ParseSkeleton("", r, err)); CHECK(!err.empty()); }
}
