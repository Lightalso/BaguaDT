#pragma once
#include "Scene.h"
namespace bagua {
struct ImportOptions {
    double metresPerUnit = 1;
    bool zUp = false;
    bool fixed = true;
};
class Assets {
public:
    static bool importFile(const QString &path, QVector<Object> &objects, QString *error = nullptr,
                           ImportOptions options = {});
};
} // namespace bagua
