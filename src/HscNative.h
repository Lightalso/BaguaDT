#pragma once
#include "Scene.h"
#include "Hsc.h"
#include <memory>
namespace bagua {
class HscNative {
public:
    HscNative();
    ~HscNative();
    static bool available();
    bool start(const Scene &source,Scene &preview,const HscOptions &options,QString *error);
    bool step(int count,Scene &preview,QString *error);
    double time() const;
    HscStats stats() const;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
