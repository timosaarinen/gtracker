#ifndef GTRACKER_CORE_H
#define GTRACKER_CORE_H

#include <stddef.h>
#include <stdint.h>

#define GT_VERSION "0.5.0"
#define GT_VERSION_MAJOR 0
#define GT_VERSION_MINOR 5
#define GT_VERSION_PATCH 0
#define GT_MAX_STRINGS 12

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gt_result {
    GT_OK = 0,
    GT_INVALID_ARGUMENT = 1,
    GT_OUT_OF_MEMORY = 2,
    GT_INTERNAL_ERROR = 3
} gt_result;

typedef struct gt_config {
    uint32_t sample_rate;       /* 8000..192000 Hz. */
    uint32_t string_count;      /* 1..12 independent strings; not courses yet. */
    uint32_t seed;
    float tuning_hz[GT_MAX_STRINGS]; /* Active strings: 20..2000 Hz, <= rate/4. */
    float decay_seconds;        /* 0.02..60; approximate low-frequency T60. */
    float brightness;           /* 0..1; higher values lose fewer harmonics. */
    float body_mix;             /* 0..1; modal body contribution. */
} gt_config;

typedef enum gt_event_type {
    GT_EVENT_PLUCK = 1,
    GT_EVENT_DAMP = 2,
    GT_EVENT_BODY_HIT = 3
} gt_event_type;

typedef struct gt_event {
    uint64_t frame;             /* Applied before this zero-based sample. */
    gt_event_type type;
    uint32_t string_index;      /* Zero-based; 0 is the highest tab lane. */
    float fret;                /* 0..36; fractional frets are permitted. */
    float strength;            /* 0..1; damping: 1 means immediate silence. */
} gt_event;

typedef struct gt_synth gt_synth;

typedef enum gt_model {
    GT_MODEL_BASELINE_0_1 = 0,
    GT_MODEL_CONTACT_0_2 = 1, /* Experimental moving-delay contact; not calibrated physics. */
    GT_MODEL_RETAINED_0_5 = 2 /* Retained repluck vibration + contact; explicit experimental choice. */
} gt_model;

typedef struct gt_fret_point {
    uint64_t frame_offset; /* Relative to the slide start; first must be zero. */
    float fret;           /* 0..36; linearly interpolated at native sample frames. */
} gt_fret_point;

typedef struct gt_slide {
    uint64_t frame;
    uint32_t string_index;
    const gt_fret_point* points; /* Copied by gt_set_performance; never retained. */
    size_t point_count;         /* 2..256; offsets strictly increase. */
} gt_slide;

/* Static string, never NULL; unknown result codes return "unknown result". */
const char* gt_result_string(gt_result result);
/* Static source/toolchain digest plus actual build configuration; never NULL. */
const char* gt_renderer_build_id(void);

/* 48 kHz, six strings ordered E4 B3 G3 D3 A2 E2, seed 42. */
gt_config gt_default_config(void);

/* On failure, *out_synth is NULL when out_synth is non-NULL. */
gt_result gt_create(const gt_config* config, gt_synth** out_synth);
/* Explicit model selection. gt_create always selects the preserved 0.1 baseline. */
gt_result gt_create_model(const gt_config* config, gt_model model, gt_synth** out_synth);
const char* gt_model_name(gt_model model);
void gt_destroy(gt_synth* synth); /* NULL is allowed. */

/*
 * Copy up to 1,000,000 events, sorted by nondecreasing frame, then reset.
 * Equal-frame events execute in their supplied order. A zero count clears the
 * performance; events may then be NULL. Failure leaves the existing state intact.
 * PLUCK replaces vibration in baseline/contact. retained_0_5 preserves a
 * weighted old displacement and adds new excitation; zero strength preserves
 * vibration there. DAMP ignores fret and is the explicit way to silence it.
 * BODY_HIT ignores string_index and fret. All supplied floats must still be finite,
 * fret must be 0..36, and strength must be 0..1. A plucked pitch must be <= rate/4.
 */
gt_result gt_set_events(gt_synth* synth, const gt_event* events, size_t count);

/*
 * Copy events plus <=16,384 slides / <=65,536 total points, sorted by slide start.
 * Slides require CONTACT_0_2 or RETAINED_0_5, retain vibration and do not excite a silent string.
 * Same-string slides may touch but cannot overlap. A pluck at a slide start must
 * have its first fret; plucks strictly inside a slide are rejected. At its end,
 * a same-frame pluck takes precedence. Damping remains effective during slides.
 * Failure preserves the entire prior performance/state. No pointers are retained.
 * gt_set_events is equivalent to this call with no slides, clearing old curves.
 */
gt_result gt_set_performance(gt_synth* synth, const gt_event* events, size_t event_count,
                             const gt_slide* slides, size_t slide_count);

/* Rewind to frame zero, preserving configuration/events and restoring the seed. */
gt_result gt_reset(gt_synth* synth);

/*
 * Write mono float PCM in [-1,1]. No allocations occur during rendering.
 * mono may be NULL only when frames is zero. Calls on an instance must be
 * serialized by the caller; separate instances are independent. Within the same
 * build, output is bit-identical regardless of render-block partitioning.
 */
gt_result gt_render(gt_synth* synth, float* mono, size_t frames);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GTRACKER_CORE_H */
