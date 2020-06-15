#ifndef EDYN_SHAPES_PAGED_TRIANGLE_MESH_HPP
#define EDYN_SHAPES_PAGED_TRIANGLE_MESH_HPP

#include <vector>
#include <memory>
#include "edyn/shapes/triangle_mesh.hpp"
#include "edyn/math/constants.hpp"
#include "edyn/serialization/file_archive.hpp"

namespace edyn {

template<typename InputArchiveSource>
class paged_triangle_mesh {
public:
    struct triangle_mesh_node {
        size_t num_vertices;
        size_t num_indices;
        std::unique_ptr<triangle_mesh> trimesh;
    };

    paged_triangle_mesh(std::map<size_t, memory_input_archive::buffer_type> &buffer)
        : m_input_archive_source(buffer)
    {}

    template<typename Func>
    void visit(const AABB &aabb, Func func) {
        constexpr auto inset = vector3 {
            -contact_breaking_threshold, 
            -contact_breaking_threshold, 
            -contact_breaking_threshold
        };
        auto inset_aabb = aabb.inset(inset);
        
        m_tree.visit(inset_aabb, [&] (auto trimesh_idx) {
            auto &node = m_cache[trimesh_idx];

            if (!node.trimesh) {
                // Load triangle mesh into cache. Clear cache if it would go
                // above limits.
                while (cache_num_vertices() + node.num_vertices > m_max_cache_num_vertices) {
                    unload_least_recently_visited_node();
                }

                auto input = m_input_archive_source(trimesh_idx);
                node.trimesh = std::make_unique<triangle_mesh>();
                serialize(input, *node.trimesh);
            }

            node.trimesh->visit(inset_aabb, [=] (uint32_t tri_idx, const triangle_vertices &vertices) {
                func(trimesh_idx, tri_idx, vertices);
            });
            mark_recent_visit(trimesh_idx);
        });
    }

    template<typename Func>
    void visit_all(Func func) {        
        for (size_t i = 0; i < m_cache.size(); ++i) {
            auto &node = m_cache[i];

            if (!node.trimesh) {
                // Load triangle mesh into cache. Clear cache if it would go
                // above limits.
                while (cache_num_vertices() + node.num_vertices > m_max_cache_num_vertices) {
                    unload_least_recently_visited_node();
                }

                auto input = m_input_archive_source(i);
                node.trimesh = std::make_unique<triangle_mesh>();
                serialize(input, *node.trimesh);
            }

            node.trimesh->visit_all([=] (uint32_t tri_idx, const triangle_vertices &vertices) {
                func(i, tri_idx, vertices);
            });
        }
    }

    template<typename Func>
    void visit_cache(const AABB &aabb, Func func) const {
        constexpr auto inset = vector3 {
            -contact_breaking_threshold, 
            -contact_breaking_threshold, 
            -contact_breaking_threshold
        };

        for (size_t i = 0; i < m_cache.size(); ++i) {
            auto &node = m_cache[i];

            if (node.trimesh) {
                node.trimesh->visit(aabb, [=] (uint32_t tri_idx, const triangle_vertices &vertices) {
                    func(i, tri_idx, vertices);
                });
            }
        }
    }

    template<typename Func>
    void visit_cache_all(Func func) const {
        for (size_t i = 0; i < m_cache.size(); ++i) {
            auto &node = m_cache[i];

            if (node.trimesh) {
                node.trimesh->visit_all( [=] (uint32_t tri_idx, const triangle_vertices &vertices) {
                    func(i, tri_idx, vertices);
                });
            }
        }
    }

    size_t cache_num_vertices() const {
        size_t count = 0;

        for (auto &node : m_cache) {
            if (!node.trimesh) {
                continue;
            }

            count += node.num_vertices;
        }

        return count;
    }

    void mark_recent_visit(size_t trimesh_idx) {
        auto it = std::find(m_lru_indices.begin(), m_lru_indices.end(), trimesh_idx);
        std::rotate(m_lru_indices.begin(), it, it + 1);
    }

