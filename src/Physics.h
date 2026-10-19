#pragma once
#include "Hsc.h"
#include "Scene.h"
#include <memory>
namespace bagua {
// Engine objects live only in this runtime; authored scene and undo history are untouched.
class Physics {
public:
    Physics();
    ~Physics();
    static bool available();
    bool start(const Scene &source, QString *error = nullptr, HscOptions options = {});
    bool step(int count = 1, QString *error = nullptr);
    void reset();
    bool active() const;
    double time() const;
    int contacts() const;
    QString solverDescription() const;
    HscStats hscStats() const;
    bool hscActive() const;
    Scene preview;
    static constexpr double timeStep = 1.0 / 240.0;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace bagua
