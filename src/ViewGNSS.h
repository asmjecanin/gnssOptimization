//
// ViewGNSS.h
// Canvas-based visualization: sky plot (top-left) + convergence plot
// (top-right) + cost-vs-iteration chart (bottom strip).
//
#pragma once
#include <gui/Canvas.h>
#include <gui/Shape.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <td/Timer.h>
#include <td/MutableString.h>
#include "GNSSModel.h"

// Plain rectangle for panel geometry - deliberately not using gui::Rect
// here since we only need left/top/right/bottom doubles for our own math.
struct PanelBounds
{
    double left = 0, top = 0, right = 0, bottom = 0;
    double width() const { return right - left; }
    double height() const { return bottom - top; }
    double centerX() const { return (left + right) / 2.0; }
    double centerY() const { return (top + bottom) / 2.0; }
};

// 0 = Gauss-Newton, 1 = Gradient Descent, 2 = full Newton
enum class AlgoChoice { GaussNewton = 0, GradientDescent = 1, Newton = 2 };

class ViewGNSS : public gui::Canvas
{
protected:
    GNSSModel _model;
    GNSSState _initialGuess;

    std::vector<GNSSModel::IterationRecord> _historyGN;
    std::vector<GNSSModel::IterationRecord> _historyGD;
    std::vector<GNSSModel::IterationRecord> _historyNewton;
    AlgoChoice _activeAlgo = AlgoChoice::GaussNewton;

    size_t _currentStep = 0;
    bool _isPlaying = true;       // play/pause for the step animation
    td::Timer<false> _stepTimer;  // paces the step-by-step animation

    double _pdop = 0, _gdop = 0;
    bool _dopValid = false;

    td::MutableString _batchSummary;
    bool _showBatchSummary = false;

    gui::Size _size;

    // convergence plot shows +/- this many meters around the fixed reference point
    double _plotHalfRangeM = 120000.0;

    // Chart strip height scales with window height instead of being a
    // fixed pixel count, so it stays proportionate on both a small
    // laptop window and a maximized/fullscreen one.
    double chartHeight() const
    {
        double h = _size.height * 0.16;
        if (h < 70) h = 70;
        if (h > 130) h = 130;
        return h;
    }

    void rerun()
    {
        _historyGN = _model.runGaussNewton(_initialGuess);
        _historyGD = _model.runGradientDescent(_initialGuess);
        _historyNewton = _model.runNewton(_initialGuess);
        _currentStep = 0;
        _isPlaying = true;
        _stepTimer.start();

        // DOP reflects geometry+weighting at the (best available) solution -
        // evaluate it at Gauss-Newton's final state regardless of which
        // algorithm is currently displayed.
        _dopValid = _model.computeDOP(_historyGN.back().state, _pdop, _gdop);
    }

    const std::vector<GNSSModel::IterationRecord>& activeHistory() const
    {
        switch (_activeAlgo)
        {
            case AlgoChoice::GradientDescent: return _historyGD;
            case AlgoChoice::Newton: return _historyNewton;
            default: return _historyGN;
        }
    }

    const char* activeAlgoName() const
    {
        switch (_activeAlgo)
        {
            case AlgoChoice::GradientDescent: return "Gradient Descent";
            case AlgoChoice::Newton: return "Newton (full Hessian)";
            default: return "Gauss-Newton";
        }
    }

    PanelBounds skyPlotBounds() const
    {
        PanelBounds b;
        b.left = 0; b.top = 0;
        b.right = _size.width / 2.0;
        b.bottom = _size.height - chartHeight();
        return b;
    }

    PanelBounds convergenceBounds() const
    {
        PanelBounds b;
        b.left = _size.width / 2.0; b.top = 0;
        b.right = _size.width;
        b.bottom = _size.height - chartHeight();
        return b;
    }

    PanelBounds chartBounds() const
    {
        PanelBounds b;
        b.left = 0; b.right = _size.width;
        b.top = _size.height - chartHeight();
        b.bottom = _size.height;
        return b;
    }

    static gui::Rect toGuiRect(const PanelBounds& b)
    {
        return gui::Rect(gui::Point(b.left, b.top), gui::Size(b.width(), b.height()));
    }

