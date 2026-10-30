#pragma once
namespace bagua {
struct HscOptions {
    bool enabled = false;
    int maxRank = 48, neumannTerms = 5, maxIterations = 2000, maxCorrections = 40;
    double omega = .7, tolerance = 1e-8, linearTolerance = 1e-4;
    bool nativeAdaptiveInverse = false;
    bool adaptiveInverse = true;
    bool useCuda = true, residualCorrection = true, nativePipeline = true;
};
struct HscStats {
    bool cuda = false;
    int contactCount = 0, newtonIterations = 0, lineSearchTrials = 0;
    bool converged = false;
    double ccdStep = 1;
    int nonzeros = 0, factorNonzeros = 0;
    int calls = 0, rank = 0, iterations = 0, corrections = 0, inverseDoublings = 0;
    int inverseFallbacks = 0;
    double relativeResidual = 0, acaResidual = 0, asymmetry = 0, milliseconds = 0;
};
}
