#pragma once

#include <algorithm>
#include <cstdint>

#include <clap/clap.h>
#include <clap/events.h>

template <typename HandleEvent, typename ProcessFrame>
void processByEventSegments(const clap_process_t* process,
                            HandleEvent&& handleEvent,
                            ProcessFrame&& processFrame) noexcept {
    const uint32_t frames = process->frames_count;
    const uint32_t eventCount = process->in_events ? process->in_events->size(process->in_events) : 0;
    uint32_t eventIndex = 0;
    uint32_t nextEventFrame = 0;

    while (eventIndex < eventCount) {
        const clap_event_header_t* event = process->in_events->get(process->in_events, eventIndex);
        if (event && event->time == 0) {
            handleEvent(event);
            ++eventIndex;
        } else {
            nextEventFrame = event ? std::min<uint32_t>(event->time, frames) : frames;
            break;
        }
    }

    if (eventIndex >= eventCount) {
        nextEventFrame = frames;
    }

    for (uint32_t frame = 0; frame < frames;) {
        while (eventIndex < eventCount && nextEventFrame == frame) {
            const clap_event_header_t* event = process->in_events->get(process->in_events, eventIndex);
            if (!event) {
                ++eventIndex;
                continue;
            }

            if (event->time != frame) {
                nextEventFrame = std::min<uint32_t>(event->time, frames);
                break;
            }

            handleEvent(event);
            ++eventIndex;

            if (eventIndex < eventCount) {
                const clap_event_header_t* nextEvent = process->in_events->get(process->in_events, eventIndex);
                nextEventFrame = nextEvent ? std::min<uint32_t>(nextEvent->time, frames) : frames;
            } else {
                nextEventFrame = frames;
            }
        }

        const uint32_t endFrame = std::max(frame + 1, nextEventFrame);
        for (; frame < endFrame && frame < frames; ++frame) {
            processFrame(frame);
        }
    }
}
