#pragma once
#include <sparse/ISolver.h>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

// M_PI is a compiler extension, not standard C++, and isn't reliably
// defined on MSVC - define our own so this builds the same on every platform.
constexpr double kPi = 3.14159265358979323846;

// A single satellite: fixed ECEF-like position, plus the noise that
// actually corrupted its measurement and the weight the solver assumes
// for it - kept separate on purpose (see WeightingStrategy below).
struct Satellite
{
    double X = 0, Y = 0, Z = 0;
    double trueSigma = 1.0;  // physical measurement std-dev (meters) - always elevation-based
    double weight = 1.0;     // assumed weight the solver uses - depends on WeightingStrategy
    double pseudorange = 0;  // noisy measured pseudorange to the true receiver
    double azimuth = 0;      // radians, for sky-plot drawing
    double elevation = 0;    // radians, for sky-plot drawing
};

// How the solver decides each satellite's weight. TrueElevation matches
// the physical noise model exactly (the "correct" choice). Uniform and
// ElevationSquared are deliberately mismatched, to demonstrate what
// happens when the assumed weighting doesn't match reality - exactly
// the "weighting strategies" experiment the proposal calls for.
enum class WeightingStrategy { Uniform, TrueElevation, ElevationSquared };

// Receiver state: X, Y, Z position + clock bias b (all in meters).
struct GNSSState
{
    double X = 0, Y = 0, Z = 0, b = 0;
};

class GNSSModel
{
protected:
    std::vector<Satellite> _sats;
    GNSSState _truePos;
    double _sigma0 = 3.0;       // baseline pseudorange noise at zenith (m)
    double _elevationMaskDeg = 15.0;
    WeightingStrategy _weightStrategy = WeightingStrategy::TrueElevation;
    std::mt19937 _rng{ 42 };    // fixed seed -> reproducible runs while debugging

    static double distance(double x, double y, double z, const Satellite& s)
    {
        double dx = x - s.X, dy = y - s.Y, dz = z - s.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Recomputes every satellite's *assumed* weight from the current
    // strategy - never touches trueSigma or the measured pseudorange, so
    // this can be called on its own to re-weight the exact same noisy
    // data differently, which is the controlled experiment we want.
    void recomputeWeights()
    {
        for (auto& s : _sats)
        {
            switch (_weightStrategy)
            {
                case WeightingStrategy::Uniform:
                    s.weight = 1.0;
                    break;
                case WeightingStrategy::TrueElevation:
                    s.weight = 1.0 / (s.trueSigma * s.trueSigma);
                    break;
                case WeightingStrategy::ElevationSquared:
                {
                    // assumes noise ~ sigma0/sin^2(el) instead of the true sigma0/sin(el)
                    double assumedSigma = _sigma0 / (std::sin(s.elevation) * std::sin(s.elevation));
                    s.weight = 1.0 / (assumedSigma * assumedSigma);
                    break;
                }
            }
        }
    }

    // Cost only (no matrix build) - used by the line search below.
    double costAt(const GNSSState& x) const
    {
        double cost = 0.0;
        for (const auto& s : _sats)
        {
            double predicted = distance(x.X, x.Y, x.Z, s) + x.b;
            double r = s.pseudorange - predicted;
            cost += s.weight * r * r;
        }
        return 0.5 * cost;
    }

    // Shared by both steps below: linearizes around x and accumulates
    // G = J'WJ (4x4) and rhs = J'Wr (4x1) by hand. This is deliberately
    // plain C++, not natID's IMatrix::calcGainAndRHS - that function
    // crashes inside Matrix.dll (an internal assert in MultRowSorter.h)
    // on this machine, and for a 4-column Jacobian there's no real
    // sparsity to exploit anyway, so a hand-rolled loop is both simpler
    // and sidesteps the bug entirely. The actual linear solve below
    // still goes through natID's sparse solver.
    void buildNormalEquations(const GNSSState& x, double G[4][4], double rhs[4], double& outCost) const
    {
        for (int a = 0; a < 4; ++a)
        {
            rhs[a] = 0.0;
            for (int b = 0; b < 4; ++b)
                G[a][b] = 0.0;
        }
        outCost = 0.0;

        for (const auto& s : _sats)
        {
            double dx = x.X - s.X, dy = x.Y - s.Y, dz = x.Z - s.Z;
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 1.0) dist = 1.0; // guard against a degenerate initial guess
            double predicted = dist + x.b;
            double r = s.pseudorange - predicted;
            double weight = s.weight;

            double J[4] = { dx / dist, dy / dist, dz / dist, 1.0 };

            for (int a = 0; a < 4; ++a)
            {
                rhs[a] += weight * J[a] * r;
                for (int b = 0; b < 4; ++b)
                    G[a][b] += weight * J[a] * J[b];
            }

            outCost += weight * r * r;
        }
        outCost *= 0.5;
    }

public:
    // One Gauss-Newton step: build the normal equations by hand (see
    // above), then hand G/rhs to natID's sparse LDLT solver to actually
    // solve the 4x4 system - this is the framework's sparse solver doing
    // the real linear algebra, exactly as required.
    GNSSState gaussNewtonStep(const GNSSState& x, double& outCost) const
    {
        double G[4][4], rhs[4];
        buildNormalEquations(x, G, rhs, outCost);

        sparse::DblSolverReleaser p_solver(
            sparse::createDblSolver(4, 10, sparse::Symmetry::SymmetricPosDef, sparse::SolverType::LDLT)
        );
        sparse::DblSolver& solver = *(p_solver.ptr());

        // SymmetricPosDef only needs the upper triangle.
        for (int a = 0; a < 4; ++a)
            for (int b = a; b < 4; ++b)
                solver.addTriple(a, b, G[a][b]);

        for (int a = 0; a < 4; ++a)
            solver.setRHS(a, rhs[a]);

        if (!solver.factorize())
            return GNSSState{}; // caller should check for stalled convergence

        solver.solve();

        GNSSState delta;
        delta.X = solver.x(0);
        delta.Y = solver.x(1);
        delta.Z = solver.x(2);
        delta.b = solver.x(3);
        return delta;
    }

