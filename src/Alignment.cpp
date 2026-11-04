#include "Alignment.h"
#include <QQuaternion>
#include <cmath>
#include <stdexcept>
namespace youngkly {
QMatrix4x4 rigidAlignment(const QVector<QVector3D> &source, const QVector<QVector3D> &target) {
    if (source.size() != target.size() || source.size() < 3)
        throw std::runtime_error("Alignment needs at least three corresponding anchors");
    QVector3D a, b;
    for (int i = 0; i < source.size(); ++i) { a += source[i]; b += target[i]; }
    a /= source.size(); b /= target.size();
    auto noncollinear = [](const QVector<QVector3D> &points) {
        for (int i = 1; i < points.size(); ++i)
            for (int j = i + 1; j < points.size(); ++j)
                if (QVector3D::crossProduct(points[i] - points[0], points[j] - points[0]).lengthSquared() > 1e-8f) return true;
        return false;
    };
    if (!noncollinear(source) || !noncollinear(target))
        throw std::runtime_error("Alignment anchors are collinear; choose different atom IDs");
    double s[3][3]{};
    for (int i = 0; i < source.size(); ++i) {
        auto x = source[i] - a, y = target[i] - b;
        for (int u = 0; u < 3; ++u) for (int v = 0; v < 3; ++v) s[u][v] += double(x[u]) * y[v];
    }
    double n[4][4]{
        {s[0][0]+s[1][1]+s[2][2],s[1][2]-s[2][1],s[2][0]-s[0][2],s[0][1]-s[1][0]},
        {s[1][2]-s[2][1],s[0][0]-s[1][1]-s[2][2],s[0][1]+s[1][0],s[0][2]+s[2][0]},
        {s[2][0]-s[0][2],s[0][1]+s[1][0],-s[0][0]+s[1][1]-s[2][2],s[1][2]+s[2][1]},
        {s[0][1]-s[1][0],s[0][2]+s[2][0],s[1][2]+s[2][1],-s[0][0]-s[1][1]+s[2][2]}};
    double eigen[4][4]{}; for (int i = 0; i < 4; ++i) eigen[i][i] = 1;
    for (int step = 0; step < 80; ++step) {
        int p = 0, q = 1;
        for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j)
            if (std::abs(n[i][j]) > std::abs(n[p][q])) { p = i; q = j; }
        if (std::abs(n[p][q]) < 1e-12) break;
        const double phi = 0.5 * std::atan2(2 * n[p][q], n[q][q] - n[p][p]);
        const double c = std::cos(phi), d = std::sin(phi);
        double rotation[4][4]{}; for (int i = 0; i < 4; ++i) rotation[i][i] = 1;
        rotation[p][p] = rotation[q][q] = c; rotation[p][q] = d; rotation[q][p] = -d;
        double temp[4][4]{}, next[4][4]{}, vectors[4][4]{};
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) {
            temp[i][j] += n[i][k] * rotation[k][j]; vectors[i][j] += eigen[i][k] * rotation[k][j];
        }
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k)
            next[i][j] += rotation[k][i] * temp[k][j];
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { n[i][j] = next[i][j]; eigen[i][j] = vectors[i][j]; }
    }
    int best = 0; for (int i = 1; i < 4; ++i) if (n[i][i] > n[best][best]) best = i;
    QQuaternion rotation(eigen[0][best], eigen[1][best], eigen[2][best], eigen[3][best]);
    QMatrix4x4 transform; transform.translate(b); transform.rotate(rotation.normalized()); transform.translate(-a);
    return transform;
}
}
