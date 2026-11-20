#include "ProjectSerializer.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QDateTime>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace omniframe
{

    namespace
    {
        inline QString trUi(const char *text)
        {
            return QCoreApplication::translate("InterfaceGrafica", text);
        }

        bool readFiniteDouble(const QJsonObject &obj, const char *key, double defaultVal, bool required, double &out)
        {
            if (!obj.contains(key))
            {
                if (required)
                    return false;
                out = defaultVal;
                return std::isfinite(out);
            }
            const QJsonValue v = obj.value(key);
            if (!v.isDouble())
                return false;
            const double val = v.toDouble();
            if (!std::isfinite(val))
                return false;
            out = val;
            return true;
        }

        bool readValidInt(const QJsonObject &obj, const char *key, int defaultVal, bool required, int &out)
        {
            if (!obj.contains(key))
            {
                if (required)
                    return false;
                out = defaultVal;
                return true;
            }
            const QJsonValue v = obj.value(key);
            if (!v.isDouble())
                return false;
            const double d = v.toDouble();
            if (!std::isfinite(d) || std::floor(d) != d)
                return false;
            out = static_cast<int>(d);
            return true;
        }
    } // namespace

    bool ProjectSerializer::saveToFile(const QString &filePath, const StructuralModel &model)
    {
        QJsonObject rootObj;
        if (!model.preservedUnknownJson().empty())
        {
            const QJsonDocument unkDoc = QJsonDocument::fromJson(QByteArray::fromStdString(model.preservedUnknownJson()));
            if (unkDoc.isObject())
            {
                rootObj = unkDoc.object();
            }
        }
        rootObj["version"] = "2.1.0";
        rootObj["schemaVersion"] = "2.1.0";
        rootObj["application"] = "OmniFrame 3D";
        rootObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        // 0. Metadados do Projeto e Parametros NBR (Schema 2.1.0)
        {
            const auto &meta = model.metadata();
            QJsonObject metaObj;
            metaObj["projectName"] = QString::fromStdString(meta.projectName);
            metaObj["engineerName"] = QString::fromStdString(meta.engineerName);
            metaObj["clientName"] = QString::fromStdString(meta.clientName);
            metaObj["siteLocation"] = QString::fromStdString(meta.siteLocation);
            metaObj["soilSigmaAdm_kPa"] = meta.soilSigmaAdm_kPa;
            metaObj["footingB_m"] = meta.footingB_m;
            metaObj["footingL_m"] = meta.footingL_m;
            metaObj["windV0_ms"] = meta.windV0_ms;
            metaObj["windS1"] = meta.windS1;
            metaObj["windS2"] = meta.windS2;
            metaObj["windS3"] = meta.windS3;
            rootObj["projectMetadata"] = metaObj;
        }

        // 1. Materiais
        QJsonArray matArray;
        for (const auto &mat : model.getMaterials())
        {
            QJsonObject mObj;
            mObj["id"] = mat.id;
            mObj["name"] = QString::fromStdString(mat.name);
            mObj["E"] = mat.E;
            mObj["nu"] = mat.nu;
            mObj["alpha"] = mat.alpha;
            mObj["gamma"] = mat.gamma;
            mObj["rho"] = mat.rho;
            mObj["isPlastic"] = mat.isPlastic;
            mObj["sigmaY"] = mat.sigmaY;
            mObj["H_iso"] = mat.H_iso;
            mObj["H_kin"] = mat.H_kin;
            matArray.append(mObj);
        }
        rootObj["materials"] = matArray;

        // 2. Seções Transversais
        QJsonArray secArray;
        for (const auto &sec : model.getCrossSections())
        {
            QJsonObject sObj;
            sObj["id"] = sec.id;
            sObj["name"] = QString::fromStdString(sec.name);
            sObj["shape"] = static_cast<int>(sec.shape);
            sObj["dim1"] = sec.dim1;
            sObj["dim2"] = sec.dim2;
            sObj["dim3"] = sec.dim3;
            sObj["dim4"] = sec.dim4;
            sObj["dim5"] = sec.dim5;
            sObj["dim6"] = sec.dim6;
            sObj["A"] = sec.A;
            sObj["Iy"] = sec.Iy;
            sObj["Iz"] = sec.Iz;
            sObj["J"] = sec.J;
            sObj["Iyz"] = sec.Iyz;
            sObj["I1"] = sec.I1;
            sObj["I2"] = sec.I2;
            sObj["principalAngle"] = sec.principalAngle;
            sObj["Cw"] = sec.Cw;
            sObj["Asy"] = sec.Asy;
            sObj["Asz"] = sec.Asz;
            if (!sec.polygonLoops.empty())
            {
                QJsonArray loopsArr;
                for (const auto &lp : sec.polygonLoops)
                {
                    QJsonObject lObj;
                    lObj["isHole"] = lp.isHole;
                    QJsonArray pts;
                    for (const auto &pt : lp.vertices)
                    {
                        QJsonArray pArr;
                        pArr.append(pt[0]);
                        pArr.append(pt[1]);
                        pts.append(pArr);
                    }
                    lObj["vertices"] = pts;
                    loopsArr.append(lObj);
                }
                sObj["polygonLoops"] = loopsArr;
            }
            if (!sec.compositeParts.empty())
            {
                QJsonArray compArr;
                for (const auto &cp : sec.compositeParts)
                {
                    QJsonObject cObj;
                    cObj["yCenter"] = cp.yCenter;
                    cObj["zCenter"] = cp.zCenter;
                    cObj["widthB"] = cp.widthB;
                    cObj["heightH"] = cp.heightH;
                    cObj["modularRatioN"] = cp.modularRatioN;
                    compArr.append(cObj);
                }
                sObj["compositeParts"] = compArr;
            }
            secArray.append(sObj);
        }
        rootObj["sections"] = secArray;

        // 3. Nós
        QJsonArray nodeArray;
        for (const auto &n : model.getNodes())
        {
            QJsonObject nObj;
            nObj["id"] = n.id;
            nObj["x"] = n.x();
            nObj["y"] = n.y();
            nObj["z"] = n.z();

            // Apoios
            QJsonObject supObj;
            QJsonArray fixArr, sprArr, prsArr, hasPrsArr;
            for (int k = 0; k < 6; ++k)
            {
                fixArr.append(n.support.fixed[k]);
                sprArr.append(n.support.springStiffness[k]);
                prsArr.append(n.support.prescribedValue[k]);
                hasPrsArr.append(n.support.hasPrescribed[k]);
            }
            supObj["fixed"] = fixArr;
            supObj["springs"] = sprArr;
            supObj["prescribed"] = prsArr;
            supObj["hasPrescribed"] = hasPrsArr;
            nObj["support"] = supObj;

            // Cargas Nodais
            QJsonObject ldObj;
            ldObj["fx"] = n.load.force.x;
            ldObj["fy"] = n.load.force.y;
            ldObj["fz"] = n.load.force.z;
            ldObj["mx"] = n.load.moment.x;
            ldObj["my"] = n.load.moment.y;
            ldObj["mz"] = n.load.moment.z;
            nObj["load"] = ldObj;

            nodeArray.append(nObj);
        }
        rootObj["nodes"] = nodeArray;

        // 4. Elementos (Barras)
        QJsonArray elemArray;
        for (const auto &e : model.getElements())
        {
            QJsonObject eObj;
            eObj["id"] = e.id;
            eObj["nodeStart"] = e.nodeStart;
            eObj["nodeEnd"] = e.nodeEnd;
            eObj["materialId"] = e.materialId;
            eObj["sectionId"] = e.sectionId;
            eObj["rollAngle"] = e.rollAngle;
            eObj["formulation"] = static_cast<int>(e.formulation);

            // Rótulas
            QJsonObject relObj;
            QJsonArray startRelArr, endRelArr;
            for (int k = 0; k < 6; ++k)
            {
                startRelArr.append(e.releases.start[k]);
                endRelArr.append(e.releases.end[k]);
            }
            relObj["start"] = startRelArr;
            relObj["end"] = endRelArr;
            eObj["releases"] = relObj;

            // Carga Distribuída
            QJsonObject qObj;
            qObj["active"] = e.distLoad.active;
            qObj["coordSystem"] = static_cast<int>(e.distLoad.coordSystem);
            qObj["qx"] = e.distLoad.w.x;
            qObj["qy"] = e.distLoad.w.y;
            qObj["qz"] = e.distLoad.w.z;
            eObj["distLoad"] = qObj;

            // Carga Térmica
            QJsonObject thObj;
            thObj["active"] = e.thermalLoad.active;
            thObj["deltaT"] = e.thermalLoad.deltaT;
            eObj["thermalLoad"] = thObj;

            elemArray.append(eObj);
        }
        rootObj["elements"] = elemArray;

        // 5. Casos de Carga (Schema 2.0.0)
        QJsonArray lcArray;
        for (const auto &lc : model.getLoadCases())
        {
            QJsonObject lcObj;
            lcObj["id"] = lc.id;
            lcObj["name"] = QString::fromStdString(lc.name);
            lcObj["category"] = static_cast<int>(lc.category);

            QJsonObject swObj;
            swObj["enabled"] = lc.selfWeight.enabled;
            swObj["factor"] = lc.selfWeight.factor;
            swObj["dx"] = lc.selfWeight.direction.x();
            swObj["dy"] = lc.selfWeight.direction.y();
            swObj["dz"] = lc.selfWeight.direction.z();
            lcObj["selfWeight"] = swObj;

            QJsonArray nlArr;
            for (const auto &nl : lc.nodalLoads)
            {
                QJsonObject nlObj;
                nlObj["nodeId"] = nl.nodeId;
                nlObj["fx"] = nl.forceMoment[0];
                nlObj["fy"] = nl.forceMoment[1];
                nlObj["fz"] = nl.forceMoment[2];
                nlObj["mx"] = nl.forceMoment[3];
                nlObj["my"] = nl.forceMoment[4];
                nlObj["mz"] = nl.forceMoment[5];
                nlArr.append(nlObj);
            }
            lcObj["nodalLoads"] = nlArr;

            QJsonArray elArr;
            for (const auto &el : lc.elementLoads)
            {
                QJsonObject elObj;
                elObj["elementId"] = el.elementId;
                elObj["hasDistributed"] = el.load.hasDistributed;
                elObj["coordSystem"] = el.load.coordSystem;
                elObj["qx"] = el.load.distributedForce.x();
                elObj["qy"] = el.load.distributedForce.y();
                elObj["qz"] = el.load.distributedForce.z();
                elObj["hasThermal"] = el.load.hasThermal;
                elObj["deltaT"] = el.load.deltaT;
                elObj["normalPressure"] = el.load.normalPressure;
                elArr.append(elObj);
            }
            lcObj["elementLoads"] = elArr;
            lcArray.append(lcObj);
        }
        rootObj["loadCases"] = lcArray;

        // 6. Combinações
        QJsonArray combArray;
        for (const auto &comb : model.getLoadCombinations())
        {
            QJsonObject cObj;
            cObj["id"] = comb.id;
            cObj["name"] = QString::fromStdString(comb.name);
            cObj["limitState"] = static_cast<int>(comb.limitState);
            cObj["pDeltaEnabled"] = comb.pDeltaEnabled;
            QJsonArray facArr;
            for (const auto &f : comb.factors)
            {
                QJsonObject fObj;
                fObj["loadCaseId"] = f.loadCaseId;
                fObj["gamma"] = f.gamma;
                facArr.append(fObj);
            }
            cObj["factors"] = facArr;
            combArray.append(cObj);
        }
        rootObj["loadCombinations"] = combArray;

        // 7. Históricos temporais
        QJsonArray histArray;
        for (const auto &h : model.getLoadHistories())
        {
            QJsonObject hObj;
            hObj["id"] = h.id;
            hObj["name"] = QString::fromStdString(h.name);
            hObj["loadCaseId"] = h.loadCaseId;
            QJsonArray pts;
            for (const auto &p : h.points)
            {
                QJsonObject pObj;
                pObj["t"] = p.timeOrStep;
                pObj["f"] = p.factor;
                pts.append(pObj);
            }
            hObj["points"] = pts;
            histArray.append(hObj);
        }
        rootObj["loadHistories"] = histArray;

        // 8. Vínculos MPC (Ligações Rígidas e Diafragmas)
        QJsonArray rlArray;
        for (const auto &rl : model.getRigidLinks())
        {
            QJsonObject o;
            o["masterNode"] = rl.masterNode;
            o["slaveNode"] = rl.slaveNode;
            o["coupleRotations"] = rl.coupleRotations;
            rlArray.append(o);
        }
        rootObj["rigidLinks"] = rlArray;

        QJsonArray diaArray;
        for (const auto &dia : model.getRigidDiaphragms())
        {
            QJsonObject o;
            o["masterNode"] = dia.masterNode;
            o["normalAxis"] = dia.normalAxis;
            QJsonArray sl;
            for (NodeId sid : dia.slaveNodes) sl.append(sid);
            o["slaveNodes"] = sl;
            diaArray.append(o);
        }
        rootObj["rigidDiaphragms"] = diaArray;

        // 9. Opções de Solver Linear
        const auto &so = model.solverOptions();
        QJsonObject soObj;
        soObj["type"] = static_cast<int>(so.type);
        soObj["ordering"] = static_cast<int>(so.ordering);
        soObj["preconditioner"] = static_cast<int>(so.preconditioner);
        soObj["maxIterations"] = so.maxIterations;
        soObj["tolerance"] = so.tolerance;
        soObj["iterativeRefinement"] = so.iterativeRefinement;
        soObj["refinementSteps"] = so.refinementSteps;
        rootObj["solverOptions"] = soObj;

        // 10. Malha Superficial / Solida (Schema 2.1.0)
        const auto &sm = model.surfaceMesh();
        if (!sm.nodes.empty() || !sm.elements.empty())
        {
            QJsonObject smObj;
            QJsonArray smNodes;
            for (const auto &mn : sm.nodes)
            {
                QJsonObject nObj;
                nObj["id"] = mn.id;
                nObj["x"] = mn.position.x();
                nObj["y"] = mn.position.y();
                nObj["z"] = mn.position.z();
                nObj["isBoundary"] = mn.isBoundary;
                if (!mn.tag.empty())
                    nObj["tag"] = QString::fromStdString(mn.tag);
                smNodes.append(nObj);
            }
            smObj["nodes"] = smNodes;

            QJsonArray smElems;
            for (const auto &me : sm.elements)
            {
                QJsonObject eObj;
                eObj["id"] = me.id;
                eObj["type"] = static_cast<int>(me.type);
                if (!me.groupTag.empty())
                    eObj["groupTag"] = QString::fromStdString(me.groupTag);
                QJsonArray nidArr;
                for (int nid : me.nodeIds)
                    nidArr.append(nid);
                eObj["nodeIds"] = nidArr;
                smElems.append(eObj);
            }
            smObj["elements"] = smElems;
            rootObj["surfaceMesh"] = smObj;
        }

        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }

        const QByteArray payload = QJsonDocument(rootObj).toJson(QJsonDocument::Indented);
        if (file.write(payload) != payload.size())
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

    bool ProjectSerializer::loadFromFile(const QString &filePath, StructuralModel &model)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }

        const QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }

        const QJsonObject rootObj = doc.object();
        StructuralModel loaded;
        loaded.clear();

        // Preserva campos desconhecidos de esquemas futuros com aviso explícito
        {
            static const std::unordered_set<std::string> knownKeys{
                "version", "schemaVersion", "application", "timestamp", "projectMetadata",
                "materials", "sections", "nodes", "elements",
                "loadCases", "loadCombinations", "loadHistories",
                "rigidLinks", "rigidDiaphragms", "solverOptions", "surfaceMesh"
            };
            QJsonObject unknownObj;
            QStringList unknownNames;
            for (auto it = rootObj.constBegin(); it != rootObj.constEnd(); ++it)
            {
                if (!knownKeys.contains(it.key().toStdString()))
                {
                    unknownObj.insert(it.key(), it.value());
                    unknownNames.push_back(it.key());
                }
            }
            if (!unknownObj.isEmpty())
            {
                loaded.preservedUnknownJson() =
                    QJsonDocument(unknownObj).toJson(QJsonDocument::Compact).toStdString();
                loaded.serializationWarnings().push_back(
                    "Campos desconhecidos de esquema preservados sem alteração: " +
                    unknownNames.join(", ").toStdString());
            }
        }

        // 0. Metadados do Projeto (Schema 2.1.0, opcional para compatibilidade 1.0/2.0)
        if (rootObj.contains("projectMetadata") && rootObj["projectMetadata"].isObject())
        {
            const QJsonObject metaObj = rootObj["projectMetadata"].toObject();
            auto &meta = loaded.metadata();
            if (metaObj.contains("projectName")) meta.projectName = metaObj["projectName"].toString().toStdString();
            if (metaObj.contains("engineerName")) meta.engineerName = metaObj["engineerName"].toString().toStdString();
            if (metaObj.contains("clientName")) meta.clientName = metaObj["clientName"].toString().toStdString();
            if (metaObj.contains("siteLocation")) meta.siteLocation = metaObj["siteLocation"].toString().toStdString();
            readFiniteDouble(metaObj, "soilSigmaAdm_kPa", meta.soilSigmaAdm_kPa, false, meta.soilSigmaAdm_kPa);
            readFiniteDouble(metaObj, "footingB_m", meta.footingB_m, false, meta.footingB_m);
            readFiniteDouble(metaObj, "footingL_m", meta.footingL_m, false, meta.footingL_m);
            readFiniteDouble(metaObj, "windV0_ms", meta.windV0_ms, false, meta.windV0_ms);
            readFiniteDouble(metaObj, "windS1", meta.windS1, false, meta.windS1);
            readFiniteDouble(metaObj, "windS2", meta.windS2, false, meta.windS2);
            readFiniteDouble(metaObj, "windS3", meta.windS3, false, meta.windS3);
        }

        // 1. Materiais
        if (rootObj.contains("materials"))
        {
            if (!rootObj["materials"].isArray())
                return false;
            loaded.getMaterials().clear();
            std::unordered_set<MaterialId> matIds;
            for (const auto &val : rootObj["materials"].toArray())
            {
                if (!val.isObject())
                    return false;
                const QJsonObject mObj = val.toObject();
                Material mat;
                if (!readValidInt(mObj, "id", 0, true, mat.id) || mat.id <= 0 || !matIds.insert(mat.id).second)
                    return false;
                mat.name = mObj["name"].toString().toStdString();
                if (!readFiniteDouble(mObj, "E", 2.0e8, false, mat.E) || mat.E <= 0.0 ||
                    !readFiniteDouble(mObj, "nu", 0.3, false, mat.nu) || mat.nu <= -1.0 || mat.nu >= 0.5 ||
                    !readFiniteDouble(mObj, "alpha", 1.2e-5, false, mat.alpha) || mat.alpha < 0.0 ||
                    !readFiniteDouble(mObj, "gamma", 78.5, false, mat.gamma) || mat.gamma < 0.0)
                    return false;
                const double defaultRho = mat.gamma * (1000.0 / 9.80665);
                if (!readFiniteDouble(mObj, "rho", defaultRho, false, mat.rho) || mat.rho < 0.0)
                    return false;
                mat.isPlastic = mObj["isPlastic"].toBool(false);
                if (!readFiniteDouble(mObj, "sigmaY", 2.5e5, false, mat.sigmaY) || mat.sigmaY < 0.0 ||
                    !readFiniteDouble(mObj, "H_iso", 0.0, false, mat.H_iso) ||
                    !readFiniteDouble(mObj, "H_kin", 0.0, false, mat.H_kin))
                    return false;
                loaded.addMaterial(mat);
            }
        }

        // 2. Seções
        if (rootObj.contains("sections"))
        {
            if (!rootObj["sections"].isArray())
                return false;
            loaded.getCrossSections().clear();
            std::unordered_set<SectionId> secIds;
            for (const auto &val : rootObj["sections"].toArray())
            {
                if (!val.isObject())
                    return false;
                const QJsonObject sObj = val.toObject();
                CrossSection sec;
                if (!readValidInt(sObj, "id", 0, true, sec.id) || sec.id <= 0 || !secIds.insert(sec.id).second)
                    return false;
                sec.name = sObj["name"].toString().toStdString();
                int shapeInt = 0;
                if (!readValidInt(sObj, "shape", 0, false, shapeInt) || shapeInt < 0 || shapeInt > 11)
                    return false;
                sec.shape = static_cast<SectionShape>(shapeInt);
                if (!readFiniteDouble(sObj, "dim1", 0.2, false, sec.dim1) ||
                    !readFiniteDouble(sObj, "dim2", 0.4, false, sec.dim2) ||
                    !readFiniteDouble(sObj, "dim3", 0.01, false, sec.dim3) ||
                    !readFiniteDouble(sObj, "dim4", 0.01, false, sec.dim4) ||
                    !readFiniteDouble(sObj, "dim5", 0.165, false, sec.dim5) ||
                    !readFiniteDouble(sObj, "dim6", 0.0102, false, sec.dim6) ||
                    !readFiniteDouble(sObj, "A", 0.08, true, sec.A) || sec.A <= 0.0 ||
                    !readFiniteDouble(sObj, "Iy", 1e-3, true, sec.Iy) || sec.Iy <= 0.0 ||
                    !readFiniteDouble(sObj, "Iz", 1e-3, true, sec.Iz) || sec.Iz <= 0.0 ||
                    !readFiniteDouble(sObj, "J", 5e-4, true, sec.J) || sec.J <= 0.0 ||
                    !readFiniteDouble(sObj, "Iyz", 0.0, false, sec.Iyz) ||
                    !readFiniteDouble(sObj, "I1", sec.Iy, false, sec.I1) ||
                    !readFiniteDouble(sObj, "I2", sec.Iz, false, sec.I2) ||
                    !readFiniteDouble(sObj, "principalAngle", 0.0, false, sec.principalAngle) ||
                    !readFiniteDouble(sObj, "Cw", 0.0, false, sec.Cw) ||
                    !readFiniteDouble(sObj, "Asy", (5.0 / 6.0) * sec.A, false, sec.Asy) ||
                    !readFiniteDouble(sObj, "Asz", (5.0 / 6.0) * sec.A, false, sec.Asz))
                    return false;
                if (sObj.contains("polygonLoops") && sObj["polygonLoops"].isArray())
                {
                    for (const auto &lv : sObj["polygonLoops"].toArray())
                    {
                        if (!lv.isObject()) return false;
                        const QJsonObject lObj = lv.toObject();
                        kernel::PolygonLoop2D lp;
                        lp.isHole = lObj["isHole"].toBool(false);
                        if (lObj.contains("vertices") && lObj["vertices"].isArray())
                        {
                            for (const auto &pv : lObj["vertices"].toArray())
                            {
                                if (!pv.isArray()) return false;
                                const QJsonArray pArr = pv.toArray();
                                if (pArr.size() != 2 || !pArr[0].isDouble() || !pArr[1].isDouble()) return false;
                                const double py = pArr[0].toDouble();
                                const double pz = pArr[1].toDouble();
                                if (!std::isfinite(py) || !std::isfinite(pz)) return false;
                                lp.vertices.push_back({py, pz});
                            }
                        }
                        sec.polygonLoops.push_back(std::move(lp));
                    }
                }
                if (sObj.contains("compositeParts") && sObj["compositeParts"].isArray())
                {
                    for (const auto &cv : sObj["compositeParts"].toArray())
                    {
                        if (!cv.isObject()) return false;
                        const QJsonObject cObj = cv.toObject();
                        kernel::CompositeRectPart cp;
                        if (!readFiniteDouble(cObj, "yCenter", 0.0, false, cp.yCenter) ||
                            !readFiniteDouble(cObj, "zCenter", 0.0, false, cp.zCenter) ||
                            !readFiniteDouble(cObj, "widthB", 0.1, false, cp.widthB) ||
                            !readFiniteDouble(cObj, "heightH", 0.1, false, cp.heightH) ||
                            !readFiniteDouble(cObj, "modularRatioN", 1.0, false, cp.modularRatioN))
                            return false;
                        sec.compositeParts.push_back(cp);
                    }
                }
                loaded.addCrossSection(sec);
            }
        }

        // 3. Nós
        std::unordered_set<NodeId> nodeIds;
        if (rootObj.contains("nodes"))
        {
            if (!rootObj["nodes"].isArray())
                return false;
            for (const auto &val : rootObj["nodes"].toArray())
            {
                if (!val.isObject())
                    return false;
                const QJsonObject nObj = val.toObject();
                Node n;
                if (!readValidInt(nObj, "id", 0, true, n.id) || n.id <= 0 || !nodeIds.insert(n.id).second)
                    return false;
                if (!readFiniteDouble(nObj, "x", 0.0, true, n.position.x) ||
                    !readFiniteDouble(nObj, "y", 0.0, true, n.position.y) ||
                    !readFiniteDouble(nObj, "z", 0.0, true, n.position.z))
                    return false;

                if (nObj.contains("support"))
                {
                    if (!nObj["support"].isObject())
                        return false;
                    const QJsonObject supObj = nObj["support"].toObject();
                    const QJsonArray fixArr = supObj["fixed"].toArray();
                    const QJsonArray sprArr = supObj["springs"].toArray();
                    const QJsonArray prsArr = supObj["prescribed"].toArray();
                    const QJsonArray hasPrsArr = supObj["hasPrescribed"].toArray();

                    for (int k = 0; k < 6; ++k)
                    {
                        if (k < fixArr.size())
                            n.support.fixed[k] = fixArr[k].toBool();
                        if (k < sprArr.size())
                        {
                            if (!sprArr[k].isDouble() || !std::isfinite(sprArr[k].toDouble()) || sprArr[k].toDouble() < 0.0)
                                return false;
                            n.support.springStiffness[k] = sprArr[k].toDouble();
                        }
                        if (k < prsArr.size())
                        {
                            if (!prsArr[k].isDouble() || !std::isfinite(prsArr[k].toDouble()))
                                return false;
                            n.support.prescribedValue[k] = prsArr[k].toDouble();
                        }
                        if (k < hasPrsArr.size())
                            n.support.hasPrescribed[k] = hasPrsArr[k].toBool();
                    }
                }

                if (nObj.contains("load"))
                {
                    if (!nObj["load"].isObject())
                        return false;
                    const QJsonObject ldObj = nObj["load"].toObject();
                    if (!readFiniteDouble(ldObj, "fx", 0.0, false, n.load.force.x) ||
                        !readFiniteDouble(ldObj, "fy", 0.0, false, n.load.force.y) ||
                        !readFiniteDouble(ldObj, "fz", 0.0, false, n.load.force.z) ||
                        !readFiniteDouble(ldObj, "mx", 0.0, false, n.load.moment.x) ||
                        !readFiniteDouble(ldObj, "my", 0.0, false, n.load.moment.y) ||
                        !readFiniteDouble(ldObj, "mz", 0.0, false, n.load.moment.z))
                        return false;
                }

                loaded.addNode(n);
            }
        }

        // 4. Elementos
        if (rootObj.contains("elements"))
        {
            if (!rootObj["elements"].isArray())
                return false;
            std::unordered_set<ElementId> elemIds;
            for (const auto &val : rootObj["elements"].toArray())
            {
                if (!val.isObject())
                    return false;
                const QJsonObject eObj = val.toObject();
                Element e;
                if (!readValidInt(eObj, "id", 0, true, e.id) || e.id <= 0 || !elemIds.insert(e.id).second ||
                    !readValidInt(eObj, "nodeStart", 0, true, e.nodeStart) ||
                    !readValidInt(eObj, "nodeEnd", 0, true, e.nodeEnd) ||
                    !readValidInt(eObj, "materialId", 1, false, e.materialId) ||
                    !readValidInt(eObj, "sectionId", 1, false, e.sectionId) ||
                    !readFiniteDouble(eObj, "rollAngle", 0.0, false, e.rollAngle))
                    return false;
                int formInt = 0;
                if (!readValidInt(eObj, "formulation", 0, false, formInt) || formInt < 0 || formInt > 2)
                    return false;
                e.formulation = static_cast<ElementFormulation>(formInt);

                if (eObj.contains("releases"))
                {
                    if (!eObj["releases"].isObject())
                        return false;
                    const QJsonObject relObj = eObj["releases"].toObject();
                    const QJsonArray sArr = relObj["start"].toArray();
                    const QJsonArray eArr = relObj["end"].toArray();
                    for (int k = 0; k < 6; ++k)
                    {
                        if (k < sArr.size())
                            e.releases.start[k] = sArr[k].toBool();
                        if (k < eArr.size())
                            e.releases.end[k] = eArr[k].toBool();
                    }
                }

                if (eObj.contains("distLoad"))
                {
                    if (!eObj["distLoad"].isObject())
                        return false;
                    const QJsonObject qObj = eObj["distLoad"].toObject();
                    e.distLoad.active = qObj["active"].toBool();
                    int csInt = 0;
                    if (!readValidInt(qObj, "coordSystem", 0, false, csInt) || csInt < 0 || csInt > 1)
                        return false;
                    e.distLoad.coordSystem = static_cast<CoordSystem>(csInt);
                    if (!readFiniteDouble(qObj, "qx", 0.0, false, e.distLoad.w.x) ||
                        !readFiniteDouble(qObj, "qy", 0.0, false, e.distLoad.w.y) ||
                        !readFiniteDouble(qObj, "qz", 0.0, false, e.distLoad.w.z))
                        return false;
                }

                if (eObj.contains("thermalLoad"))
                {
                    if (!eObj["thermalLoad"].isObject())
                        return false;
                    const QJsonObject thObj = eObj["thermalLoad"].toObject();
                    e.thermalLoad.active = thObj["active"].toBool();
                    if (!readFiniteDouble(thObj, "deltaT", 0.0, false, e.thermalLoad.deltaT))
                        return false;
                }

                loaded.addElement(e);
            }
        }

        // 5. Casos de Carga (Schema 2.0.0)
        if (rootObj.contains("loadCases"))
        {
            if (!rootObj["loadCases"].isArray())
                return false;
            std::unordered_set<kernel::LoadCaseId> lcIds;
            for (const auto &val : rootObj["loadCases"].toArray())
            {
                if (!val.isObject())
                    return false;
                const QJsonObject lcObj = val.toObject();
                kernel::LoadCase lc;
                if (!readValidInt(lcObj, "id", 0, true, lc.id) || lc.id <= 0 || !lcIds.insert(lc.id).second)
                    return false;
                lc.name = lcObj["name"].toString().toStdString();
                int catInt = 0;
                if (!readValidInt(lcObj, "category", 0, false, catInt) || catInt < 0 || catInt > 8)
                    return false;
                lc.category = static_cast<kernel::LoadCaseCategory>(catInt);

                if (lcObj.contains("selfWeight") && lcObj["selfWeight"].isObject())
                {
                    const QJsonObject swObj = lcObj["selfWeight"].toObject();
                    lc.selfWeight.enabled = swObj["enabled"].toBool(false);
                    double dx = 0.0, dy = 0.0, dz = -1.0;
                    if (!readFiniteDouble(swObj, "factor", 1.0, false, lc.selfWeight.factor) ||
                        !readFiniteDouble(swObj, "dx", 0.0, false, dx) ||
                        !readFiniteDouble(swObj, "dy", 0.0, false, dy) ||
                        !readFiniteDouble(swObj, "dz", -1.0, false, dz))
                        return false;
                    lc.selfWeight.direction = {dx, dy, dz};
                }

                if (lcObj.contains("nodalLoads") && lcObj["nodalLoads"].isArray())
                {
                    for (const auto &nv : lcObj["nodalLoads"].toArray())
                    {
                        if (!nv.isObject()) return false;
                        const QJsonObject nObj = nv.toObject();
                        kernel::NodalLoadPatternEntry ne;
                        if (!readValidInt(nObj, "nodeId", 0, true, ne.nodeId) ||
                            !readFiniteDouble(nObj, "fx", 0.0, false, ne.forceMoment[0]) ||
                            !readFiniteDouble(nObj, "fy", 0.0, false, ne.forceMoment[1]) ||
                            !readFiniteDouble(nObj, "fz", 0.0, false, ne.forceMoment[2]) ||
                            !readFiniteDouble(nObj, "mx", 0.0, false, ne.forceMoment[3]) ||
                            !readFiniteDouble(nObj, "my", 0.0, false, ne.forceMoment[4]) ||
                            !readFiniteDouble(nObj, "mz", 0.0, false, ne.forceMoment[5]))
                            return false;
                        lc.nodalLoads.push_back(ne);
                    }
                }

                if (lcObj.contains("elementLoads") && lcObj["elementLoads"].isArray())
                {
                    for (const auto &ev : lcObj["elementLoads"].toArray())
                    {
                        if (!ev.isObject()) return false;
                        const QJsonObject eObj = ev.toObject();
                        kernel::ElementLoadPatternEntry ee;
                        if (!readValidInt(eObj, "elementId", 0, true, ee.elementId))
                            return false;
                        ee.load.hasDistributed = eObj["hasDistributed"].toBool(false);
                        if (!readValidInt(eObj, "coordSystem", 0, false, ee.load.coordSystem) ||
                            ee.load.coordSystem < 0 || ee.load.coordSystem > 1)
                            return false;
                        double qx = 0.0, qy = 0.0, qz = 0.0;
                        if (!readFiniteDouble(eObj, "qx", 0.0, false, qx) ||
                            !readFiniteDouble(eObj, "qy", 0.0, false, qy) ||
                            !readFiniteDouble(eObj, "qz", 0.0, false, qz))
                            return false;
                        ee.load.distributedForce = {qx, qy, qz};
                        ee.load.hasThermal = eObj["hasThermal"].toBool(false);
                        if (!readFiniteDouble(eObj, "deltaT", 0.0, false, ee.load.deltaT) ||
                            !readFiniteDouble(eObj, "normalPressure", 0.0, false, ee.load.normalPressure))
                            return false;
                        lc.elementLoads.push_back(ee);
                    }
                }
                loaded.addLoadCase(lc);
            }
        }

        // 6. Combinações
        if (rootObj.contains("loadCombinations"))
        {
            if (!rootObj["loadCombinations"].isArray())
                return false;
            std::unordered_set<kernel::LoadComboId> combIds;
            for (const auto &val : rootObj["loadCombinations"].toArray())
            {
                if (!val.isObject()) return false;
                const QJsonObject cObj = val.toObject();
                kernel::LoadCombination comb;
                if (!readValidInt(cObj, "id", 0, true, comb.id) || comb.id <= 0 || !combIds.insert(comb.id).second)
                    return false;
                comb.name = cObj["name"].toString().toStdString();
                int lsInt = 0;
                if (!readValidInt(cObj, "limitState", 0, false, lsInt) || lsInt < 0 || lsInt > 6)
                    return false;
                comb.limitState = static_cast<kernel::LimitStateType>(lsInt);
                comb.pDeltaEnabled = cObj["pDeltaEnabled"].toBool(false);
                if (cObj.contains("factors") && cObj["factors"].isArray())
                {
                    for (const auto &fv : cObj["factors"].toArray())
                    {
                        if (!fv.isObject()) return false;
                        const QJsonObject fObj = fv.toObject();
                        kernel::LoadCombinationFactor lcf;
                        if (!readValidInt(fObj, "loadCaseId", 0, true, lcf.loadCaseId) ||
                            !readFiniteDouble(fObj, "gamma", 1.0, false, lcf.gamma))
                            return false;
                        comb.factors.push_back(lcf);
                    }
                }
                loaded.addLoadCombination(comb);
            }
        }

        // 7. Históricos temporais
        if (rootObj.contains("loadHistories"))
        {
            if (!rootObj["loadHistories"].isArray()) return false;
            for (const auto &val : rootObj["loadHistories"].toArray())
            {
                if (!val.isObject()) return false;
                const QJsonObject hObj = val.toObject();
                kernel::LoadHistory h;
                if (!readValidInt(hObj, "id", 1, false, h.id) ||
                    !readValidInt(hObj, "loadCaseId", 1, false, h.loadCaseId))
                    return false;
                h.name = hObj["name"].toString().toStdString();
                if (hObj.contains("points") && hObj["points"].isArray())
                {
                    for (const auto &pv : hObj["points"].toArray())
                    {
                        if (!pv.isObject()) return false;
                        const QJsonObject pObj = pv.toObject();
                        kernel::LoadHistoryPoint pt;
                        if (!readFiniteDouble(pObj, "t", 0.0, true, pt.timeOrStep) ||
                            !readFiniteDouble(pObj, "f", 0.0, true, pt.factor))
                            return false;
                        h.points.push_back(pt);
                    }
                }
                loaded.getLoadHistories().push_back(std::move(h));
            }
        }

        // 8. Ligações Rígidas e Diafragmas
        if (rootObj.contains("rigidLinks"))
        {
            if (!rootObj["rigidLinks"].isArray()) return false;
            for (const auto &val : rootObj["rigidLinks"].toArray())
            {
                if (!val.isObject()) return false;
                const QJsonObject o = val.toObject();
                RigidLinkSpec rl;
                if (!readValidInt(o, "masterNode", 0, true, rl.masterNode) ||
                    !readValidInt(o, "slaveNode", 0, true, rl.slaveNode))
                    return false;
                rl.coupleRotations = o["coupleRotations"].toBool(true);
                loaded.getRigidLinks().push_back(rl);
            }
        }
        if (rootObj.contains("rigidDiaphragms"))
        {
            if (!rootObj["rigidDiaphragms"].isArray()) return false;
            for (const auto &val : rootObj["rigidDiaphragms"].toArray())
            {
                if (!val.isObject()) return false;
                const QJsonObject o = val.toObject();
                RigidDiaphragmSpec dia;
                if (!readValidInt(o, "masterNode", 0, true, dia.masterNode) ||
                    !readValidInt(o, "normalAxis", 2, false, dia.normalAxis) ||
                    dia.normalAxis < 0 || dia.normalAxis > 2)
                    return false;
                if (o.contains("slaveNodes") && o["slaveNodes"].isArray())
                {
                    for (const auto &sv : o["slaveNodes"].toArray())
                    {
                        if (!sv.isDouble()) return false;
                        dia.slaveNodes.push_back(sv.toInt());
                    }
                }
                loaded.getRigidDiaphragms().push_back(std::move(dia));
            }
        }

        // 9. Opções de Solver
        if (rootObj.contains("solverOptions") && rootObj["solverOptions"].isObject())
        {
            const QJsonObject soObj = rootObj["solverOptions"].toObject();
            int tInt = 0, oInt = 0, pInt = 0;
            if (!readValidInt(soObj, "type", 0, false, tInt) || tInt < 0 || tInt > 5 ||
                !readValidInt(soObj, "ordering", 0, false, oInt) || oInt < 0 || oInt > 3 ||
                !readValidInt(soObj, "preconditioner", 0, false, pInt) || pInt < 0 || pInt > 4 ||
                !readValidInt(soObj, "maxIterations", 5000, false, loaded.solverOptions().maxIterations) ||
                !readFiniteDouble(soObj, "tolerance", 1e-10, false, loaded.solverOptions().tolerance) ||
                !readValidInt(soObj, "refinementSteps", 2, false, loaded.solverOptions().refinementSteps))
                return false;
            loaded.solverOptions().type = static_cast<kernel::LinearSolverType>(tInt);
            loaded.solverOptions().ordering = static_cast<kernel::SparseOrderingType>(oInt);
            loaded.solverOptions().preconditioner = static_cast<kernel::PreconditionerType>(pInt);
            loaded.solverOptions().iterativeRefinement = soObj["iterativeRefinement"].toBool(true);
        }

        // 10. Malha Superficial / Solida (Schema 2.1.0)
        if (rootObj.contains("surfaceMesh") && rootObj["surfaceMesh"].isObject())
        {
            const QJsonObject smObj = rootObj["surfaceMesh"].toObject();
            kernel::MeshContainer sm;
            if (smObj.contains("nodes") && smObj["nodes"].isArray())
            {
                for (const auto &nv : smObj["nodes"].toArray())
                {
                    if (!nv.isObject()) return false;
                    const QJsonObject nObj = nv.toObject();
                    kernel::MeshNode mn;
                    double x = 0.0, y = 0.0, z = 0.0;
                    if (!readValidInt(nObj, "id", 0, true, mn.id) ||
                        !readFiniteDouble(nObj, "x", 0.0, true, x) ||
                        !readFiniteDouble(nObj, "y", 0.0, true, y) ||
                        !readFiniteDouble(nObj, "z", 0.0, true, z))
                        return false;
                    mn.position = kernel::Vec3(x, y, z);
                    mn.isBoundary = nObj["isBoundary"].toBool(false);
                    mn.tag = nObj["tag"].toString().toStdString();
                    sm.nodes.push_back(std::move(mn));
                }
            }
            if (smObj.contains("elements") && smObj["elements"].isArray())
            {
                for (const auto &ev : smObj["elements"].toArray())
                {
                    if (!ev.isObject()) return false;
                    const QJsonObject eObj = ev.toObject();
                    kernel::MeshElement me;
                    int tInt = 2;
                    if (!readValidInt(eObj, "id", 0, true, me.id) ||
                        !readValidInt(eObj, "type", 2, false, tInt) || tInt < 0 || tInt > 7)
                        return false;
                    me.type = static_cast<kernel::MeshCellType>(tInt);
                    me.groupTag = eObj["groupTag"].toString().toStdString();
                    if (eObj.contains("nodeIds") && eObj["nodeIds"].isArray())
                    {
                        for (const auto &nidVal : eObj["nodeIds"].toArray())
                        {
                            if (!nidVal.isDouble()) return false;
                            me.nodeIds.push_back(nidVal.toInt());
                        }
                    }
                    sm.elements.push_back(std::move(me));
                }
            }
            loaded.surfaceMesh() = std::move(sm);
        }

        loaded.clearUndoHistory();
        loaded.pushUndo();
        model = std::move(loaded);
        return true;
    }

    bool ProjectSerializer::exportTechnicalReportHtml(const QString &filePath, const StructuralModel &model, const SolverResult &result)
    {
        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return false;
        }

        QTextStream out(&file);

        const bool isEn = (QCoreApplication::translate("InterfaceGrafica", "pt_BR_LANG_TAG") == QStringLiteral("en_US_LANG_TAG"));
        out << "<!DOCTYPE html>\n<html lang=\"" << (isEn ? "en-US" : "pt-BR") << "\">\n<head>\n"
            << "<meta charset=\"UTF-8\">\n"
            << "<title>" << trUi("Relatório Técnico de Análise Estrutural 3D - OmniFrame") << "</title>\n"
            << "<style>\n"
            << "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 30px; background: #fdfdfd; color: #333; line-height: 1.6; }\n"
            << "h1, h2, h3 { color: #1a365d; border-bottom: 2px solid #2b6cb0; padding-bottom: 5px; }\n"
            << "table { border-collapse: collapse; width: 100%; margin: 15px 0 25px 0; font-size: 14px; }\n"
            << "th, td { border: 1px solid #cbd5e0; padding: 8px 12px; text-align: right; }\n"
            << "th { background-color: #2b6cb0; color: white; text-align: center; }\n"
            << "tr:nth-child(even) { background-color: #edf2f7; }\n"
            << ".metric-box { display: flex; gap: 20px; margin: 20px 0; }\n"
            << ".metric-card { background: #ebf8ff; border-left: 4px solid #3182ce; padding: 15px; border-radius: 4px; flex: 1; }\n"
            << ".metric-card b { font-size: 18px; color: #2b6cb0; }\n"
            << ".balanced { color: #276749; font-weight: bold; }\n"
            << ".unbalanced { color: #c53030; font-weight: bold; }\n"
            << "</style>\n</head>\n<body>\n";

        out << "<h1>" << trUi("Relatório de Análise Estrutural 3D - OmniFrame") << "</h1>\n";
        out << "<p>" << trUi("Gerado em:") << " " << QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss") << "</p>\n";

        // Resumo
        out << "<div class=\"metric-box\">\n"
            << "<div class=\"metric-card\">" << trUi("Total de Nós:") << " <b>" << model.getNodes().size() << "</b></div>\n"
            << "<div class=\"metric-card\">" << trUi("Total de Barras:") << " <b>" << model.getElements().size() << "</b></div>\n"
            << "<div class=\"metric-card\">" << trUi("Graus de Liberdade:") << " <b>" << (model.getNodes().size() * 6) << "</b></div>\n"
            << "<div class=\"metric-card\">" << trUi("Tempo de Solver:") << " <b>" << QString::number(result.executionTimeMs, 'f', 2) << " ms</b></div>\n"
            << "</div>\n";

        // Verificação de Equilíbrio
        out << "<h2>" << trUi("Verificação de Equilíbrio Estático Global") << "</h2>\n";
        out << "<p>" << trUi("Status:") << " " << (result.equilibrium.isBalanced ? QString("<span class=\"balanced\">%1</span>").arg(trUi("EQUILÍBRIO SATISFEITO (ΣF = 0, ΣM = 0)")) : QString("<span class=\"unbalanced\">%1</span>").arg(trUi("DESEQUILÍBRIO DETECTADO"))) << "</p>\n";
        out << "<table>\n<tr><th>" << trUi("Componente") << "</th><th>" << trUi("Cargas Aplicadas (kN ou kNm)")
            << "</th><th>" << trUi("Reações de Apoio (kN ou kNm)") << "</th><th>" << trUi("Residual (Erro)") << "</th></tr>\n";
        out << "<tr><td>Fx</td><td>" << QString::number(result.equilibrium.sumAppliedForces.x, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.sumReactions.x, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.residualForces.x, 'e', 2) << "</td></tr>\n";
        out << "<tr><td>Fy</td><td>" << QString::number(result.equilibrium.sumAppliedForces.y, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.sumReactions.y, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.residualForces.y, 'e', 2) << "</td></tr>\n";
        out << "<tr><td>Fz</td><td>" << QString::number(result.equilibrium.sumAppliedForces.z, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.sumReactions.z, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.residualForces.z, 'e', 2) << "</td></tr>\n";
        out << "<tr><td>Mx</td><td>" << QString::number(result.equilibrium.sumAppliedMoments.x, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.sumReactionMoments.x, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.residualMoments.x, 'e', 2) << "</td></tr>\n";
        out << "<tr><td>My</td><td>" << QString::number(result.equilibrium.sumAppliedMoments.y, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.sumReactionMoments.y, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.residualMoments.y, 'e', 2) << "</td></tr>\n";
        out << "<tr><td>Mz</td><td>" << QString::number(result.equilibrium.sumAppliedMoments.z, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.sumReactionMoments.z, 'f', 4) << "</td><td>" << QString::number(result.equilibrium.residualMoments.z, 'e', 2) << "</td></tr>\n";
        out << "</table>\n";

        // Tabela de Deslocamentos Nodais
        out << "<h2>" << trUi("Deslocamentos Nodais") << "</h2>\n";
        out << "<table>\n<tr><th>" << trUi("Nó") << "</th><th>uX (mm)</th><th>uY (mm)</th><th>uZ (mm)</th><th>θX (mrad)</th><th>θY (mrad)</th><th>θZ (mrad)</th></tr>\n";
        for (const auto &n : model.getNodes())
        {
            out << "<tr><td>" << n.id << "</td>"
                << "<td>" << QString::number(n.displacement[0] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(n.displacement[1] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(n.displacement[2] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(n.displacement[3] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(n.displacement[4] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(n.displacement[5] * 1000.0, 'f', 3) << "</td></tr>\n";
        }
        out << "</table>\n";

        // Tabela de Reações de Apoio
        out << "<h2>" << trUi("Reações de Apoio") << "</h2>\n";
        out << "<table>\n<tr><th>" << trUi("Nó") << "</th><th>Rx (kN)</th><th>Ry (kN)</th><th>Rz (kN)</th><th>Mx (kNm)</th><th>My (kNm)</th><th>Mz (kNm)</th></tr>\n";
        for (const auto &n : model.getNodes())
        {
            if (!n.isSupported())
                continue;
            out << "<tr><td>" << n.id << "</td>"
                << "<td>" << QString::number(n.reaction[0], 'f', 3) << "</td>"
                << "<td>" << QString::number(n.reaction[1], 'f', 3) << "</td>"
                << "<td>" << QString::number(n.reaction[2], 'f', 3) << "</td>"
                << "<td>" << QString::number(n.reaction[3], 'f', 3) << "</td>"
                << "<td>" << QString::number(n.reaction[4], 'f', 3) << "</td>"
                << "<td>" << QString::number(n.reaction[5], 'f', 3) << "</td></tr>\n";
        }
        out << "</table>\n";

        // Tabela de Esforços nas Barras
        out << "<h2>" << trUi("Extremos de Esforços Internos nas Barras") << "</h2>\n";
        out << "<table>\n<tr><th>" << trUi("Barra") << "</th><th>N min/max (kN)</th><th>Vy min/max (kN)</th><th>Vz min/max (kN)</th><th>Tx min/max (kNm)</th><th>My min/max (kNm)</th><th>Mz min/max (kNm)</th><th>" << trUi("Flecha Máx (mm)") << "</th></tr>\n";
        for (const auto &ext : result.elementExtremes)
        {
            out << "<tr><td>" << ext.elementId << "</td>"
                << "<td>[" << QString::number(ext.N_min, 'f', 2) << "; " << QString::number(ext.N_max, 'f', 2) << "]</td>"
                << "<td>[" << QString::number(ext.Vy_min, 'f', 2) << "; " << QString::number(ext.Vy_max, 'f', 2) << "]</td>"
                << "<td>[" << QString::number(ext.Vz_min, 'f', 2) << "; " << QString::number(ext.Vz_max, 'f', 2) << "]</td>"
                << "<td>[" << QString::number(ext.Tx_min, 'f', 2) << "; " << QString::number(ext.Tx_max, 'f', 2) << "]</td>"
                << "<td>[" << QString::number(ext.My_min, 'f', 2) << "; " << QString::number(ext.My_max, 'f', 2) << "]</td>"
                << "<td>[" << QString::number(ext.Mz_min, 'f', 2) << "; " << QString::number(ext.Mz_max, 'f', 2) << "]</td>"
                << "<td>" << QString::number(ext.maxDeflection * 1000.0, 'f', 2) << "</td></tr>\n";
        }
        out << "</table>\n";

        out << "</body>\n</html>\n";
        out.flush();
        if (out.status() != QTextStream::Ok)
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

    bool ProjectSerializer::exportTechnicalReportMarkdown(const QString &filePath, const StructuralModel &model, const SolverResult &result)
    {
        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return false;
        }

        QTextStream out(&file);

        out << "# " << trUi("Relatório de Análise Estrutural 3D - OmniFrame") << "\n\n";
        out << "**" << trUi("Data:") << "** " << QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss") << "\n\n";
        out << "- **" << trUi("Total de Nós:") << "** " << model.getNodes().size() << "\n";
        out << "- **" << trUi("Total de Barras:") << "** " << model.getElements().size() << "\n";
        out << "- **" << trUi("Graus de Liberdade:") << "** " << (model.getNodes().size() * 6) << "\n";
        out << "- **" << trUi("Tempo de Solução:") << "** " << QString::number(result.executionTimeMs, 'f', 2) << " ms\n\n";

        out << "## " << trUi("Verificação de Equilíbrio Estático") << "\n\n";
        out << "**" << trUi("Status:") << "** " << (result.equilibrium.isBalanced ? trUi("EQUILÍBRIO SATISFEITO (ΣF = 0, ΣM = 0)") : trUi("DESEQUILÍBRIO DETECTADO")) << "\n\n";
        out << "| " << trUi("Componente") << " | " << trUi("Cargas Aplicadas") << " | " << trUi("Reações") << " | " << trUi("Residual") << " |\n";
        out << "| :--- | :---: | :---: | :---: |\n";
        out << "| Fx (kN) | " << result.equilibrium.sumAppliedForces.x << " | " << result.equilibrium.sumReactions.x << " | " << result.equilibrium.residualForces.x << " |\n";
        out << "| Fy (kN) | " << result.equilibrium.sumAppliedForces.y << " | " << result.equilibrium.sumReactions.y << " | " << result.equilibrium.residualForces.y << " |\n";
        out << "| Fz (kN) | " << result.equilibrium.sumAppliedForces.z << " | " << result.equilibrium.sumReactions.z << " | " << result.equilibrium.residualForces.z << " |\n";
        out << "| Mx (kNm) | " << result.equilibrium.sumAppliedMoments.x << " | " << result.equilibrium.sumReactionMoments.x << " | " << result.equilibrium.residualMoments.x << " |\n";
        out << "| My (kNm) | " << result.equilibrium.sumAppliedMoments.y << " | " << result.equilibrium.sumReactionMoments.y << " | " << result.equilibrium.residualMoments.y << " |\n";
        out << "| Mz (kNm) | " << result.equilibrium.sumAppliedMoments.z << " | " << result.equilibrium.sumReactionMoments.z << " | " << result.equilibrium.residualMoments.z << " |\n\n";

        out << "## " << trUi("Deslocamentos Nodais") << "\n\n";
        out << "| " << trUi("Nó") << " | uX (mm) | uY (mm) | uZ (mm) | θX (mrad) | θY (mrad) | θZ (mrad) |\n";
        out << "| :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n";
        for (const auto &n : model.getNodes())
        {
            out << "| " << n.id << " | "
                << (n.displacement[0] * 1000.0) << " | "
                << (n.displacement[1] * 1000.0) << " | "
                << (n.displacement[2] * 1000.0) << " | "
                << (n.displacement[3] * 1000.0) << " | "
                << (n.displacement[4] * 1000.0) << " | "
                << (n.displacement[5] * 1000.0) << " |\n";
        }
        out << "\n";

        out << "## " << trUi("Reações de Apoio") << "\n\n";
        out << "| " << trUi("Nó") << " | Rx (kN) | Ry (kN) | Rz (kN) | Mx (kNm) | My (kNm) | Mz (kNm) |\n";
        out << "| :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n";
        for (const auto &n : model.getNodes())
        {
            if (!n.isSupported())
                continue;
            out << "| " << n.id << " | "
                << n.reaction[0] << " | "
                << n.reaction[1] << " | "
                << n.reaction[2] << " | "
                << n.reaction[3] << " | "
                << n.reaction[4] << " | "
                << n.reaction[5] << " |\n";
        }
        out << "\n";

        out << "## " << trUi("Extremos de Esforços Internos nas Barras") << "\n\n";
        out << "| " << trUi("Barra") << " | N min/max (kN) | Vy min/max (kN) | Vz min/max (kN) | Tx min/max (kNm) | My min/max (kNm) | Mz min/max (kNm) | " << trUi("Flecha Máx (mm)") << " |\n";
        out << "| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n";
        for (const auto &ext : result.elementExtremes)
        {
            out << "| " << ext.elementId
                << " | [" << QString::number(ext.N_min, 'f', 2) << "; " << QString::number(ext.N_max, 'f', 2) << "]"
                << " | [" << QString::number(ext.Vy_min, 'f', 2) << "; " << QString::number(ext.Vy_max, 'f', 2) << "]"
                << " | [" << QString::number(ext.Vz_min, 'f', 2) << "; " << QString::number(ext.Vz_max, 'f', 2) << "]"
                << " | [" << QString::number(ext.Tx_min, 'f', 2) << "; " << QString::number(ext.Tx_max, 'f', 2) << "]"
                << " | [" << QString::number(ext.My_min, 'f', 2) << "; " << QString::number(ext.My_max, 'f', 2) << "]"
                << " | [" << QString::number(ext.Mz_min, 'f', 2) << "; " << QString::number(ext.Mz_max, 'f', 2) << "]"
                << " | " << QString::number(ext.maxDeflection * 1000.0, 'f', 2) << " |\n";
        }
        out << "\n";
        out.flush();
        if (out.status() != QTextStream::Ok)
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

} // namespace omniframe
