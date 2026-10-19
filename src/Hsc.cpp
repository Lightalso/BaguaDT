#include "Hsc.h"
#include <Eigen/Dense>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
namespace bagua {
namespace {
using Mat = Eigen::MatrixXd;
using Vec = Eigen::VectorXd;
void require(bool yes, const char *message) {
    if (!yes)
        throw std::runtime_error(message);
}
Mat inverseBlocks(const Mat &m, const std::vector<int> &blocks) {
    Mat result = Mat::Zero(m.rows(), m.cols());
    int offset = 0;
    for (int size : blocks) {
        require(size > 0 && offset + size <= m.rows(), "Invalid HSC block partition");
        Mat block = m.block(offset, offset, size, size);
        Eigen::LLT<Mat> factor(block);
        require(factor.info() == Eigen::Success, "HSC requires positive-definite diagonal blocks");
        result.block(offset, offset, size, size) = factor.solve(Mat::Identity(size, size));
        offset += size;
    }
    require(offset == m.rows(), "HSC blocks must cover the entire subspace");
    return result;
}
Mat polynomial(const Mat &h, const Mat &inverse, const HscOptions &o) {
    Mat m = o.omega * inverse, term = m, result = m;
    for (int i = 1; i < o.neumannTerms; ++i) {
        term = (term - m * h * term).eval();
        result += term;
    }
    return ((result + result.transpose()) * .5).eval();
}
Vec pcg(const std::function<Vec(const Vec &)> &apply, const Mat &preconditioner, const Vec &b, const HscOptions &o,
        HscStats &stats) {
    Vec x = Vec::Zero(b.size());
    const double norm = b.norm();
    if (!norm)
        return x;
    Vec r = b, z = preconditioner * r, p = z;
    double rz = r.dot(z);
    for (int i = 0; i < o.maxIterations; ++i) {
        Vec ap = apply(p);
        double denominator = p.dot(ap);
        require(std::isfinite(denominator) && denominator > 0 && std::isfinite(rz) && rz > 0,
                "HSC Schur PCG encountered a non-SPD operator");
        x += (rz / denominator) * p;
        r = b - apply(x);
        ++stats.iterations;
        if (r.norm() / norm <= std::min(1e-5, o.tolerance * .1))
            return x;
        z = preconditioner * r;
        double next = r.dot(z);
        p = z + (next / rz) * p;
        rz = next;
    }
    throw std::runtime_error("HSC Schur PCG iteration limit reached; no solver fallback was used");
}
} // namespace
bool solveHsc(const std::vector<double> &matrix, int nf, const std::vector<int> &blocks, const std::vector<double> &rhs,
              const HscOptions &o, std::vector<double> &solution, HscStats &stats, QString *error) {
    const auto started = std::chrono::steady_clock::now();
    HscStats next;
    try {
        int n = int(rhs.size()), nq = n - nf;
        require(n > 0 && n <= 1536 && nf > 0 && nf % 3 == 0 && nq > 0 && matrix.size() == size_t(n) * n,
                "HSC reference adapter requires mixed FEM/rigid DOFs and at most 1536 active DOFs");
        require(o.maxRank > 0 && o.neumannTerms > 0 && o.neumannTerms <= 32 && o.maxIterations > 0 &&
                    o.maxCorrections > 0 && std::isfinite(o.omega) && o.omega > 0 && o.omega <= 1 &&
                    std::isfinite(o.tolerance) && o.tolerance > 0 && o.tolerance < 1,
                "Invalid HSC parameters");
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> original(matrix.data(),
                                                                                                          n, n);
        Eigen::Map<const Vec> b(rhs.data(), n);
        require(original.allFinite() && b.allFinite(), "Non-finite HSC input");
        double asymmetry = (original - original.transpose()).norm() / std::max(1., original.norm());
        next.asymmetry = asymmetry;
        if (asymmetry > 1e-4)
            throw std::runtime_error(
                QString("HSC requires symmetry; relative asymmetry = %1").arg(asymmetry, 0, 'g', 8).toStdString());
        Mat h = (original + original.transpose()) * .5;
        Mat d = h.topLeftCorner(nf, nf), a = h.bottomRightCorner(nq, nq), e = h.bottomLeftCorner(nq, nf);
        Mat p = polynomial(a, inverseBlocks(a, blocks), o);
        if (o.adaptiveInverse) {
            Mat identity = Mat::Identity(nq, nq);
            // Optional enhancement: an ill-conditioned block keeps the fixed-order polynomial
            // instead of failing the whole solve.
            for (;;) {
                double inverseResidual = (identity - a * p).norm() / std::sqrt(double(nq));
                if (std::isfinite(inverseResidual) && inverseResidual <= 1e-5)
                    break;
                if (next.inverseDoublings >= 32 || !p.allFinite() || p.norm() > 1e100) {
                    p = polynomial(a, inverseBlocks(a, blocks), o);
                    next.inverseDoublings = 0;
                    next.inverseFallbacks = 1;
                    break;
                }
                p = (p + p * (identity - a * p)).eval();
                p = ((p + p.transpose()) * .5).eval();
                ++next.inverseDoublings;
            }
        }
        Mat residual = e, u(nq, 0), v(nf, 0);
        double scale = e.norm();
        for (int i = 0; i < std::min({o.maxRank, nq, nf}) && scale > 0; ++i) {
            Eigen::Index row, col;
            residual.cwiseAbs().maxCoeff(&row, &col);
            double pivot = residual(row, col);
            if (std::abs(pivot) <= std::numeric_limits<double>::epsilon() * scale)
                break;
            Vec uc = residual.col(col), vc = residual.row(row).transpose() / pivot;
            u.conservativeResize(nq, i + 1);
            v.conservativeResize(nf, i + 1);
            u.col(i) = uc;
            v.col(i) = vc;
            residual.noalias() -= uc * vc.transpose();
            ++next.rank;
        }
        next.acaResidual = scale ? residual.norm() / scale : 0;
        Mat c = u.transpose() * p * u;
        c = ((c + c.transpose()) * .5).eval();
        Mat pre = polynomial(d, inverseBlocks(d, std::vector<int>(size_t(nf / 3), 3)), o);
        auto apply = [&](const Vec &x) -> Vec { return d * x - v * (c * (v.transpose() * x)); };
        Vec x = Vec::Zero(n);
        double norm = b.norm();
        next.relativeResidual = norm ? 1 : 0;
        while (norm && next.relativeResidual > o.tolerance) {
            require(next.corrections < o.maxCorrections,
                    "HSC full-system residual correction limit reached; no fallback was used");
            Vec r = b - original * x;
            Vec dx = pcg(apply, pre, Vec(r.head(nf) - v * (u.transpose() * (p * r.tail(nq)))), o, next);
            Vec direction(n);
            direction.head(nf) = dx;
            direction.tail(nq) = p * (r.tail(nq) - u * (v.transpose() * dx));
            double numerator = r.dot(direction), denominator = direction.dot(original * direction);
            require(numerator > 0 && denominator > 0 && direction.allFinite(),
                    "HSC correction is not a descent direction");
            x += std::min(1., numerator / denominator) * direction;
            next.relativeResidual = (b - original * x).norm() / norm;
            ++next.corrections;
        }
        solution.assign(x.data(), x.data() + x.size());
        next.calls = 1;
        next.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        stats = next;
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
} // namespace bagua