    // One gradient-descent step. The descent direction is exactly the
    // rhs (J'Wr) from the same normal-equations build above - gradient
    // descent doesn't solve a linear system at all (that's the whole
    // point of contrasting it with Gauss-Newton), so no solver call
    // here, just a backtracking line search on the step size.
    GNSSState gradientDescentStep(const GNSSState& x, double& outCost, double& outAlpha,
                                   double initialAlpha = 1.0, int maxBacktrack = 40) const
    {
        double G[4][4], rhs[4];
        buildNormalEquations(x, G, rhs, outCost); // G unused here, only rhs

        GNSSState grad;
        grad.X = rhs[0];
        grad.Y = rhs[1];
        grad.Z = rhs[2];
        grad.b = rhs[3];

        double alpha = initialAlpha;
        for (int i = 0; i < maxBacktrack; ++i)
        {
            GNSSState candidate = x;
            candidate.X += alpha * grad.X;
            candidate.Y += alpha * grad.Y;
            candidate.Z += alpha * grad.Z;
            candidate.b += alpha * grad.b;

            if (costAt(candidate) < outCost)
                break;
            alpha *= 0.5;
        }
        outAlpha = alpha;

        GNSSState delta;
        delta.X = alpha * grad.X;
        delta.Y = alpha * grad.Y;
        delta.Z = alpha * grad.Z;
        delta.b = alpha * grad.b;
        return delta;
    }

    // Places nSat satellites at random azimuth/elevation around the true
    // receiver position, at a fixed slant range typical of GNSS geometry,
    // then synthesizes a noisy pseudorange for each one.
    void generateConstellation(int nSat, const GNSSState& truePos, double slantRangeM = 2.2e7)
    {
        _truePos = truePos;
        _sats.clear();
        _sats.reserve(nSat);

        std::uniform_real_distribution<double> azDist(0.0, 2.0 * kPi);
        std::uniform_real_distribution<double> elDist(_elevationMaskDeg * kPi / 180.0, kPi / 2.0);

        for (int i = 0; i < nSat; ++i)
        {
            double az = azDist(_rng);
            double el = elDist(_rng);

            Satellite s;
            s.azimuth = az;
            s.elevation = el;
            // local ENU-style offset from the receiver, then treated as absolute
            // ECEF-like coordinates (fine for a toy/course-project geometry -
            // real ECEF orbital placement can replace this later without
            // touching the solver code above).
            s.X = truePos.X + slantRangeM * std::cos(el) * std::sin(az);
            s.Y = truePos.Y + slantRangeM * std::cos(el) * std::cos(az);
            s.Z = truePos.Z + slantRangeM * std::sin(el);

            // Elevation-based physical noise: always the true model,
            // regardless of what the solver assumes for weighting.
            s.trueSigma = _sigma0 / std::sin(el);

            double trueRange = distance(truePos.X, truePos.Y, truePos.Z, s);
            std::normal_distribution<double> noise(0.0, s.trueSigma);
            s.pseudorange = trueRange + truePos.b + noise(_rng);

            _sats.push_back(s);
        }

        recomputeWeights();
    }

