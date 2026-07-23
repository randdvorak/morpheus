#ifndef MORPHEUS_FRAME_SCHEDULER_H_
#define MORPHEUS_FRAME_SCHEDULER_H_

#define MORPHEUS_FRAME_WAIT_MS 8

static int
morph_frame_wait_timeout_ms(
    unsigned long long attempted_frame_count)
{
    if (!attempted_frame_count) return 0;
    return MORPHEUS_FRAME_WAIT_MS;
}

#endif
