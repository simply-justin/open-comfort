#pragma once

namespace opencomfort {

class IModule {
public:
    virtual ~IModule() = default;
    virtual void bootstrap() = 0;
    virtual bool isBooted() const = 0;
    virtual void run() = 0;
};

}  // namespace opencomfort