    int satelliteCount() const { return (int)_sats.size(); }
    const std::vector<Satellite>& satellites() const { return _sats; }
    const GNSSState& truePosition() const { return _truePos; }

    double sigma0() const { return _sigma0; }
    void setSigma0(double s) { _sigma0 = s; } // takes effect on the next generateConstellation() call

    WeightingStrategy weightingStrategy() const { return _weightStrategy; }
    void setWeightingStrategy(WeightingStrategy s)
    {
        _weightStrategy = s;
        recomputeWeights(); // re-weights the exact same noisy measurements - no regeneration
    }

    // Moves the true/target position without regenerating the satellite
    // geometry - satellites keep their absolute X/Y/Z, only the receiver
    // location (and therefore each measured pseudorange, redrawn with
    // fresh noise) changes. This is what dragging the target marker calls.
    // Simplification: satellite azimuth/elevation (used only for the sky
    // plot) are not recomputed - at real GNSS slant ranges (~20,000 km),
    // moving the receiver by the tens of km this UI allows shifts the
    // apparent satellite directions by a negligible amount.
    void setTruePosition(const GNSSState& newTruePos)
    {
        _truePos = newTruePos;
        for (auto& s : _sats)
        {
            double trueRange = distance(newTruePos.X, newTruePos.Y, newTruePos.Z, s);
            std::normal_distribution<double> noise(0.0, s.trueSigma);
            s.pseudorange = trueRange + newTruePos.b + noise(_rng);
        }
    }

    struct IterationRecord
    {
        GNSSState state;
        double cost = 0.0;
    };

    // Runs Gauss-Newton to convergence (or maxIter) and returns the full
    // state+cost history, including the starting guess as entry 0 - this
    // is what the GUI animates through.
    std::vector<IterationRecord> runGaussNewton(const GNSSState& x0, int maxIter = 20, double tol = 1e-4) const
    {
        std::vector<IterationRecord> history;
        GNSSState x = x0;
        history.push_back({ x, costAt(x) });

        for (int i = 0; i < maxIter; ++i)
        {
            double cost = 0.0;
            GNSSState delta = gaussNewtonStep(x, cost);
            x.X += delta.X; x.Y += delta.Y; x.Z += delta.Z; x.b += delta.b;

            double stepNorm = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
            history.push_back({ x, costAt(x) });
            if (stepNorm < tol)
                break;
        }
        return history;
    }

    // Same, for gradient descent - typically far more entries, but now
    // also stops early once the cost stops meaningfully improving
    // (relative change below a small threshold), since GD's step size
    // alone rarely gets as small as GN's within a reasonable iteration
    // budget - it's still improving, just too slowly to matter.
    std::vector<IterationRecord> runGradientDescent(const GNSSState& x0, int maxIter = 500, double tol = 1e-4) const
    {
        std::vector<IterationRecord> history;
        GNSSState x = x0;
        double prevCost = costAt(x);
        history.push_back({ x, prevCost });

        for (int i = 0; i < maxIter; ++i)
        {
            double cost = 0.0, alpha = 0.0;
            GNSSState delta = gradientDescentStep(x, cost, alpha);
            x.X += delta.X; x.Y += delta.Y; x.Z += delta.Z; x.b += delta.b;

            double stepNorm = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
            double newCost = costAt(x);
            history.push_back({ x, newCost });

            bool costPlateaued = std::fabs(prevCost - newCost) < 1e-6 * std::max(1.0, prevCost);
            if (stepNorm < tol || costPlateaued)
                break;
            prevCost = newCost;
        }
        return history;
    }

