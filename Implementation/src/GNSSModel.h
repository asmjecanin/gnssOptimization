#pragma once
#include <sparse/ISolver.h>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

constexpr double kPi = 3.14159265358979323846;


struct Satellite
{
    double X = 0, Y = 0, Z = 0;
    double trueSigma = 1.0;  // physical measurement 
    double weight = 1.0;     // assumed weight the solver uses - depends on WeightingStrategy
    double pseudorange = 0;  // noisy measured pseudorange to the true receiver
    double azimuth = 0;      // radians, for sky-plot drawing
    double elevation = 0;    // radians, for sky-plot drawing
};

// TrueElevation matches the physical noise model exactly. Uniform and
// ElevationSquared are deliberately mismatched, to demonstrate what
// happens when the assumed weighting doesn't match reality
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
    std::mt19937 _rng{ 42 };    

    static double distance(double x, double y, double z, const Satellite& s)
    {
        double dx = x - s.X, dy = y - s.Y, dz = z - s.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    
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
            if (dist < 1.0) dist = 1.0; 
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
    
    GNSSState gaussNewtonStep(const GNSSState& x, double& outCost) const
    {
        double G[4][4], rhs[4];
        buildNormalEquations(x, G, rhs, outCost);

        sparse::DblSolverReleaser p_solver(
            sparse::createDblSolver(4, 10, sparse::Symmetry::SymmetricPosDef, sparse::SolverType::LDLT)
        );
        sparse::DblSolver& solver = *(p_solver.ptr());

        for (int a = 0; a < 4; ++a)
            for (int b = a; b < 4; ++b)
                solver.addTriple(a, b, G[a][b]);

        for (int a = 0; a < 4; ++a)
            solver.setRHS(a, rhs[a]);

        if (!solver.factorize())
            return GNSSState{}; 

        solver.solve();

        GNSSState delta;
        delta.X = solver.x(0);
        delta.Y = solver.x(1);
        delta.Z = solver.x(2);
        delta.b = solver.x(3);
        return delta;
    }

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
    void setSigma0(double s) { _sigma0 = s; } 

    WeightingStrategy weightingStrategy() const { return _weightStrategy; }
    void setWeightingStrategy(WeightingStrategy s)
    {
        _weightStrategy = s;
        recomputeWeights(); 
    }

    
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

    
    bool computeDOP(const GNSSState& x, double& outPDOP, double& outGDOP) const
    {
        double G[4][4], rhs[4], cost;
        buildNormalEquations(x, G, rhs, cost);

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
                return false; 

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