    // azimuth/elevation -> screen point. Zenith (90 deg) at the panel
    // center, horizon (the elevation mask) at the panel edge.
    gui::Point skyPlotPoint(double azimuthRad, double elevationRad, const PanelBounds& b) const
    {
        double elevDeg = elevationRad * 180.0 / kPi;
        double radius = (90.0 - elevDeg) / 90.0 * (std::min(b.width(), b.height()) * 0.40);
        return gui::Point(b.centerX() + radius * std::sin(azimuthRad),
                           b.centerY() - radius * std::cos(azimuthRad));
    }

    // world X/Y (meters) -> screen point, centered on the FIXED reference
    // point (kBaseX/kBaseY), not the live true position - if this were
    // centered on the true position, the target marker would always land
    // exactly at panel-center by definition and could never appear to move.
    gui::Point convergencePoint(double worldX, double worldY, const PanelBounds& b) const
    {
        double scale = (std::min(b.width(), b.height()) * 0.40) / _plotHalfRangeM;
        double offsetX = worldX - kBaseX;
        double offsetY = worldY - kBaseY;
        return gui::Point(b.centerX() + offsetX * scale, b.centerY() - offsetY * scale);
    }

    // small filled+ring "icon" - used for every marker so they all read
    // as deliberate icons rather than plain dots. fillRadius <= 0 means
    // ring-only (used for the initial-guess marker, to distinguish it
    // from the solid, filled "current estimate" marker).
    void drawMarker(const gui::Point& p, double ringRadius, double fillRadius, td::ColorID color) const
    {
        gui::Circle ring(p, ringRadius);
        gui::Shape ringShape;
        ringShape.createCircle(ring);
        ringShape.drawWire(color, 1.5f);

        if (fillRadius > 0)
        {
            gui::Circle fill(p, fillRadius);
            gui::Shape fillShape;
            fillShape.createCircle(fill);
            fillShape.drawFill(color);
        }
    }

    // thin reference-ring outline (no fill) - used for sky-plot elevation
    // rings and convergence-plot distance rings.
    void drawRing(const gui::Point& center, double radius, td::ColorID color) const
    {
        gui::Circle c(center, radius);
        gui::Shape shape;
        shape.createCircle(c);
        shape.drawWire(color, 1.0f);
    }

    static double elevationRadius(double elevDeg, double maxRadius)
    {
        return (90.0 - elevDeg) / 90.0 * maxRadius;
    }

    static td::ColorID algoColor(AlgoChoice a)
    {
        switch (a)
        {
            case AlgoChoice::GradientDescent: return td::ColorID::Orange;
            case AlgoChoice::Newton: return td::ColorID::Green;
            default: return td::ColorID::Teal;
        }
    }

    // Cost-vs-iteration chart, all three algorithms overlaid. X axis is
    // iteration PROGRESS FRACTION (i / (N-1)) rather than raw iteration
    // count - GN/Newton converge in a handful of steps and GD in
    // hundreds, so a shared raw iteration axis would squash the fast
    // methods into an invisible sliver. Y axis is log10(cost).
    void drawCostChart(const PanelBounds& b) const
    {
        double minLog = 1e18, maxLog = -1e18;
        auto scan = [&](const std::vector<GNSSModel::IterationRecord>& h)
        {
            for (const auto& rec : h)
            {
                double l = std::log10(std::max(rec.cost, 1e-12));
                minLog = std::min(minLog, l);
                maxLog = std::max(maxLog, l);
            }
        };
        scan(_historyGN);
        scan(_historyGD);
        scan(_historyNewton);
        if (maxLog <= minLog) maxLog = minLog + 1.0;

        double padX = 12, padY = 22;
        double left = b.left + padX, right = b.right - padX;
        double top = b.top + padY, bottom = b.bottom - padY * 0.5;

        auto plot = [&](const std::vector<GNSSModel::IterationRecord>& h, td::ColorID color, bool isActive)
        {
            if (h.size() < 2) return;
            size_t n = h.size();
            gui::Point prev;
            for (size_t i = 0; i < n; ++i)
            {
                double frac = (double)i / (double)(n - 1);
                double l = std::log10(std::max(h[i].cost, 1e-12));
                double yFrac = (l - minLog) / (maxLog - minLog);
                gui::Point p(left + frac * (right - left), bottom - (1.0 - yFrac) * (bottom - top));
                if (i > 0)
                {
                    gui::Point pts[] = { prev, p };
                    gui::Shape line;
                    line.createLines(pts, 2);
                    line.drawWire(color, isActive ? 2.0f : 1.0f);
                }
                prev = p;
            }
            if (isActive && _currentStep < n)
            {
                double frac = (double)_currentStep / (double)(n - 1);
                double l = std::log10(std::max(h[_currentStep].cost, 1e-12));
                double yFrac = (l - minLog) / (maxLog - minLog);
                gui::Point p(left + frac * (right - left), bottom - (1.0 - yFrac) * (bottom - top));
                drawMarker(p, 4, 3, color);
            }
        };

        plot(_historyGN, algoColor(AlgoChoice::GaussNewton), _activeAlgo == AlgoChoice::GaussNewton);
        plot(_historyGD, algoColor(AlgoChoice::GradientDescent), _activeAlgo == AlgoChoice::GradientDescent);
        plot(_historyNewton, algoColor(AlgoChoice::Newton), _activeAlgo == AlgoChoice::Newton);

        gui::DrawableString::draw("cost vs. iteration (log) - teal=GN, orange=GD, green=Newton",
                                   gui::Point(b.left + 10, b.top + 4), gui::Font::ID::SystemNormal, td::ColorID::SysText);

        if (_showBatchSummary)
        {
            gui::DrawableString::draw(_batchSummary.getString(), gui::Point(b.right - 460, b.top + 4),
                                       gui::Font::ID::SystemNormal, td::ColorID::SysText);
        }
    }

