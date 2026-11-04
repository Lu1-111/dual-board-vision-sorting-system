#pragma once
// Validate optional receiver-produced fusion without changing raw model values.
static bool validateFusion(const QJsonObject &obj, const QString &path, const QSize &size,
                           QString *state, QString *basis) {
    const auto sha = [](const QJsonValue &v) { return v.isString() && QRegularExpression("^[0-9a-f]{64}$").match(v.toString()).hasMatch(); };
    const auto reason = [](const QJsonValue &v) { return v.isString() && QRegularExpression("^[A-Za-z0-9_.-]{1,128}$").match(v.toString()).hasMatch(); };
    const auto cv = obj.value("color_assist");
    bool colorValid = false, blue = false, empty = false, skin = false;
    if (!cv.isUndefined() && !cv.isNull()) {
        if (!cv.isObject()) return false;
        const auto c = cv.toObject();
        if (c.value("version").toString() != "color_features_v1" ||
            !QStringList{"green_belt_wide_v1","green_belt_wide_v2"}.contains(c.value("roi_config_id").toString()) ||
            !sha(c.value("config_sha256")) || !sha(c.value("mask_sha256")) || !reason(c.value("reason")) ||
            c.value("source_width").toDouble() != 1920 || c.value("source_height").toDouble() != 1080 || size != QSize(1920,1080)) return false;
        for (const char *key : {"valid","alignment_valid","exposure_valid","blue_present","skin_like_present","skin_blocking","continuous_green"})
            if (!c.value(key).isBool()) return false;
        double g=0,b=0,bl=0,s=0,sl=0,n=0,nl=0,r=0,ms=0;
        if (!number(c.value("green_fraction"),&g,0,1) || !number(c.value("blue_fraction"),&b,0,1) ||
            !number(c.value("blue_largest_fraction"),&bl,0,1) || !number(c.value("skin_fraction"),&s,0,1) ||
            !number(c.value("skin_largest_fraction"),&sl,0,1) || !number(c.value("nongreen_fraction"),&n,0,1) ||
            !number(c.value("nongreen_largest_fraction"),&nl,0,1) || !number(c.value("blue_nongreen_ratio"),&r,0,1) ||
            !number(c.value("processing_ms"),&ms,0,9e15)) return false;
        if (bl>b || sl>s || nl>n || std::abs(r-(n>0?qMin(1.,b/n):0.))>1e-9 ||
            c.value("blue_present").toBool() != (bl>=.001) || c.value("skin_like_present").toBool() != (sl>=.003) ||
            c.value("skin_blocking").toBool() != (s>=.001) || c.value("continuous_green").toBool() != (g>=.95 && nl<=.005)) return false;
        colorValid=c.value("valid").toBool();
        if (colorValid && (!c.value("alignment_valid").toBool() || !c.value("exposure_valid").toBool())) return false;
        skin=s>=.001 || sl>=.003;
        blue=bl>=.001 && !skin && r>=.8 && n-b<=.005;
        empty=g>=.95 && nl<=.005 && bl<.001 && b<.001 && s<.001 && n<=.005 && !skin;
    }
    const auto fv=obj.value("fusion");
    if (fv.isUndefined() || fv.isNull()) return true;
    if (!fv.isObject()) return false;
    const auto roi = obj.value("roi").toObject();
    const QString roiId = roi.value("config_id").toString();
    if (!QStringList{"green_belt_wide_v1","green_belt_wide_v2"}.contains(roiId) ||
        !roi.value("enabled").isBool() || !roi.value("enabled").toBool() || roi.value("mode").toString() != "fixed" ||
        roi.value("x").toDouble(-1) != 0 || roi.value("y").toDouble(-1) != 180 ||
        roi.value("width").toDouble(-1) != (roiId == "green_belt_wide_v2" ? 1580 : 1410) ||
        roi.value("height").toDouble(-1) != 740 || roi.value("source_width").toDouble(-1) != 1920 ||
        roi.value("source_height").toDouble(-1) != 1080 || size != QSize(1920,1080) ||
        (cv.isObject() && cv.toObject().value("roi_config_id").toString() != roiId)) return false;
    if (roiId == "green_belt_wide_v2") {
        const auto m = obj.value("model_roi").toObject();
        if (!m.value("enabled").isBool() || !m.value("enabled").toBool() || m.value("mode").toString() != "fixed" ||
            m.value("config_id").toString() != "green_belt_model_v1" ||
            m.value("x").toDouble(-1) != 0 || m.value("y").toDouble(-1) != 180 ||
            m.value("width").toDouble(-1) != 1410 || m.value("height").toDouble(-1) != 740 ||
            m.value("source_width").toDouble(-1) != 1920 || m.value("source_height").toDouble(-1) != 1080) return false;
        const QRect displayRect(roi.value("x").toInt(),roi.value("y").toInt(),roi.value("width").toInt(),roi.value("height").toInt());
        const QRect modelRect(m.value("x").toInt(),m.value("y").toInt(),m.value("width").toInt(),m.value("height").toInt());
        if (!displayRect.contains(modelRect)) return false;
    }
    const auto f=fv.toObject(); const QString raw=obj.value("state").toString();
    const QString source=f.value("source").toString(), candidate=f.value("candidate_state").toString(), final=f.value("state").toString();
    const QStringList states{"empty","object_present","hand_present","uncertain"};
    int count=0;
    if (f.value("version").toString()!="color_cnn_fusion_v1" || !f.value("enabled").isBool() || !f.value("enabled").toBool() ||
        f.value("policy_id").toString()!="green_belt_fusion_v1" || f.value("model_threshold").toDouble(-1)!=.8 ||
        f.value("required_support").toDouble(-1)!=3 || !integer(f.value("support_count"),&count) || count>3 ||
        f.value("source_session")!=obj.value("source_session") || f.value("frame_sequence")!=obj.value("frame_sequence") ||
        !sha(f.value("frame_sha256")) || f.value("frame_sha256")!=obj.value("jpeg_sha256") ||
        f.value("model_state").toString()!=raw || !states.contains(candidate) || !states.contains(final) || !reason(f.value("reason"))) return false;
    QFile jpeg(path);
    if (!jpeg.open(QIODevice::ReadOnly) || QString::fromLatin1(QCryptographicHash::hash(jpeg.readAll(),QCryptographicHash::Sha256).toHex())!=f.value("frame_sha256").toString()) return false;
    if (source=="model") {
        if (count || final!=raw || candidate!=raw) return false;
        // A qualified opposing observation must be reported as a conflict.
        if (colorValid && raw!="hand_present" && ((skin && raw!="uncertain") || (blue && raw=="empty") || (empty && raw=="object_present"))) return false;
        *basis=QStringLiteral("模型判断");
    } else {
        if (!colorValid || raw=="hand_present") return false;
        if (source=="conflict") {
            if (count || candidate!="uncertain" || final!="uncertain" ||
                !(skin || (blue && raw=="empty") || (empty && raw=="object_present"))) return false;
            *basis=QStringLiteral("证据冲突");
        } else {
            if (raw!="uncertain" || !((candidate=="object_present" && blue) || (candidate=="empty" && empty))) return false;
            if (source=="stabilizing" && count>=1 && count<=2 && final=="uncertain") *basis=QStringLiteral("等待连续确认");
            else if (source=="color_assisted" && count==3 && final==candidate) *basis=QStringLiteral("颜色辅助");
            else return false;
        }
    }
    *state=final;
    return true;
}
