#include "gtracker/core.h"
#include <string.h>

/* Compiled as C and linked to the same C++ core used by the applications. */
int main(void) {
    gt_config config = gt_default_config();
    gt_synth* synth = NULL;
    float silence[8];
    gt_event invalid = {0, (gt_event_type)99, 0, 0.0f, 1.0f};
    size_t i;
    if (gt_create(&config, &synth) != GT_OK) {
        return 1;
    }
    if (gt_render(synth, silence, 8) != GT_OK) {
        gt_destroy(synth);
        return 2;
    }
    for (i = 0; i < 8; ++i) {
        if (silence[i] != 0.0f) {
            gt_destroy(synth);
            return 3;
        }
    }
    if (gt_set_events(synth, &invalid, 1) != GT_INVALID_ARGUMENT ||
        strcmp(gt_result_string((gt_result)99), "unknown result") != 0) {
        gt_destroy(synth);
        return 4;
    }
    gt_destroy(synth);
    return GT_VERSION_MAJOR != 0 || GT_VERSION_MINOR != 1;
}
