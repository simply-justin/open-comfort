#include "src/Protocols/Matter/MatterAdapter.hpp"

extern "C" {
    #include "esp_log.h"
}

static const char* TAG = "MatterAdapter";

MatterAdapter::MatterAdapter(Ac* acModule)
    : acModule_(acModule)
{}

bool MatterAdapter::init(bool required) {
    if (!acModule_) {
        ESP_LOGW(TAG, "Matter adapter disabled: AC module unavailable");
        return !required;
    }

    // ESP Matter is intentionally isolated here. The dependency is not present
    // in this repository yet, so local web control can remain buildable while
    // Matter support is added behind this adapter.
    ESP_LOGW(TAG, "ESP Matter component is not configured; Matter adapter disabled");
    enabled_ = false;
    return !required;
}

void MatterAdapter::publishState() {
    if (!enabled_ || !acModule_) return;

    // TODO: Publish AC state to Matter clusters after ESP Matter is wired.
}
