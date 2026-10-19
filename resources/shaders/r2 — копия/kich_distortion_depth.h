#ifndef KICH_DISTORTION_DEPTH_H
#define KICH_DISTORTION_DEPTH_H

// A cleared position pixel (z == 0) is background, not a near surface.
// Do not drag geometry across a sky silhouette in either direction. For two
// real surfaces retain the original rejection of a nearer offset sample.
float kich_distortion_accept(float base_depth, float sample_depth)
{
    bool base_geometry = base_depth > 0.0f;
    bool sample_geometry = sample_depth > 0.0f;
    bool same_coverage = base_geometry == sample_geometry;
    bool foreground = sample_geometry && (sample_depth + 0.001f < base_depth);
    return (same_coverage && !foreground) ? 1.0f : 0.0f;
}

#endif
