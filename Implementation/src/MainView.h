//
// MainView.h
//
#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/ComboBox.h>
#include <gui/Slider.h>
#include <gui/GridLayout.h>
#include <gui/GridComposer.h>
#include "ViewGNSS.h"

class MainView : public gui::View
{
protected:
    gui::Label _lblAlgorithm;
    gui::ComboBox _cmbAlgorithm;
    gui::Label _lblSatCount;
    gui::Slider _slSatCount;
    gui::Label _lblNoise;
    gui::Slider _slNoise;

    gui::Label _lblTargetX;
    gui::Slider _slTargetX;
    gui::Label _lblTargetY;
    gui::Slider _slTargetY;

    gui::Label _lblGuessX;
    gui::Slider _slGuessX;
    gui::Label _lblGuessY;
    gui::Slider _slGuessY;

    gui::Label _lblWeighting;
    gui::ComboBox _cmbWeighting;

    gui::Label _lblPlayback;
    gui::ComboBox _cmbPlayback;

    ViewGNSS _animation;
    gui::GridLayout _gl;

    void setupEventHandlers()
    {
        _cmbAlgorithm.onChangedSelection([this]()
        {
            _animation.setAlgorithm(_cmbAlgorithm.getSelectedIndex());
        });

        _slSatCount.onChangedValue([this]()
        {
            int nSat = (int)_slSatCount.getValue();
            _animation.setSatelliteCount(nSat);
        });

        _slNoise.onChangedValue([this]()
        {
            double sigma0 = _slNoise.getValue();
            _animation.setNoiseLevel(sigma0);
        });

        _slTargetX.onChangedValue([this]() { updateTarget(); });
        _slTargetY.onChangedValue([this]() { updateTarget(); });
        _slGuessX.onChangedValue([this]() { updateGuess(); });
        _slGuessY.onChangedValue([this]() { updateGuess(); });

        _cmbWeighting.onChangedSelection([this]()
        {
            _animation.setWeightingStrategy(_cmbWeighting.getSelectedIndex());
        });

        _cmbPlayback.onChangedSelection([this]()
        {
            int sel = _cmbPlayback.getSelectedIndex();
            if (sel == 0) _animation.play();
            else if (sel == 1) _animation.pause();
            else _animation.restartPlayback();
        });
    }

    void updateTarget()
    {
        _animation.setTargetOffsetKm(_slTargetX.getValue(), _slTargetY.getValue());
    }

    void updateGuess()
    {
        _animation.setInitialGuessOffsetKm(_slGuessX.getValue(), _slGuessY.getValue());
    }

public:
    MainView()
    : _lblAlgorithm("Algorithm")
    , _lblSatCount("Satellites")
    , _lblNoise("Noise (m)")
    , _lblTargetX("Target X (km)")
    , _lblTargetY("Target Y (km)")
    , _lblGuessX("Guess X (km)")
    , _lblGuessY("Guess Y (km)")
    , _lblWeighting("Weighting")
    , _lblPlayback("Playback")
    , _gl(5, 7)
    {
        _cmbAlgorithm.addItem("Gauss-Newton");
        _cmbAlgorithm.addItem("Gradient Descent");
        _cmbAlgorithm.addItem("Newton (full Hessian)");
        _cmbAlgorithm.selectIndex(0);

        _cmbWeighting.addItem("Uniform");
        _cmbWeighting.addItem("True Elevation");
        _cmbWeighting.addItem("Elevation^2");
        _cmbWeighting.selectIndex(1); // matches the physical noise model by default

        _cmbPlayback.addItem("Play");
        _cmbPlayback.addItem("Pause");
        _cmbPlayback.addItem("Restart");
        _cmbPlayback.selectIndex(0);

        _slSatCount.setRange(4, 16);
        _slSatCount.setValue(8);

        _slNoise.setRange(1, 15);
        _slNoise.setValue(3);

        _slTargetX.setRange(-100, 100);
        _slTargetX.setValue(0);
        _slTargetY.setRange(-100, 100);
        _slTargetY.setValue(0);

        _slGuessX.setRange(-100, 100);
        _slGuessX.setValue(50);
        _slGuessY.setRange(-100, 100);
        _slGuessY.setValue(-50);

        setupEventHandlers();

        gui::GridComposer gc(_gl);
        gc.startNewRowWithSpace(5, 0) << _lblAlgorithm << _cmbAlgorithm << _lblSatCount << _slSatCount << _lblNoise << _slNoise;
        gc.startNewRowWithSpace(5, 0) << _lblTargetX << _slTargetX << _lblTargetY << _slTargetY;
        gc.startNewRowWithSpace(5, 0) << _lblGuessX << _slGuessX << _lblGuessY << _slGuessY;
        gc.startNewRowWithSpace(5, 0) << _lblWeighting << _cmbWeighting << _lblPlayback << _cmbPlayback;
        gc.appendRow(_animation, -1);

        setLayout(&_gl);

        _animation.newScenario(8);
    }

    ViewGNSS& getViewGNSS() { return _animation; }
};
