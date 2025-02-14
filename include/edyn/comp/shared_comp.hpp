#ifndef EDYN_SHARED_COMP_HPP
#define EDYN_SHARED_COMP_HPP

#include "edyn/comp/aabb.hpp"
#include "edyn/comp/gravity.hpp"
#include "edyn/comp/linvel.hpp"
#include "edyn/comp/angvel.hpp"
#include "edyn/comp/mass.hpp"
#include "edyn/comp/inertia.hpp"
#include "edyn/comp/position.hpp"
#include "edyn/comp/orientation.hpp"
#include "edyn/comp/center_of_mass.hpp"
#include "edyn/constraints/constraint.hpp"
#include "edyn/constraints/constraint_impulse.hpp"
#include "edyn/comp/tag.hpp"
#include "edyn/comp/shape_index.hpp"
#include "edyn/comp/material.hpp"
#include "edyn/comp/island.hpp"
#include "edyn/comp/collision_filter.hpp"
#include "edyn/comp/collision_exclusion.hpp"
#include "edyn/comp/continuous.hpp"
#include "edyn/shapes/shapes.hpp"
#include "edyn/collision/tree_view.hpp"
#include "edyn/collision/contact_manifold.hpp"
#include "edyn/collision/contact_point.hpp"

namespace edyn {

/**
 * Tuple of components that are exchanged between island coordinator and
 * island workers.
 */
static const auto shared_components = std::tuple_cat(std::tuple<
    island_timestamp,
    AABB,
    collision_filter,
    collision_exclusion,
    constraint_impulse,
    inertia,
    inertia_inv,
    inertia_world_inv,
    gravity,
    angvel,
    linvel,
    mass,
    mass_inv,
    material,
    position,
    orientation,
    contact_manifold,
    contact_point,
    continuous,
    center_of_mass,
    dynamic_tag,
    kinematic_tag,
    static_tag,
    procedural_tag,
    sleeping_tag,
    sleeping_disabled_tag,
    disabled_tag,
    continuous_contacts_tag,
    external_tag,
    shape_index,
    rigidbody_tag,
    tree_view
>{}, constraints_tuple, shapes_tuple); // Concatenate with all shapes and constraints at the end.

using shared_components_t = std::decay_t<decltype(shared_components)>;

}

#endif // EDYN_SHARED_COMP_HPP
