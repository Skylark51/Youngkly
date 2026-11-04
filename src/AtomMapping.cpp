#include "AtomMapping.h"
#include <QSet>
#include <stdexcept>
namespace youngkly {
QVector<QVector<int>> atomCorrespondence(const QVector<QVector<int>> &elements, const QJsonArray &configured) {
    if (elements.isEmpty()) throw std::runtime_error("Nonempty structures are required");
    const int reference = elements.size() > 1 ? 1 : 0;
    const int count = elements[reference].size();
    if (!count) throw std::runtime_error("Nonempty structures are required");
    if (!configured.isEmpty() && configured.size() != elements.size()) throw std::runtime_error("atom_mapping needs one array per structure in sheet order");
    QVector<QVector<int>> output;
    for (int i = 0; i < elements.size(); ++i) {
        if (elements[i].size() != count) throw std::runtime_error("Different atom counts require manual review");
        auto ids = configured.isEmpty() ? QJsonArray() : configured[i].toArray();
        if (!configured.isEmpty() && ids.size() != count) throw std::runtime_error("atom_mapping length differs from atom count");
        QVector<int> map; QSet<int> seen;
        for (int a = 0; a < count; ++a) {
            const int id = configured.isEmpty() ? a + 1 : ids[a].toInt();
            if (id < 1 || id > count || seen.contains(id) || (i == reference && id != a + 1))
                throw std::runtime_error("Mapping must be bijective; reference mapping must retain its original atom IDs");
            if (elements[i][id - 1] != elements[reference][a]) throw std::runtime_error("Atom elements differ; explicit atom_mapping is required");
            seen.insert(id); map << id;
        }
        output << map;
    }
    return output;
}
QJsonArray mappingJson(const QVector<QVector<int>> &mapping) {
    QJsonArray result;
    for (auto ids : mapping) { QJsonArray row; for (int id : ids) row << id; result << row; }
    return result;
}
}
