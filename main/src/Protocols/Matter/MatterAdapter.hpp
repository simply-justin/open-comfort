#pragma once

#include "src/Modules/Ac/Ac.hpp"

class MatterAdapter {
public:
    explicit MatterAdapter(Ac* acModule);

    bool init(bool required = false);
    bool isEnabled() const { return enabled_; }
    void publishState();

private:
    Ac* acModule_;
    bool enabled_ = false;
};
