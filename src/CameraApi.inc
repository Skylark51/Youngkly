    // Youngkly-local adapter; the upstream Youngcraft checkout is not modified.
    QJsonObject reportCameraState() const {
        return {{"target", QJsonArray{m_target.x(), m_target.y(), m_target.z()}},
                {"rotation", QJsonArray{m_rotation.scalar(), m_rotation.x(), m_rotation.y(), m_rotation.z()}},
                {"distance", m_distance}, {"ortho_scale", m_orthoScale},
                {"perspective", m_style.perspective}, {"fov", m_style.perspectiveFov}};
    }
    bool applyCameraState(const QJsonObject &state) {
        auto target = state["target"].toArray(), rotation = state["rotation"].toArray();
        if (target.size() != 3 || rotation.size() != 4 || state["distance"].toDouble() <= 0 || state["ortho_scale"].toDouble() <= 0) return false;
        if (!std::isfinite(state["distance"].toDouble()) || !std::isfinite(state["ortho_scale"].toDouble())) return false;
        for (auto v : target) if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
        for (auto v : rotation) if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
        QQuaternion q(rotation[0].toDouble(), rotation[1].toDouble(), rotation[2].toDouble(), rotation[3].toDouble());
        if (!std::isfinite(q.lengthSquared()) || q.lengthSquared() < 1e-12) return false;
        const auto previous = reportCameraState();
        m_target = QVector3D(target[0].toDouble(), target[1].toDouble(), target[2].toDouble());
        m_rotation = std::abs(q.lengthSquared() - 1.0f) > 1e-6f ? q.normalized() : q;
        m_distance = state["distance"].toDouble(); m_orthoScale = state["ortho_scale"].toDouble();
        m_style.perspective = state["perspective"].toBool(); m_style.perspectiveFov = state["fov"].toDouble(35);
        update();
        if (previous != reportCameraState()) emit cameraChanged();
        return true;
    }
