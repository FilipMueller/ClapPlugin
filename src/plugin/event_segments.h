#pragma once

#include <algorithm>
#include <cstdint>

#include <clap/clap.h>
#include <clap/events.h>

// Splits one process() block at every event timestamp and hands the resulting
// segments to the caller.
//
// The callback takes a half-open frame range [begin, end) rather than a single
// frame. That is what allows the caller to derive its DSP coefficients once per
// segment instead of once per sample, which is the whole point of the split:
// parameter values cannot change inside a segment by definition, because a
// segment is bounded by the events that would change them.
//
// handleEvent(const clap_event_header_t*) is invoked for every event, in the
// sample-sorted order the host guarantees. Events timestamped at or beyond the
// end of the block are still delivered, so that parameter state stays correct
// for the next block.

template <typename HandleEvent, typename ProcessRange>
void processByEventSegments(const clap_process_t* process,
                            HandleEvent&& handleEvent,
                            ProcessRange&& processRange) noexcept {
    const uint32_t frames = process->frames_count;
    const clap_input_events_t* in = process->in_events;
    const uint32_t eventCount = in ? in->size(in) : 0;

    uint32_t eventIndex = 0;
    uint32_t frame = 0;

    while (frame < frames) {
        while (eventIndex < eventCount) {
            const clap_event_header_t* event = in->get(in, eventIndex);

            if (!event) {
                ++eventIndex;
                continue;
            }

            if (event->time > frame) {
                break;
            }

            handleEvent(event);
            ++eventIndex;
        }

        uint32_t segmentEnd = frames;

        if (eventIndex < eventCount) {
            const clap_event_header_t* next = in->get(in, eventIndex);
            if (next) {
                segmentEnd = std::min<uint32_t>(next->time, frames);
            }
        }

        processRange(frame, segmentEnd);
        frame = segmentEnd;
    }

    while (eventIndex < eventCount) {
        if (const clap_event_header_t* event = in->get(in, eventIndex)) {
            handleEvent(event);
        }
        ++eventIndex;
    }
}
