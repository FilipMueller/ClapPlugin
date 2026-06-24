#include "entry.h"
#include "plugin.h"

#include <atomic>
#include <cstring>

#include <clap/clap.h>
#include <clap/factory/plugin-factory.h>
#include <clap/version.h>

namespace {
std::atomic<int> entryInitCounter {0};

bool CLAP_ABI entryInit(const char*) noexcept {
    entryInitCounter.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void CLAP_ABI entryDeinit() noexcept {
    entryInitCounter.fetch_sub(1, std::memory_order_relaxed);
}

uint32_t CLAP_ABI pluginFactoryGetPluginCount(const clap_plugin_factory_t*) noexcept {
    return 1;
}

const clap_plugin_descriptor_t* CLAP_ABI pluginFactoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index) noexcept {
    return index == 0 ? getDelayPluginDescriptor() : nullptr;
}

const clap_plugin_t* CLAP_ABI pluginFactoryCreatePlugin(const clap_plugin_factory_t*,
                                                        const clap_host_t* host,
                                                        const char* pluginId) noexcept {
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version)) {
        return nullptr;
    }

    const clap_plugin_descriptor_t* desc = getDelayPluginDescriptor();
    if (std::strcmp(pluginId, desc->id) != 0) {
        return nullptr;
    }

    return createDelayPlugin(host);
}

const clap_plugin_factory_t pluginFactory {
    pluginFactoryGetPluginCount,
    pluginFactoryGetPluginDescriptor,
    pluginFactoryCreatePlugin,
};

const void* CLAP_ABI entryGetFactory(const char* factoryId) noexcept {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &pluginFactory;
    }
    return nullptr;
}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
