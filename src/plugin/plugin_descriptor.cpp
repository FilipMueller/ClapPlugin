#include "plugin/delay_plugin.h"

#include <new>

#include <clap/plugin-features.h>
#include <clap/version.h>

const clap_plugin_descriptor_t* DelayPlugin::descriptor() noexcept {
    static const char* features[] = {
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
        CLAP_PLUGIN_FEATURE_DELAY,
        CLAP_PLUGIN_FEATURE_STEREO,
        nullptr,
    };

    static const clap_plugin_descriptor_t desc {
        CLAP_VERSION_INIT,
        "de.filipmueller.delay",
        "Delay",
        "Filip Mueller",
        "",
        "",
        "",
#ifdef DELAY_VERSION
        DELAY_VERSION,
#else
        "0.1.0",
#endif
        "Real-time stereo delay effect and measurement platform built with the CLAP plugin API.",
        features,
    };

    return &desc;
}

const clap_plugin_t* createDelayPlugin(const clap_host_t* host) noexcept {
    auto* plugin = new (std::nothrow) DelayPlugin(host);
    return plugin ? plugin->clapPlugin() : nullptr;
}

const clap_plugin_descriptor_t* getDelayPluginDescriptor() noexcept {
    return DelayPlugin::descriptor();
}
