#include "edyn/collision/collide.hpp"
#include "edyn/config/constants.hpp"
#include "edyn/math/geom.hpp"
#include "edyn/math/math.hpp"
#include "edyn/math/matrix3x3.hpp"
#include "edyn/math/quaternion.hpp"
#include "edyn/math/scalar.hpp"
#include "edyn/math/vector2.hpp"
#include "edyn/math/vector2_3_util.hpp"
#include "edyn/math/vector3.hpp"
#include "edyn/shapes/box_shape.hpp"
#include "edyn/shapes/cylinder_shape.hpp"

namespace edyn {

void collide(const cylinder_shape &shA, const box_shape &shB,
             const collision_context &ctx, collision_result &result) {
    const auto &posA = ctx.posA;
    const auto &ornA = ctx.ornA;
    const auto &posB = ctx.posB;
    const auto &ornB = ctx.ornB;

    const auto box_axes = std::array<vector3, 3>{
        quaternion_x(ornB),
        quaternion_y(ornB),
        quaternion_z(ornB)
    };

    const auto cyl_axis = quaternion_x(ornA);
    const auto cyl_vertices = std::array<vector3, 2>{
        posA + cyl_axis * shA.half_length,
        posA - cyl_axis * shA.half_length
    };

    vector3 sep_axis;
    scalar distance = -EDYN_SCALAR_MAX;

    // Box faces.
    for (size_t i = 0; i < 3; ++i) {
        auto dir = box_axes[i];

        if (dot(posA - posB, dir) < 0) {
            dir *= -1; // Make dir point towards A.
        }

        auto projA = -shA.support_projection(posA, ornA, -dir);
        auto projB = dot(posB, dir) + shB.half_extents[i];
        auto dist = projA - projB;

        if (dist > distance) {
            distance = dist;
            sep_axis = dir;
        }
    }

    // Cylinder cap faces.
    {
        vector3 dir = cyl_axis;

        if (dot(posA - posB, dir) < 0) {
            dir *= -1; // Make dir point towards A.
        }

        auto projA = -(dot(posA, -dir) + shA.half_length);
        auto projB = shB.support_projection(posB, ornB, dir);
        auto dist = projA - projB;

        if (dist > distance) {
            distance = dist;
            sep_axis = dir;
        }
    }

    // Box edges vs cylinder side edges.
    for (size_t i = 0; i < 3; ++i) {
        auto dir = cross(box_axes[i], cyl_axis);

        if (!try_normalize(dir)) {
            continue;
        }

        if (dot(posA - posB, dir) < 0) {
            dir *= -1; // Make it point towards A.
        }

        auto projA = -shA.support_projection(posA, ornA, -dir);
        auto projB = shB.support_projection(posB, ornB, dir);
        auto dist = projA - projB;

        if (dist > distance) {
            distance = dist;
            sep_axis = dir;
        }
    }

    // Box vertices vs cylinder side edges.
    for (size_t i = 0; i < get_box_num_features(box_feature::vertex); ++i) {
        auto vertex = shB.get_vertex(i, posB, ornB);
        vector3 closest; scalar t;
        closest_point_line(posA, cyl_axis, vertex, t, closest);
        auto dir = closest - vertex;

        if (!try_normalize(dir)) {
            continue;
        }

        if (dot(posA - posB, dir) < 0) {
            dir *= -1; // Make it point towards A.
        }

        auto projA = -(dot(posA, -dir) + shA.radius);
        auto projB = shB.support_projection(posB, ornB, dir);
        auto dist = projA - projB;

        if (dist > distance) {
            distance = dist;
            sep_axis = dir;
        }
    }

    // Cylinder cap edges.
    for (size_t i = 0; i < 2; ++i) {
        auto circle_position = cyl_vertices[i];

        for (size_t j = 0; j < get_box_num_features(box_feature::edge); ++j) {
            auto edge_vertices = shB.get_edge(j, posB, ornB);

            // Find closest point between circle and triangle edge segment.
            size_t num_points;
            scalar s[2];
            vector3 closest_circle[2];
            vector3 closest_line[2];
            vector3 dir;
            closest_point_circle_line(circle_position, ornA, shA.radius,
                                      edge_vertices[0], edge_vertices[1], num_points,
                                      s[0], closest_circle[0], closest_line[0],
                                      s[1], closest_circle[1], closest_line[1],
                                      dir, support_feature_tolerance);

            // If there are two closest points, it means the segment is parallel
            // to the plane of the circle, which means the separating axis would
            // be a cylinder cap face normal which was already handled.
            if (num_points == 2) {
                continue;
            }

            if (dot(posA - posB, dir) < 0) {
                dir *= -1; // Make it point towards A.
            }

            auto projA = -shA.support_projection(posA, ornA, -dir);
            auto projB = shB.support_projection(posB, ornB, dir);
            auto dist = projA - projB;

            if (dist > distance) {
                distance = dist;
                sep_axis = dir;
            }
        }
    }

    if (distance > ctx.threshold) {
        return;
    }

    cylinder_feature featureA;
    size_t feature_indexA;
    shA.support_feature(posA, ornA, -sep_axis, featureA, feature_indexA,
                        support_feature_tolerance);

    box_feature featureB;
    size_t feature_indexB;
    shB.support_feature(posB, ornB, sep_axis, featureB, feature_indexB,
                        support_feature_tolerance);


    if (featureA == cylinder_feature::face && featureB == box_feature::face) {
        auto sign_faceA = to_sign(feature_indexA == 0);
        auto verticesB_local = shB.get_face(feature_indexB);
        std::array<vector3, 4> verticesB_world;

        for (size_t i = 0; i < 4; ++i) {
            verticesB_world[i] = to_world_space(verticesB_local[i], posB, ornB);
        }

        size_t num_edge_intersections = 0;
        std::pair<vector3, vector3> last_edge;

        // Check if circle and face edges intersect.
        for (size_t vertex_idx = 0; vertex_idx < 4; ++vertex_idx) {
            auto next_vertex_idx = (vertex_idx + 1) % 4;
            // Transform vertices to `shA` (cylinder) space. The cylinder axis
            // is the x-axis.
            auto v0w = verticesB_world[vertex_idx];
            auto v1w = verticesB_world[next_vertex_idx];

            auto v0A = to_object_space(v0w, posA, ornA);
            auto v1A = to_object_space(v1w, posA, ornA);

            scalar s[2];
            auto v0A_zy = to_vector2_zy(v0A);
            auto v1A_zy = to_vector2_zy(v1A);
            auto num_points = intersect_line_circle(v0A_zy, v1A_zy,
                                                    shA.radius, s[0], s[1]);

            if (num_points == 0) {
                continue;
            }

            // Single point outside the segment range.
            if (num_points == 1 && (s[0] < 0 || s[0] > 1)) {
                continue;
            }

            // If both s'es are either below zero or above one, it means the
            // line intersects the circle, but the segment doesn't.
            if (num_points == 2 && ((s[0] < 0 && s[1] < 0) || (s[0] > 1 && s[1] > 1))) {
                continue;
            }

            ++num_edge_intersections;
            last_edge = std::make_pair(v0w, v1w);

            auto v0B = verticesB_local[vertex_idx];
            auto v1B = verticesB_local[next_vertex_idx];
            auto pivotA_x = shA.half_length * sign_faceA;

            for (size_t pt_idx = 0; pt_idx < num_points; ++pt_idx) {
                auto t = s[pt_idx];
                // Avoid adding the same point twice.
                if (!(t < 1)) continue;

                auto u = clamp_unit(t);
                auto pivotA = lerp(v0A, v1A, u);
                auto local_distance = (pivotA.x - pivotA_x) * sign_faceA;
                pivotA.x = pivotA_x;
                auto pivotB = lerp(v0B, v1B, u);
                result.maybe_add_point({pivotA, pivotB, sep_axis, local_distance, contact_normal_attachment::normal_on_B});
            }
        }

        // If there are no edge intersections, it means the circle could be
        // contained in the box face.
        const auto posA_in_B = to_object_space(posA, posB, ornB);
        const auto ornA_in_B = conjugate(ornB) * ornA;
        auto face_normal_local = shB.get_face_normal(feature_indexB);

        if (num_edge_intersections == 0) {
            // Check if cylinder face center is in quad.
            if (point_in_polygonal_prism(verticesB_local, face_normal_local, posA_in_B)) {
                auto multipliers = std::array<scalar, 4>{0, 1, 0, -1};
                for(int i = 0; i < 4; ++i) {
                    auto j = (i + 1) % 4;
                    auto pivotA_x = shA.half_length * sign_faceA;
                    auto pivotA = vector3{pivotA_x,
                                        shA.radius * multipliers[i],
                                        shA.radius * multipliers[j]};
                    auto pivotA_in_B = to_world_space(pivotA, posA_in_B, ornA_in_B);
                    auto local_distance = dot(pivotA_in_B - verticesB_local[0], face_normal_local);
                    auto pivotB = project_plane(pivotA_in_B, verticesB_local[0], face_normal_local);
                    result.maybe_add_point({pivotA, pivotB, sep_axis, local_distance, contact_normal_attachment::normal_on_B});
                }
            }
        } else if (num_edge_intersections == 1) {
            // If it intersects a single edge, only two contact points have
            // been added, thus add extra points to create a stable base.
            auto edge_in_A = std::make_pair(
                to_vector2_zy(to_object_space(last_edge.first, posA, ornA)),
                to_vector2_zy(to_object_space(last_edge.second, posA, ornA))
            );

            auto edge_dir = edge_in_A.second - edge_in_A.first;
            auto tangent = normalize(orthogonal(edge_dir));

            // Make tangent point towards box face.
            auto box_face_center = to_vector2_zy(to_object_space(posB, posA, ornA));
            if (dot(tangent, box_face_center) < 0) {
                tangent *= -1;
            }

            auto pivotA_x = shA.half_length * to_sign(feature_indexA == 0);
            auto pivotA = vector3{pivotA_x, tangent.y * shA.radius, tangent.x * shA.radius};
            // Transform pivotA into box space and project onto box face.
            auto pivotA_in_B = to_world_space(pivotA, posA_in_B, ornA_in_B);
            auto pivotB = project_plane(pivotA_in_B, verticesB_local[0], face_normal_local);
            auto local_distance = dot(pivotA_in_B - verticesB_local[0], face_normal_local);
            result.maybe_add_point({pivotA, pivotB, sep_axis, local_distance, contact_normal_attachment::normal_on_B});
        }
    } else if (featureA == cylinder_feature::face && featureB == box_feature::edge) {
        auto verticesB_local = shB.get_edge(feature_indexB);
        std::array<vector3, 2> verticesB_world;

        for (size_t i = 0; i < 2; ++i) {
            verticesB_world[i] = to_world_space(verticesB_local[i], posB, ornB);
        }

        // Check if circle and edge intersect.
        // Transform vertices to `shA` (cylinder) space. The cylinder axis
        // is the x-axis.
        auto v0A = to_object_space(verticesB_world[0], posA, ornA);
        auto v1A = to_object_space(verticesB_world[1], posA, ornA);

        scalar s[2];
        auto v0A_zy = to_vector2_zy(v0A);
        auto v1A_zy = to_vector2_zy(v1A);
        auto num_points = intersect_line_circle(v0A_zy, v1A_zy,
                                                shA.radius, s[0], s[1]);

        EDYN_ASSERT(num_points > 0);

        auto sign_faceA = to_sign(feature_indexA == 0);
        auto pivotA_x = shA.half_length * sign_faceA;

        for (size_t pt_idx = 0; pt_idx < num_points; ++pt_idx) {
            auto t = clamp_unit(s[pt_idx]);
            auto pivotA = lerp(v0A, v1A, t);
            auto local_distance = (pivotA.x - pivotA_x) * sign_faceA;
            pivotA.x = pivotA_x;
            auto pivotB = lerp(verticesB_local[0], verticesB_local[1], t);
            result.maybe_add_point({pivotA, pivotB, sep_axis, local_distance, contact_normal_attachment::normal_on_A});
        }
    } else if (featureA == cylinder_feature::face && featureB == box_feature::vertex) {
        auto sign_faceA = to_sign(feature_indexA == 0);
        auto vertexB = shB.get_vertex(feature_indexB);
        auto vertexB_world = to_world_space(vertexB, posB, ornB);
        auto vertexA_x = shA.half_length * sign_faceA;
        auto vertexA = to_object_space(vertexB_world, posA, ornA);
        auto local_distance = (vertexA.x - vertexA_x) * sign_faceA;
        vertexA.x = vertexA_x; // Project onto face by setting the x value directly.
        result.maybe_add_point({vertexA, vertexB, sep_axis, local_distance, contact_normal_attachment::normal_on_A});
    } else if (featureA == cylinder_feature::side_edge && featureB == box_feature::face) {
        auto face_normal = shB.get_face_normal(feature_indexB, ornB);
        auto face_vertices = shB.get_face(feature_indexB, posB, ornB);

        std::array<vector3, 2> edge_vertices;
        edge_vertices[0] = cyl_vertices[0] - sep_axis * shA.radius;
        edge_vertices[1] = cyl_vertices[1] - sep_axis * shA.radius;

        // Perform edge intersection tests.
        auto face_center = shB.get_face_center(feature_indexB, posB, ornB);
        auto face_basis = shB.get_face_basis(feature_indexB, ornB);
        auto half_extents = shB.get_face_half_extents(feature_indexB);

        auto e0 = to_object_space(edge_vertices[0], face_center, face_basis);
        auto e1 = to_object_space(edge_vertices[1], face_center, face_basis);
        auto p0 = to_vector2_xz(e0);
        auto p1 = to_vector2_xz(e1);

        scalar s[2];
        auto num_points = intersect_line_aabb(p0, p1, -half_extents, half_extents, s[0], s[1]);

        for (size_t i = 0; i < num_points; ++i) {
            auto t = clamp_unit(s[i]); // Keep points within segment.
            auto edge_pivot = lerp(edge_vertices[0], edge_vertices[1], t);
            auto local_distance = dot(edge_pivot - face_vertices[0], face_normal);
            auto pivot_on_face = edge_pivot - face_normal * local_distance;
            auto pivotA = to_object_space(edge_pivot, posA, ornA);
            auto pivotB = to_object_space(pivot_on_face, posB, ornB);
            result.add_point({pivotA, pivotB, sep_axis, local_distance, contact_normal_attachment::normal_on_B});
        }
    } else if (featureA == cylinder_feature::side_edge && featureB == box_feature::edge) {
        auto box_edge = shB.get_edge(feature_indexB, posB, ornB);
        scalar s[2], t[2];
        vector3 closestA[2], closestB[2];
        size_t num_points = 0;
        closest_point_segment_segment(cyl_vertices[0], cyl_vertices[1],
                                        box_edge[0], box_edge[1],
                                        s[0], t[0], closestA[0], closestB[0], &num_points,
                                        &s[1], &t[1], &closestA[1], &closestB[1]);

        for (size_t i = 0; i < num_points; ++i) {
            auto pivotA_world = closestA[i] - sep_axis * shA.radius;
            auto pivotB_world = closestB[i];
            auto pivotA = to_object_space(pivotA_world, posA, ornA);
            auto pivotB = to_object_space(pivotB_world, posB, ornB);
            result.add_point({pivotA, pivotB, sep_axis, distance, contact_normal_attachment::none});
        }
    } else if (featureA == cylinder_feature::side_edge && featureB == box_feature::vertex) {
        auto pivotB = shB.get_vertex(feature_indexB);
        auto pivotB_world = to_world_space(pivotB, posB, ornB);
        vector3 closest; scalar t;
        closest_point_segment(cyl_vertices[0], cyl_vertices[1], pivotB_world, t, closest);

        auto pivotA_world = closest - sep_axis * shA.radius;
        auto pivotA = to_object_space(pivotA_world, posA, ornA);
        result.add_point({pivotA, pivotB, sep_axis, distance, contact_normal_attachment::none});
    } else if (featureA == cylinder_feature::cap_edge) {
        auto supportA = shA.support_point(posA, ornA, -sep_axis);
        auto pivotA = to_object_space(supportA, posA, ornA);
        auto pivotB = to_object_space(supportA - sep_axis * distance, posB, ornB);
        auto normal_attachment = featureB == box_feature::face ?
            contact_normal_attachment::normal_on_B :
            contact_normal_attachment::none;
        result.maybe_add_point({pivotA, pivotB, sep_axis, distance, normal_attachment});
    }
}

void collide(const box_shape &shA, const cylinder_shape &shB,
             const collision_context &ctx, collision_result &result) {
    swap_collide(shA, shB, ctx, result);
}

}
