#include <stdio.h>

#include "morpheus_frame_scheduler.h"

int main(void)
{
    if (morph_frame_wait_timeout_ms(0) != 0) return 1;
    if (morph_frame_wait_timeout_ms(1) != MORPHEUS_FRAME_WAIT_MS) return 2;
    if (MORPHEUS_FRAME_WAIT_MS <= 0 || MORPHEUS_FRAME_WAIT_MS > 16) return 3;
    puts("PASS: frame scheduling caps the busy-spin without throttling updates");
    return 0;
}