    void onResize(const gui::Size& newSize) override
    {
        _size = newSize;
    }

    void onDraw(const gui::Rect& rect) override
    {
        // advance the animated step a few times a second while playing
        if (_isPlaying && _stepTimer.getDurationInSeconds() > 0.3)
        {
            _stepTimer.start();
            if (_currentStep + 1 < activeHistory().size())
                ++_currentStep;
        }

        PanelBounds skyB = skyPlotBounds();
        PanelBounds convB = convergenceBounds();
        PanelBounds chartB = chartBounds();

        gui::Shape::drawRect(toGuiRect(skyB), td::ColorID::White, 1.0f);
        gui::Shape::drawRect(toGuiRect(convB), td::ColorID::White, 1.0f);
        gui::Shape::drawRect(toGuiRect(chartB), td::ColorID::White, 1.0f);

        // --- sky plot: elevation reference rings + compass labels first, so markers draw on top ---
        double skyMaxR = std::min(skyB.width(), skyB.height()) * 0.40;
        gui::Point skyCenter(skyB.centerX(), skyB.centerY());
        drawRing(skyCenter, elevationRadius(15, skyMaxR), td::ColorID::SysText);
        drawRing(skyCenter, elevationRadius(30, skyMaxR), td::ColorID::SysText);
        drawRing(skyCenter, elevationRadius(60, skyMaxR), td::ColorID::SysText);
        gui::DrawableString::draw("N", gui::Point(skyCenter.x - 4, skyB.top + 20), gui::Font::ID::SystemNormal, td::ColorID::SysText);
        gui::DrawableString::draw("S", gui::Point(skyCenter.x - 4, skyB.bottom - 18), gui::Font::ID::SystemNormal, td::ColorID::SysText);
        gui::DrawableString::draw("E", gui::Point(skyB.right - 22, skyCenter.y - 8), gui::Font::ID::SystemNormal, td::ColorID::SysText);
        gui::DrawableString::draw("W", gui::Point(skyB.left + 8, skyCenter.y - 8), gui::Font::ID::SystemNormal, td::ColorID::SysText);
        gui::DrawableString::draw("SKY PLOT", gui::Point(skyB.left + 10, skyB.top + 4),
                                   gui::Font::ID::SystemLargerBold, td::ColorID::SysText);

        for (const auto& s : _model.satellites())
        {
            gui::Point p = skyPlotPoint(s.azimuth, s.elevation, skyB);
            drawMarker(p, 7, 3, td::ColorID::Orange);
        }

        // --- convergence plot: distance reference rings + title first ---
        double convMaxR = std::min(convB.width(), convB.height()) * 0.40;
        gui::Point convCenter(convB.centerX(), convB.centerY());
        double ringStepM = 40000.0; // one ring every 40 km
        for (double r = ringStepM; r * (convMaxR / _plotHalfRangeM) < convMaxR; r += ringStepM)
        {
            double screenR = r * (convMaxR / _plotHalfRangeM);
            drawRing(convCenter, screenR, td::ColorID::SysText);
        }
        gui::DrawableString::draw("CONVERGENCE", gui::Point(convB.left + 10, convB.top + 4),
                                   gui::Font::ID::SystemLargerBold, td::ColorID::SysText);

        gui::Point truePt = convergencePoint(_model.truePosition().X, _model.truePosition().Y, convB);
        drawMarker(truePt, 12, 6, td::ColorID::Teal);

        gui::Point guessPt = convergencePoint(_initialGuess.X, _initialGuess.Y, convB);
        drawMarker(guessPt, 8, 0, td::ColorID::Red);

        // estimate trail so far, in the active algorithm's color
        const auto& hist = activeHistory();
        td::ColorID trailColor = algoColor(_activeAlgo);
        for (size_t i = 0; i + 1 <= _currentStep && i + 1 < hist.size(); ++i)
        {
            gui::Point p1 = convergencePoint(hist[i].state.X, hist[i].state.Y, convB);
            gui::Point p2 = convergencePoint(hist[i + 1].state.X, hist[i + 1].state.Y, convB);
            gui::Point pts[] = { p1, p2 };
            gui::Shape line;
            line.createLines(pts, 2);
            line.drawWire(trailColor, 1.5f);
        }

        if (!hist.empty())
        {
            gui::Point estPt = convergencePoint(hist[_currentStep].state.X, hist[_currentStep].state.Y, convB);
            drawMarker(estPt, 6, 5, trailColor);

            double ex = hist[_currentStep].state.X - _model.truePosition().X;
            double ey = hist[_currentStep].state.Y - _model.truePosition().Y;
            double ez = hist[_currentStep].state.Z - _model.truePosition().Z;
            double posError = std::sqrt(ex * ex + ey * ey + ez * ez);

            double textX = convB.left + 10;
            double lineY = convB.top + 26;

            td::MutableString info;
            info.reserve(160);
            info.appendFormat("%s   iter %d/%d   cost=%.3f   %s",
                               activeAlgoName(), (int)_currentStep, (int)hist.size() - 1,
                               hist[_currentStep].cost, _isPlaying ? "" : "[paused]");
            gui::DrawableString::draw(info.getString(), gui::Point(textX, lineY), gui::Font::ID::SystemNormal, td::ColorID::SysText);
            lineY += 20;

            td::MutableString info2;
            info2.reserve(160);
            const char* stratName = "Uniform";
            if (_model.weightingStrategy() == WeightingStrategy::TrueElevation) stratName = "TrueElevation";
            else if (_model.weightingStrategy() == WeightingStrategy::ElevationSquared) stratName = "Elevation^2";
            info2.appendFormat("sats=%d   sigma0=%.1fm   w=%s   |err|=%.1fm",
                                _model.satelliteCount(), _model.sigma0(), stratName, posError);
            gui::DrawableString::draw(info2.getString(), gui::Point(textX, lineY), gui::Font::ID::SystemNormal, td::ColorID::SysText);
            lineY += 20;

            td::MutableString info3;
            info3.reserve(96);
            if (_dopValid)
                info3.appendFormat("PDOP=%.2f   GDOP=%.2f", _pdop, _gdop);
            else
                info3.appendFormat("DOP: singular geometry (too few satellites)");
            gui::DrawableString::draw(info3.getString(), gui::Point(textX, lineY), gui::Font::ID::SystemNormal, td::ColorID::SysText);
        }

        drawCostChart(chartB);
    }

public:
    // Fixed reference point that target/guess offset sliders measure from.
    static constexpr double kBaseX = 4.0e6, kBaseY = 3.0e6, kBaseZ = 3.5e6, kBaseB = 5.0;

