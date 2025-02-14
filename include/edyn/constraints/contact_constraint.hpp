#ifndef EDYN_CONSTRAINTS_CONTACT_CONSTRAINT_HPP
#define EDYN_CONSTRAINTS_CONTACT_CONSTRAINT_HPP

#include <entt/entity/fwd.hpp>
#include "edyn/math/constants.hpp"
#include "edyn/constraints/constraint_base.hpp"
#include "edyn/constraints/prepare_constraints.hpp"

namespace edyn {

struct contact_constraint : public constraint_base {
    scalar stiffness {large_scalar};
    scalar damping {large_scalar};
};

namespace internal {
    struct contact_friction_row {
        std::array<vector3, 4> J;
        scalar eff_mass;
        scalar rhs;
        scalar impulse;
    };

    struct contact_friction_row_pair {
        contact_friction_row row[2];
        scalar friction_coefficient;
    };

    struct contact_constraint_context {
        std::vector<contact_friction_row_pair> friction_rows;
    };
}

template<>
void prepare_constraints<contact_constraint>(entt::registry &, row_cache &, scalar dt);

template<>
void iterate_constraints<contact_constraint>(entt::registry &, row_cache &, scalar dt);

template<>
bool solve_position_constraints<contact_constraint>(entt::registry &registry, scalar dt);

}

#endif // EDYN_CONSTRAINTS_CONTACT_CONSTRAINT_HPP
