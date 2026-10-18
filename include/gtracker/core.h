#ifndef GTRACKER_CORE_H
#define GTRACKER_CORE_H

#include <stddef.h>
#include <stdint.h>

#define GT_VERSION "0.1.0"
#define GT_VERSION_MAJOR 0
#define GT_VERSION_MINOR 1
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

/* Static string, never NULL; unknown result codes return "unknown result". */
const char* gt_result_string(gt_result result);

/* 48 kHz, six strings ordered E4 B3 G3 D3 A2 E2, seed 42. */
gt_config gt_default_config(void);

/* On failure, *out_synth is NULL when out_synth is non-NULL. */
gt_result gt_create(const gt_config* config, gt_synth** out_synth);
void gt_destroy(gt_synth* synth); /* NULL is allowed. */

/*
 * Copy up to 1,000,000 events, sorted by nondecreasing frame, then reset.
 * Equal-frame events execute in their supplied order. A zero count clears the
 * performance; events may then be NULL. Failure leaves the existing state intact.
 * PLUCK replaces this string's vibration and sets its fret. DAMP ignores fret.
 * BODY_HIT ignores string_index and fret. All supplied floats must still be finite,
 * fret must be 0..36, and strength must be 0..1. A plucked pitch must be <= rate/4.
 */
gt_result gt_set_events(gt_synth* synth, const gt_event* events, size_t count);

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