    void unload_least_recently_visited_node() {
        for (auto it = m_lru_indices.rbegin(); it != m_lru_indices.rend(); ++it) {
            auto &node = m_cache[*it];
            if (node.trimesh) {
                unload_node(node);
                break;
            }
        }
    }

    void unload_node(triangle_mesh_node &node) {
        node.trimesh.reset();
    }

    triangle_mesh *get_submesh(size_t idx) {
        return m_cache[idx].trimesh.get();
    }

    size_t m_max_cache_num_vertices = 1 << 13;

    AABB m_aabb;
    static_tree m_tree;
    std::vector<triangle_mesh_node> m_cache;
    std::vector<size_t> m_lru_indices;
    InputArchiveSource m_input_archive_source;
};

template<typename VertexIterator, typename IndexIterator,
         typename InputArchiveSource, typename OutputArchiveSource>
void load_paged_triangle_mesh(paged_triangle_mesh<InputArchiveSource> &mesh,
                              VertexIterator vertex_begin, VertexIterator vertex_end,
                              IndexIterator index_begin, IndexIterator index_end,
                              OutputArchiveSource &output_archive_source,
                              uint32_t max_obj_per_leaf = 1024) {
    mesh.m_aabb.min = vector3_max;
    mesh.m_aabb.max = -vector3_max;

    for (auto it = vertex_begin; it != vertex_end; ++it) {
        mesh.m_aabb.min = min(mesh.m_aabb.min, *it);
        mesh.m_aabb.max = max(mesh.m_aabb.max, *it);
    }

    auto num_indices = std::distance(index_begin, index_end);
    auto num_triangles = num_indices / 3;
    
    std::vector<AABB> aabbs;
    aabbs.reserve(num_triangles);

    for (size_t i = 0; i < num_triangles; ++i) {
        auto verts = triangle_vertices{
            *(vertex_begin + *(index_begin + (i * 3 + 0))),
            *(vertex_begin + *(index_begin + (i * 3 + 1))),
            *(vertex_begin + *(index_begin + (i * 3 + 2)))
        };

        auto tri_aabb = get_triangle_aabb(verts);
        aabbs.push_back(tri_aabb);
    }

    auto report_leaf = [&] (static_tree::tree_node &node, auto ids_begin, auto ids_end) {
        // Transform triangle indices into vertex indices.
        auto local_num_triangles = std::distance(ids_begin, ids_end);
        auto global_indices = std::vector<size_t>();
        global_indices.reserve(local_num_triangles * 3);
        
        for (auto it = ids_begin; it != ids_end; ++it) {
            for (size_t i = 0; i < 3; ++i) {
                auto index = *(index_begin + ((*it) * 3 + i));
                global_indices.push_back(index);
            }
        }

        // Transform global indices into local indices by removing duplicates.
        // `local_indices` maps local indices to global indices.
        auto local_indices = global_indices;
        std::sort(local_indices.begin(), local_indices.end());
        local_indices.erase(std::unique(local_indices.begin(), local_indices.end()), local_indices.end());

        auto trimesh = triangle_mesh();
        trimesh.vertices.reserve(local_indices.size());
        trimesh.indices.reserve(global_indices.size());

        for (auto idx : local_indices) {
            trimesh.vertices.push_back(*(vertex_begin + idx));
        }

        // Obtain local indices from global indices.
        for (auto idx : global_indices) {
            auto it = std::find(local_indices.begin(), local_indices.end(), idx);
            auto local_idx = std::distance(local_indices.begin(), it);
            trimesh.indices.push_back(local_idx);
        }

        trimesh.calculate_aabb();
        trimesh.build_tree();
        trimesh.calculate_edge_angles();

        node.id = mesh.m_cache.size();
        auto &paged_node = mesh.m_cache.emplace_back();
        paged_node.num_vertices = trimesh.vertices.size();
        paged_node.num_indices = trimesh.indices.size();

        auto output = output_archive_source(node.id);
        serialize(output, trimesh);
    };

    mesh.m_tree.build(aabbs.begin(), aabbs.end(), report_leaf, max_obj_per_leaf);

    mesh.m_lru_indices.resize(mesh.m_cache.size());
    std::iota(mesh.m_lru_indices.begin(), mesh.m_lru_indices.end(), 0);

    // Calculate edge angles.
    constexpr scalar merge_distance = 0.01;
    mesh.visit_all([&mesh] (size_t mesh_idx_i, size_t tri_idx_i, 
                            const triangle_vertices &vertices_i) {
        // Normal vector of i-th triangle.
        auto edge0_i = vertices_i[1] - vertices_i[0];
        auto edge1_i = vertices_i[2] - vertices_i[1];
        auto normal_i = cross(edge0_i, edge1_i);
        auto normal_len_sqr_i = length_sqr(normal_i);

        if (normal_len_sqr_i > EDYN_EPSILON) {
            normal_i /= std::sqrt(normal_len_sqr_i);
        }

        auto inset = vector3 {-EDYN_EPSILON, -EDYN_EPSILON, -EDYN_EPSILON};
        auto tri_aabb = get_triangle_aabb(vertices_i).inset(inset);

        mesh.visit(tri_aabb, [&] (size_t mesh_idx_k, size_t tri_idx_k, 
                                  const triangle_vertices &vertices_k) {
            if (mesh_idx_i == mesh_idx_k && tri_idx_i == tri_idx_k) {
                return;
            }

            // Look for shared edge.
            std::pair<size_t, size_t> shared_idx[2];
            auto num_shared_vertices = 0;

            for (size_t m = 0; m < 3; ++m) {
                for (size_t n = 0; n < 3; ++n) {
                    if (distance_sqr(vertices_i[m], vertices_k[n]) < merge_distance * merge_distance) {
                        shared_idx[num_shared_vertices] = std::make_pair(m, n);
                        ++num_shared_vertices;

                        if (num_shared_vertices > 1) {
                            break;
                        }
                    }
                }
            }

            if (num_shared_vertices < 2) {
                return;
            }

            // Find index of the vertex in triangle k not in edge m.
            size_t other_idx = 0;
            if (shared_idx[0].second == other_idx) {
                ++other_idx;
                if (shared_idx[1].second == other_idx) {
                    ++other_idx;
                }
            } 
            
            if (shared_idx[1].second == other_idx) {
                ++other_idx;
                if (shared_idx[0].second == other_idx) {
                    ++other_idx;
                }
            }

            // Check if the vertex in triangle k which is not in the shared 
            // edge is in front of or behind the plane of triangle i.
            auto concave = dot(normal_i, vertices_k[other_idx] - vertices_k[shared_idx[0].second]) > -EDYN_EPSILON;
            mesh.m_cache[mesh_idx_i].trimesh->is_concave_edge[tri_idx_i * 3 + shared_idx[0].first] = concave;
            mesh.m_cache[mesh_idx_k].trimesh->is_concave_edge[tri_idx_k * 3 + shared_idx[0].second] = concave;

            // Normal vector of k-th triangle.
            auto edge0_k = vertices_k[1] - vertices_k[0];
            auto edge1_k = vertices_k[2] - vertices_k[1];
            auto normal_k = cross(edge0_k, edge1_k);
            auto normal_len_sqr_k = length_sqr(normal_k);

            if (normal_len_sqr_k > EDYN_EPSILON) {
                normal_k /= std::sqrt(normal_len_sqr_k);
            }

            // Get edge angle from the cross product of normals and use 
            // the sign to classify it as a convex or concave edge.
            auto cos_angle = dot(normal_i, normal_k);
            mesh.m_cache[mesh_idx_i].trimesh->cos_angles[tri_idx_i * 3 + shared_idx[0].first] = cos_angle;
            mesh.m_cache[mesh_idx_k].trimesh->cos_angles[tri_idx_k * 3 + shared_idx[0].second] = cos_angle;
        });
    });

    for (auto &node : mesh.m_cache) {
        node.trimesh.reset();
    }
}

}

#endif // EDYN_SHAPES_PAGED_TRIANGLE_MESH_HPP