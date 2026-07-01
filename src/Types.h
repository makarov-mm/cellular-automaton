#pragma once

// Qt-free parameters shared with the CUDA device code.

struct AutomatonParams {
    int gridN      = 48;   // cubic grid side (gridN^3 cells)
    int rulePreset = 0;    // index into the preset table (see Automaton.cpp)
    int substeps   = 1;    // simulation steps advanced per rendered frame
    bool wrap      = false;// wrap-around (toroidal) vs. dead boundary
};