    // Same G/rhs build as above, PLUS the second-order curvature term
    // Gauss-Newton drops: H = J'WJ - sum(w*r*Hess(rho)). Hess(rho) (the
    // model function's own Hessian, not the residual's) has a standard
    // closed form for a distance function: (I - u*u')/dist, where u is
    // the unit direction to the satellite (the position part of J).
    // The clock-bias row/column of Hess(rho) is exactly zero since rho
    // is linear in b.
    void buildNewtonSystem(const GNSSState& x, double H[4][4], double rhs[4], double& outCost) const
    {
        for (int a = 0; a < 4; ++a)
        {
            rhs[a] = 0.0;
            for (int b = 0; b < 4; ++b)
                H[a][b] = 0.0;
        }
        outCost = 0.0;

        for (const auto& s : _sats)
        {
            double dx = x.X - s.X, dy = x.Y - s.Y, dz = x.Z - s.Z;
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 1.0) dist = 1.0;
            double predicted = dist + x.b;
            double r = s.pseudorange - predicted;
            double weight = s.weight;

            double J[4] = { dx / dist, dy / dist, dz / dist, 1.0 };

            for (int a = 0; a < 4; ++a)
            {
                rhs[a] += weight * J[a] * r;
                for (int b = 0; b < 4; ++b)
                    H[a][b] += weight * J[a] * J[b];
            }

            double wr = weight * r;
            for (int a = 0; a < 3; ++a)
            {
                for (int b = 0; b < 3; ++b)
                {
                    double deltaAB = (a == b) ? 1.0 : 0.0;
                    double hessAB = (deltaAB - J[a] * J[b]) / dist;
                    H[a][b] -= wr * hessAB;
                }
            }

            outCost += weight * r * r;
        }
        outCost *= 0.5;
    }

    // Full Newton step. The Hessian here isn't guaranteed positive-
    // definite (unlike Gauss-Newton's J'WJ), so if the solver can't
    // factorize it we fall back to a plain Gauss-Newton step - a
    // standard, well-known safeguard for this exact failure mode.
    GNSSState newtonStep(const GNSSState& x, double& outCost, bool& usedFallback) const
    {
        double H[4][4], rhs[4];
        buildNewtonSystem(x, H, rhs, outCost);

        sparse::DblSolverReleaser p_solver(
            sparse::createDblSolver(4, 10, sparse::Symmetry::SymmetricIndef, sparse::SolverType::LDLT)
        );
        sparse::DblSolver& solver = *(p_solver.ptr());

        for (int a = 0; a < 4; ++a)
            for (int b = a; b < 4; ++b)
                solver.addTriple(a, b, H[a][b]);
        for (int a = 0; a < 4; ++a)
            solver.setRHS(a, rhs[a]);

        if (solver.factorize())
        {
            solver.solve();
            usedFallback = false;
            GNSSState delta;
            delta.X = solver.x(0);
            delta.Y = solver.x(1);
            delta.Z = solver.x(2);
            delta.b = solver.x(3);
            return delta;
        }

        usedFallback = true;
        return gaussNewtonStep(x, outCost);
    }

    std::vector<IterationRecord> runNewton(const GNSSState& x0, int maxIter = 20, double tol = 1e-4) const
    {
        std::vector<IterationRecord> history;
        GNSSState x = x0;
        history.push_back({ x, costAt(x) });

        for (int i = 0; i < maxIter; ++i)
        {
            double cost = 0.0;
            bool fallback = false;
            GNSSState delta = newtonStep(x, cost, fallback);
            x.X += delta.X; x.Y += delta.Y; x.Z += delta.Z; x.b += delta.b;

            double stepNorm = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
            history.push_back({ x, costAt(x) });
            if (stepNorm < tol)
                break;
        }
        return history;
    }

    // PDOP/GDOP at a given state: invert the 4x4 normal-equations matrix
    // G (plain Gauss-Jordan - a 4x4 inverse doesn't need the sparse
    // solver) and read the position/full trace, standard DOP definitions.
    // Returns false if G is singular (e.g. too few satellites).
    bool computeDOP(const GNSSState& x, double& outPDOP, double& outGDOP) const
    {
        double G[4][4], rhs[4], cost;
        buildNormalEquations(x, G, rhs, cost);

        // augment with the identity, then Gauss-Jordan eliminate
        double A[4][8];
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
                A[i][j] = G[i][j];
            for (int j = 0; j < 4; ++j)
                A[i][4 + j] = (i == j) ? 1.0 : 0.0;
        }

        for (int col = 0; col < 4; ++col)
        {
            int pivotRow = col;
            for (int r = col + 1; r < 4; ++r)
                if (std::fabs(A[r][col]) > std::fabs(A[pivotRow][col]))
                    pivotRow = r;
            if (std::fabs(A[pivotRow][col]) < 1e-12)
                return false; // singular - not enough independent geometry

            if (pivotRow != col)
                for (int j = 0; j < 8; ++j)
                    std::swap(A[col][j], A[pivotRow][j]);

            double piv = A[col][col];
            for (int j = 0; j < 8; ++j)
                A[col][j] /= piv;

            for (int r = 0; r < 4; ++r)
            {
                if (r == col) continue;
                double factor = A[r][col];
                for (int j = 0; j < 8; ++j)
                    A[r][j] -= factor * A[col][j];
            }
        }

        double Qxx = A[0][4], Qyy = A[1][5], Qzz = A[2][6], Qbb = A[3][7];
        outPDOP = std::sqrt(std::max(0.0, Qxx + Qyy + Qzz));
        outGDOP = std::sqrt(std::max(0.0, Qxx + Qyy + Qzz + Qbb));
        return true;
    }
};