    ViewGNSS()
    : Canvas() //enable keyboard events
    {
        setPreferredFrameRateRange(30, 30);
        enableResizeEvent(true);
    }

    void newScenario(int nSat = 8)
    {
        GNSSState truePos;
        truePos.X = kBaseX; truePos.Y = kBaseY; truePos.Z = kBaseZ; truePos.b = kBaseB;
        _model.generateConstellation(nSat, truePos);

        _initialGuess.X = truePos.X + 50000.0;
        _initialGuess.Y = truePos.Y - 50000.0;
        _initialGuess.Z = truePos.Z + 50000.0;
        _initialGuess.b = 0.0;

        _showBatchSummary = false;
        rerun();
        startAnimation();
    }

    void setTargetOffsetKm(double offsetXKm, double offsetYKm)
    {
        GNSSState newTrue;
        newTrue.X = kBaseX + offsetXKm * 1000.0;
        newTrue.Y = kBaseY + offsetYKm * 1000.0;
        newTrue.Z = kBaseZ;
        newTrue.b = kBaseB;
        _model.setTruePosition(newTrue);
        rerun();
    }

    void setInitialGuessOffsetKm(double offsetXKm, double offsetYKm)
    {
        _initialGuess.X = kBaseX + offsetXKm * 1000.0;
        _initialGuess.Y = kBaseY + offsetYKm * 1000.0;
        _initialGuess.Z = kBaseZ + 50000.0;
        _initialGuess.b = 0.0;
        rerun();
    }

