#include "group.hpp"

namespace lightwave {

std::pair<int, int> Group::numberOfPrimitives() const {
    return { int(m_children.size()), 0 };
}

bool Group::intersect(int primitiveIndex, const Ray &ray, Intersection &its,
                      Payload &p, Sampler &rng, bool useHighLOD) const {
    return m_children[primitiveIndex]->intersect(ray, its, p, rng);
}

Bounds Group::getBoundingBox(int primitiveIndex, bool forHighLOD) const {
    return m_children[primitiveIndex]->getBoundingBox();
}

Point Group::getCentroid(int primitiveIndex) const {
    return m_children[primitiveIndex]->getCentroid();
}

Group::Group(const Properties &properties) {
    m_children = properties.getChildren<Shape>();
    buildAccelerationStructure();
}

Group::Group(const ref<Shape> &spine, const ref<Shape> &barbs, const ref<Shape> &barbules) {
    m_children.push_back(spine);
    m_children.push_back(barbs);
    m_children.push_back(barbules);
    buildAccelerationStructure();
}

void Group::markAsVisible() {
    for (auto &child : m_children)
        child->markAsVisible();
}

AreaSample Group::sampleArea(Sampler &rng) const {
    int childIndex = int(rng.next() * m_children.size());
    childIndex     = std::min(childIndex, int(m_children.size()) - 1);

    AreaSample sample = m_children[childIndex]->sampleArea(rng);
    sample.pdf /= m_children.size();
    return sample;
}

void Group::passInfoToFeathers(ref<Bsdf> bsdf) {
    for (ref<Shape> child : m_children) child->passInfoToFeathers(bsdf);
}

std::string Group::toString() const {
    std::stringstream oss;
    oss << "Group[" << std::endl;
    for (auto &entity : m_children) {
        oss << "  " << indent(entity) << "," << std::endl;
    }
    oss << "]";
    return oss.str();
}

} // namespace lightwave

REGISTER_SHAPE(Group, "group")