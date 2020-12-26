#include "edyn/collision/dynamic_tree.hpp"
#include <entt/entt.hpp>

namespace edyn {

dynamic_tree::dynamic_tree()
    : m_root(null_node_id)
    , m_free_list(null_node_id)
{}

dynamic_tree::node_id_t dynamic_tree::allocate() {
    if (m_free_list == null_node_id) {
        auto id = static_cast<node_id_t>(m_nodes.size());
        auto &node = m_nodes.emplace_back();
        node.next = null_node_id;
        node.parent = null_node_id;
        node.child1 = null_node_id;
        node.child2 = null_node_id;
        node.entity = entt::null;
        node.height = 0;
        return id;
    } else {
        auto id = m_free_list;
        auto &node = m_nodes[id];
        node.parent = null_node_id;
        node.child1 = null_node_id;
        node.child2 = null_node_id;
        node.entity = entt::null;
        node.height = 0;
        m_free_list = node.next;
        return id;
    }
}

void dynamic_tree::free(node_id_t id) {
    m_nodes[id].next = m_free_list;
    m_nodes[id].height = -1;
    m_free_list = id;
}

dynamic_tree::node_id_t dynamic_tree::create(const AABB &aabb, entt::entity entity) {
    auto id = allocate();
    auto &node = m_nodes[id];
    node.entity = entity;
    node.aabb = aabb.inset(aabb_inset);

    insert(id);

    return id;
}

void dynamic_tree::destroy(dynamic_tree::node_id_t id) {
    EDYN_ASSERT(m_nodes[id].leaf());
    remove(id);
    free(id);
}

bool dynamic_tree::move(node_id_t id, const AABB &aabb, const vector3 &displacement) {
    auto &node = m_nodes[id];
    EDYN_ASSERT(node.leaf());

    // Extend AABB.
    auto offset_aabb = aabb.inset(aabb_inset);

    // Predict AABB movement.
    auto d = scalar{4} * displacement;

    for (size_t i = 0; i < 3; ++ i) {
        if (d[i] < 0) {
            offset_aabb.min[i] += d[i];
        } else {
            offset_aabb.max[i] += d[i];
        }
    }

    if (node.aabb.contains(aabb)) {
        auto big_aabb = offset_aabb.inset(aabb_inset * scalar{4});

        if (big_aabb.contains(node.aabb)) {
            return false;
        }
    }

    remove(id);
    m_nodes[id].aabb = offset_aabb;
    insert(id);

    // It moved.
    return true;
}

void dynamic_tree::insert(dynamic_tree::node_id_t leaf) {
    if (m_root == null_node_id) {
        m_root = leaf;
        m_nodes[m_root].parent = null_node_id;
        return;
    }

    // Find the best sibling for this node.
    auto leaf_aabb = m_nodes[leaf].aabb;
    auto index = m_root;

    while (!m_nodes[index].leaf()) {
        auto &node = m_nodes[index];

        auto enclosing_area = enclosing_aabb(node.aabb, leaf_aabb).area();

        // Cost of creating a new parent for this node and the new leaf.
        auto cost = scalar{2} * enclosing_area;

        // Minimum cost of pushing the leaf further down the tree.
        auto inherit_cost = scalar{2} * (enclosing_area - node.aabb.area());

        // Cost of descending into child1.
        auto &child_node1 = m_nodes[node.child1];
        auto enclosing_area_child1 = enclosing_aabb(child_node1.aabb, leaf_aabb).area();

        auto cost1 = child_node1.leaf() ?
            enclosing_area_child1 + inherit_cost :
            (enclosing_area_child1 - child_node1.aabb.area()) + inherit_cost;

        // Cost of descending into child2.
        auto &child_node2 = m_nodes[node.child2];
        auto enclosing_area_child2 = enclosing_aabb(child_node2.aabb, leaf_aabb).area();

        auto cost2 = child_node2.leaf() ?
            enclosing_area_child2 + inherit_cost :
            (enclosing_area_child2 - child_node2.aabb.area()) + inherit_cost;

        // Descend according to minimum cost.
        if (cost < cost1 && cost < cost2) {
            // Best node found.
            break;
        }

        // Descend into the best child.
        index = cost1 < cost2 ? node.child1 : node.child2;
    }

    const auto sibling = index;
    auto &sibling_node = m_nodes[sibling];

    // Create new parent.
    auto old_parent = sibling_node.parent;
    auto new_parent = allocate();
    auto &parent_node = m_nodes[new_parent];
    parent_node.parent = old_parent;
    parent_node.entity = entt::null;
    parent_node.aabb = enclosing_aabb(sibling_node.aabb, leaf_aabb);
    parent_node.height = sibling_node.height + 1;

    auto &leaf_node = m_nodes[leaf];

    if (old_parent != null_node_id) {
        // The sibling was not the root.
        auto &old_parent_node = m_nodes[old_parent];
        if (old_parent_node.child1 == sibling) {
            old_parent_node.child1 = new_parent;
        } else {
            old_parent_node.child2 = new_parent;
        }

        parent_node.child1 = sibling;
        parent_node.child2 = leaf;
        sibling_node.parent = new_parent;
        leaf_node.parent = new_parent;
    } else {
        // The sibling was the root.
        parent_node.child1 = sibling;
        parent_node.child2 = leaf;
        sibling_node.parent = new_parent;
        leaf_node.parent = new_parent;
        m_root = new_parent;
    }

    // Walk back up the tree refitting AABBs.
    adjust_bounds(leaf_node.parent);
}

void dynamic_tree::remove(node_id_t leaf) {
    if (leaf == m_root) {
        m_root = null_node_id;
        return;
    }

    auto &node = m_nodes[leaf];
    auto parent = node.parent;
    auto &parent_node = m_nodes[parent];
    auto sibling = parent_node.child1 == leaf ? parent_node.child2 : parent_node.child1;
    EDYN_ASSERT(sibling != null_node_id);
    auto &sibling_node = m_nodes[sibling];

    if (parent == m_root) {
        m_root = sibling;
        sibling_node.parent = null_node_id;
        free(parent);
    } else {
        // Destroy parent and connect sibling to grand parent.
        auto grandpa = parent_node.parent;
        EDYN_ASSERT(grandpa != null_node_id);
        auto &grandpa_node = m_nodes[grandpa];

        if (grandpa_node.child1 == parent) {
            grandpa_node.child1 = sibling;
        } else {
            grandpa_node.child2 = sibling;
        }

        sibling_node.parent = parent_node.parent;
        free(parent);

        adjust_bounds(grandpa);
    }
}

void dynamic_tree::adjust_bounds(dynamic_tree::node_id_t id) {
    while (id != null_node_id) {
        id = balance(id);
        auto &node = m_nodes[id];
        EDYN_ASSERT(node.child1 != null_node_id);
        EDYN_ASSERT(node.child2 != null_node_id);
        node.aabb = enclosing_aabb(m_nodes[node.child1].aabb, m_nodes[node.child2].aabb);
        node.height = std::max(m_nodes[node.child1].height, m_nodes[node.child2].height) + 1;
        id = node.parent;
    }
}

dynamic_tree::node_id_t dynamic_tree::balance(dynamic_tree::node_id_t idA) {
    EDYN_ASSERT(idA != null_node_id);

    auto &nodeA = m_nodes[idA];

    if (nodeA.leaf() || nodeA.height < 2) {
        return idA;
    }

    auto idB = nodeA.child1;
    auto idC = nodeA.child2;
    auto &nodeB = m_nodes[idB];
    auto &nodeC = m_nodes[idC];

    auto balance = nodeC.height - nodeB.height;

    // Rotate C up.
    if (balance > 1) {
        auto idF = nodeC.child1;
        auto idG = nodeC.child2;
        auto &nodeF = m_nodes[idF];
        auto &nodeG = m_nodes[idG];

        // Swap A and C.
        nodeC.child1 = idA;
        nodeC.parent = nodeA.parent;
        nodeA.parent = idC;

        if (nodeC.parent != null_node_id) {
            // A's old parent should point to C.
            auto &parent_nodeC = m_nodes[nodeC.parent];
            if (parent_nodeC.child1 == idA) {
                parent_nodeC.child1 = idC;
            } else {
                parent_nodeC.child2 = idC;
            }
        } else {
            m_root = idC;
        }

        // Rotate.
        if (nodeF.height > nodeG.height) {
            nodeC.child2 = idF;
            nodeA.child2 = idG;
            nodeG.parent = idA;
            nodeA.aabb = enclosing_aabb(nodeB.aabb, nodeG.aabb);
            nodeC.aabb = enclosing_aabb(nodeA.aabb, nodeF.aabb);

            nodeA.height = std::max(nodeB.height, nodeG.height) + 1;
            nodeC.height = std::max(nodeA.height, nodeF.height) + 1;
        } else {
            nodeC.child2 = idG;
            nodeA.child2 = idF;
            nodeF.parent = idA;
            nodeA.aabb = enclosing_aabb(nodeB.aabb, nodeF.aabb);
            nodeC.aabb = enclosing_aabb(nodeA.aabb, nodeG.aabb);

            nodeA.height = std::max(nodeB.height, nodeF.height) + 1;
            nodeC.height = std::max(nodeA.height, nodeG.height) + 1;
        }

        return idC;
    }

    // Rotate B up.
    if (balance < -1) {
        auto idD = nodeB.child1;
        auto idE = nodeB.child2;
        auto &nodeD = m_nodes[idD];
        auto &nodeE = m_nodes[idE];

        // Swap A and B.
        nodeB.child1 = idA;
        nodeB.parent = nodeA.parent;
        nodeA.parent = idB;

        if (nodeB.parent == null_node_id) {
            auto &parent_nodeB = m_nodes[nodeB.parent];
            if (parent_nodeB.child1 == idA) {
                parent_nodeB.child1 = idB;
            } else {
                EDYN_ASSERT(parent_nodeB.child2 == idA);
                parent_nodeB.child2 = idB;
            }
        } else {
            m_root = idB;
        }

        // Rotate.
        if (nodeD.height > nodeE.height) {
            nodeB.child2 = idD;
            nodeA.child1 = idE;
            nodeE.parent = idA;
            nodeA.aabb = enclosing_aabb(nodeC.aabb, nodeE.aabb);
            nodeB.aabb = enclosing_aabb(nodeA.aabb, nodeD.aabb);

            nodeA.height = std::max(nodeC.height, nodeE.height) + 1;
            nodeB.height = std::max(nodeA.height, nodeD.height) + 1;
        } else {
            nodeB.child2 = idE;
            nodeA.child1 = idD;
            nodeD.parent = idA;
            nodeA.aabb = enclosing_aabb(nodeC.aabb, nodeD.aabb);
            nodeB.aabb = enclosing_aabb(nodeA.aabb, nodeE.aabb);

            nodeA.height = std::max(nodeC.height, nodeD.height) + 1;
            nodeB.height = std::max(nodeA.height, nodeE.height) + 1;
        }

        return idB;
    }

    return idA;
}

}