    void setSatelliteCount(int nSat)
    {
        GNSSState truePos = _model.truePosition();
        _model.generateConstellation(nSat, truePos);
        rerun();
    }

    void setNoiseLevel(double sigma0)
    {
        _model.setSigma0(sigma0);
        GNSSState truePos = _model.truePosition();
        int nSat = _model.satelliteCount() > 0 ? _model.satelliteCount() : 8;
        _model.generateConstellation(nSat, truePos);
        rerun();
    }

    void setWeightingStrategy(int index)
    {
        WeightingStrategy s = WeightingStrategy::TrueElevation;
        if (index == 0) s = WeightingStrategy::Uniform;
        else if (index == 2) s = WeightingStrategy::ElevationSquared;
        _model.setWeightingStrategy(s);
        rerun();
    }

    // index: 0 = Gauss-Newton, 1 = Gradient Descent, 2 = Newton
    void setAlgorithm(int index)
    {
        _activeAlgo = AlgoChoice(index);
        _currentStep = 0;
        _isPlaying = true;
        _stepTimer.start();
    }

    // Playback controls - do NOT re-solve anything, just control whether
    // and where the already-computed history is being stepped through.
    void play()
    {
        _isPlaying = true;
        _stepTimer.start();
    }

    void pause()
    {
        _isPlaying = false;
    }

    void restartPlayback()
    {
        _currentStep = 0;
        _isPlaying = true;
        _stepTimer.start();
    }

    void reset()
    {
        newScenario(_model.satelliteCount() > 0 ? _model.satelliteCount() : 8);
    }

    // Runs nTrials independent noisy realizations of the SAME scenario
    // and reports mean +/- std of final position error for all three
    // algorithms.
    void runBatchExperiment(int nTrials)
    {
        int nSat = _model.satelliteCount() > 0 ? _model.satelliteCount() : 8;
        GNSSState truePos = _model.truePosition();

        double sumGN = 0, sumGN2 = 0, sumGD = 0, sumGD2 = 0, sumNe = 0, sumNe2 = 0;

        for (int t = 0; t < nTrials; ++t)
        {
            _model.generateConstellation(nSat, truePos);

            auto histGN = _model.runGaussNewton(_initialGuess);
            auto histGD = _model.runGradientDescent(_initialGuess);
            auto histNe = _model.runNewton(_initialGuess);

            auto errOf = [&](const GNSSState& f)
            {
                double e1 = f.X - truePos.X, e2 = f.Y - truePos.Y, e3 = f.Z - truePos.Z;
                return std::sqrt(e1 * e1 + e2 * e2 + e3 * e3);
            };

            double errGN = errOf(histGN.back().state);
            double errGD = errOf(histGD.back().state);
            double errNe = errOf(histNe.back().state);

            sumGN += errGN; sumGN2 += errGN * errGN;
            sumGD += errGD; sumGD2 += errGD * errGD;
            sumNe += errNe; sumNe2 += errNe * errNe;
        }

        double meanGN = sumGN / nTrials, meanGD = sumGD / nTrials, meanNe = sumNe / nTrials;
        double stdGN = std::sqrt(std::max(0.0, sumGN2 / nTrials - meanGN * meanGN));
        double stdGD = std::sqrt(std::max(0.0, sumGD2 / nTrials - meanGD * meanGD));
        double stdNe = std::sqrt(std::max(0.0, sumNe2 / nTrials - meanNe * meanNe));

        _batchSummary.reset();
        _batchSummary.appendFormat("Batch(%d): GN %.1f+/-%.1f  GD %.1f+/-%.1f  Newton %.1f+/-%.1f",
                                    nTrials, meanGN, stdGN, meanGD, stdGD, meanNe, stdNe);
        _showBatchSummary = true;

        _model.generateConstellation(nSat, truePos);
        rerun();
    }
};
