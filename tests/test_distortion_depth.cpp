#include "../resources/shaders/r2/kich_distortion_depth.h"
#include <cassert>
#include <cstdio>
int main()
{
    // Empty sky, sky/foliage in either direction, and overlapping real objects.
    assert(kich_distortion_accept(0.f, 0.f) == 1.f);
    assert(kich_distortion_accept(0.f, 30.f) == 0.f);
    assert(kich_distortion_accept(30.f, 0.f) == 0.f);
    assert(kich_distortion_accept(30.f, 10.f) == 0.f);
    assert(kich_distortion_accept(10.f, 30.f) == 1.f);
    assert(kich_distortion_accept(30.f, 30.f) == 1.f);
    assert(kich_distortion_accept(30.f, 29.9999f) == 1.f);
    // Preserve the old geometry-only comparison over representative depths.
    for (int a=1; a<1000; ++a)
        for (int b=1; b<1000; ++b)
            assert(kich_distortion_accept(float(a), float(b)) == (b < a ? 0.f : 1.f));
    std::puts("PASS: real shader helper, sky coverage, geometry compatibility");
}
