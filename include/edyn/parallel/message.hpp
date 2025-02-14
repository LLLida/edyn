#ifndef EDYN_PARALLEL_MESSAGE_HPP
#define EDYN_PARALLEL_MESSAGE_HPP

#include "edyn/math/scalar.hpp"
#include "edyn/context/settings.hpp"

namespace edyn::msg {

struct set_paused {
    bool paused;
};

struct set_fixed_dt {
    scalar dt;
};

struct set_solver_iterations {
    unsigned velocity_iterations;
    unsigned position_iterations;
};

struct set_settings {
    edyn::settings settings;
};

struct step_simulation {};

struct wake_up_island {};

struct split_island {};

struct set_com {
    entt::entity entity;
    vector3 com;
};

}

#endif // EDYN_PARALLEL_MESSAGE_HPP
