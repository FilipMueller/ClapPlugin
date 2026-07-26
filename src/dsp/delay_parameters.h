#pragma once

struct DelayParameters {
    bool bypassed = false;
    double delayMs = 350.0;
    double feedback = 0.35;
    double mix = 0.35;
    double outputDb = 0.0;
    double dspComplexity = 0.0;
};
