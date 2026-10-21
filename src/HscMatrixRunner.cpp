// Replay a frozen native Newton system without rebuilding the contact scene.
#include "HscCudaDevice.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cuda_runtime.h>
#include <vector>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "Usage: BaguaHscMatrixRunner MATRIX.bin\n");
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly))
        return 2;
    auto header = QJsonDocument::fromJson(file.readLine()).object();
    int n = header["n"].toInt(), count = header["blocks"].toInt();
    if (n <= 0 || count <= 0 || count > 10000000 || n > 10000000)
        return 2;
    auto packed = file.read(qint64(count) * sizeof(bagua::HscDeviceBlock));
    auto rhs = file.read(qint64(n) * sizeof(double));
    if (packed.size() != qint64(count) * sizeof(bagua::HscDeviceBlock) || rhs.size() != qint64(n) * sizeof(double) ||
        !file.atEnd())
        return 2;
    bagua::HscDeviceBlock *blocks = nullptr;
    double *b = nullptr, *x = nullptr;
    auto check = [](cudaError_t code) {
        if (code != cudaSuccess) {
            std::fprintf(stderr, "%s\n", cudaGetErrorString(code));
            std::exit(3);
        }
    };
    check(cudaMalloc(reinterpret_cast<void **>(&blocks), packed.size()));
    check(cudaMalloc(reinterpret_cast<void **>(&b), rhs.size()));
    check(cudaMalloc(reinterpret_cast<void **>(&x), rhs.size()));
    check(cudaMemcpy(blocks, packed.constData(), packed.size(), cudaMemcpyHostToDevice));
    check(cudaMemcpy(b, rhs.constData(), rhs.size(), cudaMemcpyHostToDevice));
    bagua::HscOptions options;
    options.maxRank = header["maxRank"].toInt();
    options.neumannTerms = header["neumannTerms"].toInt();
    options.maxIterations = header["maxIterations"].toInt();
    options.omega = header["omega"].toDouble();
    options.tolerance = header["tolerance"].toDouble();
    options.adaptiveInverse = header["adaptiveInverse"].toBool();
    options.residualCorrection = false;
    bagua::HscStats stats;
    std::string error;
    bool ok = bagua::solveHscDeviceBlocks(blocks, count, b, x, n, header["fo"].toInt(), header["nf"].toInt(),
                                          header["qo"].toInt(), header["nq"].toInt(), options, stats, error);
    std::printf("%s rank=%d iterations=%d residual=%.12g milliseconds=%.3f %s\n", ok ? "PASS" : "FAIL", stats.rank,
                stats.iterations, stats.relativeResidual, stats.milliseconds, error.c_str());
    cudaFree(x);
    cudaFree(b);
    cudaFree(blocks);
    return ok ? 0 : 1;
}
