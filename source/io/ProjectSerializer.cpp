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

        inline QString solverTypeDisplayName(kernel::LinearSolverType t)
        {
            switch (t)
            {
                case kernel::LinearSolverType::Auto: return QStringLiteral("Auto (LDLT/LU)");
                case kernel::LinearSolverType::SimplicialLDLT: return QStringLiteral("SimplicialLDLT (Cholesky)");
                case kernel::LinearSolverType::SimplicialLLT: return QStringLiteral("SimplicialLLT (Cholesky SPD)");
                case kernel::LinearSolverType::SparseLU: return QStringLiteral("SparseLU (COLAMD)");
                case kernel::LinearSolverType::ConjugateGradient: return QStringLiteral("ConjugateGradient (PCG)");
                case kernel::LinearSolverType::BiCGSTAB: return QStringLiteral("BiCGSTAB (ILUT)");
            }
            return QStringLiteral("Auto");
        }

        std::vector<const kernel::MeshNode *> collectUniqueMeshNodes(const StructuralModel &model)
        {
            std::vector<const kernel::MeshNode *> unique;
            const auto &nodes = model.getNodes();
            const auto &mNodes = model.surfaceMesh().nodes;
            unique.reserve(mNodes.size());
            for (const auto &mn : mNodes)
            {
                bool coincides = false;
                for (const auto &fn : nodes)
                {
                    const double dx = fn.x() - mn.position.x();
                    const double dy = fn.y() - mn.position.y();
                    const double dz = fn.z() - mn.position.z();
                    if ((dx * dx + dy * dy + dz * dz) <= 1e-12)
                    {
                        coincides = true;
                        break;
                    }
                }
                if (!coincides)
                {
                    unique.push_back(&mn);
                }
            }
            return unique;
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

        // 10. Malha Superficial / Solida (Schema 2.1.0+)
        const auto &sm = model.surfaceMesh();
        if (!sm.nodes.empty() || !sm.elements.empty())
        {
            QJsonObject smObj;
            auto toBoolArr6 = [](const std::array<bool, 6> &arr) {
                QJsonArray out;
                for (bool v : arr) out.append(v);
                return out;
            };
            auto toDoubleArr = [](const double *ptr, int count) {
                QJsonArray out;
                for (int i = 0; i < count; ++i) out.append(ptr[i]);
                return out;
            };

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
                if (mn.hasFixedDofs())
                    nObj["fixed"] = toBoolArr6(mn.fixed);
                if (mn.hasSprings())
                    nObj["springStiffness"] = toDoubleArr(mn.springStiffness.data(), 6);
                if (mn.hasPrescribedDisplacements())
                    nObj["prescribedDisplacement"] = toDoubleArr(mn.prescribedDisplacement.data(), 6);
                if (mn.nodalForce.squaredNorm() > 1e-24)
                    nObj["nodalForce"] = toDoubleArr(mn.nodalForce.data(), 3);
                if (mn.nodalMoment.squaredNorm() > 1e-24)
                    nObj["nodalMoment"] = toDoubleArr(mn.nodalMoment.data(), 3);
                if (mn.hasResults)
                {
                    nObj["hasResults"] = true;
                    nObj["displacement"] = toDoubleArr(mn.displacement.data(), 6);
                    nObj["reaction"] = toDoubleArr(mn.reaction.data(), 6);
                }
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
                eObj["materialId"] = me.materialId;
                eObj["sectionId"] = me.sectionId;
                eObj["thickness"] = me.thickness;
                eObj["normalPressure"] = me.normalPressure;
                if (me.distributedLoad.squaredNorm() > 1e-24)
                    eObj["distributedLoad"] = toDoubleArr(me.distributedLoad.data(), 3);
                if (std::abs(me.deltaT) > 1e-12)
                    eObj["deltaT"] = me.deltaT;
                if (me.includeSelfWeight)
                    eObj["includeSelfWeight"] = true;
                if (me.hasResults)
                {
                    eObj["hasResults"] = true;
                    eObj["vonMisesStress"] = me.vonMisesStress;
                    eObj["maxPrincipalStress"] = me.maxPrincipalStress;
                    eObj["minPrincipalStress"] = me.minPrincipalStress;
                    eObj["membraneResultants"] = toDoubleArr(me.membraneResultants.data(), 3);
                    eObj["bendingResultants"] = toDoubleArr(me.bendingResultants.data(), 3);
                    eObj["transverseShear"] = toDoubleArr(me.transverseShear.data(), 2);
                    eObj["stressVoigt"] = toDoubleArr(me.stressVoigt.data(), 6);
                }
                smElems.append(eObj);
            }
            smObj["elements"] = smElems;

            if (!sm.nodeSets.empty())
            {
                QJsonObject nsObj;
                for (const auto &[k, vec] : sm.nodeSets)
                {
                    QJsonArray arr;
                    for (int id : vec) arr.append(id);
                    nsObj[QString::fromStdString(k)] = arr;
                }
                smObj["nodeSets"] = nsObj;
            }
            if (!sm.elementGroups.empty())
            {
                QJsonObject egObj;
                for (const auto &[k, vec] : sm.elementGroups)
                {
                    QJsonArray arr;
                    for (int id : vec) arr.append(id);
                    egObj[QString::fromStdString(k)] = arr;
                }
                smObj["elementGroups"] = egObj;
            }
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

        // 10. Malha Superficial / Solida (Schema 2.1.0+, retrocompativel)
        if (rootObj.contains("surfaceMesh") && rootObj["surfaceMesh"].isObject())
        {
            const QJsonObject smObj = rootObj["surfaceMesh"].toObject();
            kernel::MeshContainer sm;
            auto readFixedDoubleArr = [](const QJsonObject &obj, const char *key, double *dst, int count) -> bool {
                if (!obj.contains(key)) return true;
                if (!obj[key].isArray()) return false;
                const QJsonArray arr = obj[key].toArray();
                const int n = std::min<int>(count, static_cast<int>(arr.size()));
                for (int i = 0; i < n; ++i)
                {
                    if (!arr[i].isDouble()) return false;
                    const double v = arr[i].toDouble();
                    if (!std::isfinite(v)) return false;
                    dst[i] = v;
                }
                return true;
            };

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
                    if (nObj.contains("fixed"))
                    {
                        if (!nObj["fixed"].isArray()) return false;
                        const QJsonArray fa = nObj["fixed"].toArray();
                        for (int d = 0; d < std::min<int>(6, static_cast<int>(fa.size())); ++d)
                            mn.fixed[static_cast<std::size_t>(d)] = fa[d].toBool(false);
                    }
                    if (!readFixedDoubleArr(nObj, "springStiffness", mn.springStiffness.data(), 6) ||
                        !readFixedDoubleArr(nObj, "prescribedDisplacement", mn.prescribedDisplacement.data(), 6) ||
                        !readFixedDoubleArr(nObj, "nodalForce", mn.nodalForce.data(), 3) ||
                        !readFixedDoubleArr(nObj, "nodalMoment", mn.nodalMoment.data(), 3))
                        return false;
                    mn.hasResults = nObj["hasResults"].toBool(false);
                    if (mn.hasResults)
                    {
                        if (!readFixedDoubleArr(nObj, "displacement", mn.displacement.data(), 6) ||
                            !readFixedDoubleArr(nObj, "reaction", mn.reaction.data(), 6))
                            return false;
                    }
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
                    if (!readValidInt(eObj, "materialId", 1, false, me.materialId) ||
                        !readValidInt(eObj, "sectionId", 0, false, me.sectionId) ||
                        !readFiniteDouble(eObj, "thickness", 0.15, false, me.thickness) ||
                        !readFiniteDouble(eObj, "normalPressure", 0.0, false, me.normalPressure) ||
                        !readFiniteDouble(eObj, "deltaT", 0.0, false, me.deltaT) ||
                        !readFixedDoubleArr(eObj, "distributedLoad", me.distributedLoad.data(), 3))
                        return false;
                    me.includeSelfWeight = eObj["includeSelfWeight"].toBool(false);
                    me.hasResults = eObj["hasResults"].toBool(false);
                    if (me.hasResults)
                    {
                        if (!readFiniteDouble(eObj, "vonMisesStress", 0.0, false, me.vonMisesStress) ||
                            !readFiniteDouble(eObj, "maxPrincipalStress", 0.0, false, me.maxPrincipalStress) ||
                            !readFiniteDouble(eObj, "minPrincipalStress", 0.0, false, me.minPrincipalStress) ||
                            !readFixedDoubleArr(eObj, "membraneResultants", me.membraneResultants.data(), 3) ||
                            !readFixedDoubleArr(eObj, "bendingResultants", me.bendingResultants.data(), 3) ||
                            !readFixedDoubleArr(eObj, "transverseShear", me.transverseShear.data(), 2) ||
                            !readFixedDoubleArr(eObj, "stressVoigt", me.stressVoigt.data(), 6))
                            return false;
                    }
                    sm.elements.push_back(std::move(me));
                }
            }
            if (smObj.contains("nodeSets") && smObj["nodeSets"].isObject())
            {
                const QJsonObject nsObj = smObj["nodeSets"].toObject();
                for (auto it = nsObj.constBegin(); it != nsObj.constEnd(); ++it)
                {
                    if (!it.value().isArray()) continue;
                    std::vector<kernel::NodeId> ids;
                    for (const auto &v : it.value().toArray()) ids.push_back(v.toInt());
                    sm.nodeSets[it.key().toStdString()] = std::move(ids);
                }
            }
            if (smObj.contains("elementGroups") && smObj["elementGroups"].isObject())
            {
                const QJsonObject egObj = smObj["elementGroups"].toObject();
                for (auto it = egObj.constBegin(); it != egObj.constEnd(); ++it)
                {
                    if (!it.value().isArray()) continue;
                    std::vector<kernel::ElementId> ids;
                    for (const auto &v : it.value().toArray()) ids.push_back(v.toInt());
                    sm.elementGroups[it.key().toStdString()] = std::move(ids);
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
        const auto &meta = model.metadata();
        const auto &so = model.solverOptions();
        const ModelValidation val = model.validate();
        const auto uniqueMeshNodes = collectUniqueMeshNodes(model);
        const QString softwareVer = QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("OmniFrame 3D Kernel C++23 / Schema 2.1.0")
            : QCoreApplication::applicationVersion();
        const QString execStamp = QDateTime::currentDateTime().toString(Qt::ISODate);
        const auto meshQuality = !model.surfaceMesh().elements.empty()
            ? kernel::MeshGenerator::evaluateQuality(model.surfaceMesh())
            : kernel::MeshQualityMetrics{};

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
            << ".metric-box { display: flex; flex-wrap: wrap; gap: 16px; margin: 20px 0; }\n"
            << ".metric-card { background: #ebf8ff; border-left: 4px solid #3182ce; padding: 15px; border-radius: 4px; flex: 1 1 180px; }\n"
            << ".metric-card b { font-size: 18px; color: #2b6cb0; }\n"
            << ".balanced { color: #276749; font-weight: bold; }\n"
            << ".unbalanced { color: #c53030; font-weight: bold; }\n"
            << ".info-list li { margin-bottom: 4px; }\n"
            << "</style>\n</head>\n<body>\n";

        out << "<h1>" << trUi("Relatório de Análise Estrutural 3D - OmniFrame") << "</h1>\n";

        // Itens 1, 2, 3, 16, 20: Identificação do Projeto, Software, Modelo, Unidades e Execução
        out << "<h2>1. Identificação do Projeto, Modelo, Software e Execução</h2>\n<ul class=\"info-list\">\n"
            << "<li><b>1. Projeto:</b> " << QString::fromStdString(meta.projectName).toHtmlEscaped()
            << " | <b>Cliente:</b> " << QString::fromStdString(meta.clientName).toHtmlEscaped()
            << " | <b>Local:</b> " << QString::fromStdString(meta.siteLocation).toHtmlEscaped()
            << " | <b>Engenheiro:</b> " << QString::fromStdString(meta.engineerName).toHtmlEscaped() << "</li>\n"
            << "<li><b>2. Versão do Software:</b> " << softwareVer.toHtmlEscaped() << "</li>\n"
            << "<li><b>3. Identificação do Modelo:</b> " << model.getNodes().size() << " nós 1D, "
            << model.getElements().size() << " barras 1D, "
            << model.surfaceMesh().nodes.size() << " nós 2D/3D, "
            << model.surfaceMesh().elements.size() << " elementos 2D/3D, "
            << result.totalDofs << " DOFs (" << result.restrainedDofs << " restringidos)</li>\n"
            << "<li><b>16. Unidades Adotadas:</b> SI Estrutural [m, kN, kNm, kPa, kg/m³, s, rad | exibidos mm, mrad]</li>\n"
            << "<li><b>20. Identificação da Execução:</b> Timestamp ISO-8601 " << execStamp << " | Modo Offline Eigen3/C++23 | Tempo Solver: "
            << QString::number(result.executionTimeMs, 'f', 2) << " ms</li>\n"
            << "</ul>\n";

        out << "<div class=\"metric-box\">\n"
            << "<div class=\"metric-card\">" << trUi("Total de Nós:") << " <b>" << (model.getNodes().size() + uniqueMeshNodes.size()) << "</b></div>\n"
            << "<div class=\"metric-card\">" << trUi("Total de Barras:") << " <b>" << model.getElements().size() << "</b></div>\n"
            << "<div class=\"metric-card\">Elementos 2D/3D: <b>" << model.surfaceMesh().elements.size() << "</b></div>\n"
            << "<div class=\"metric-card\">" << trUi("Graus de Liberdade:") << " <b>" << result.totalDofs << "</b></div>\n"
            << "<div class=\"metric-card\">" << trUi("Tempo de Solver:") << " <b>" << QString::number(result.executionTimeMs, 'f', 2) << " ms</b></div>\n"
            << "</div>\n";

        // Itens 4, 12, 13, 14, 19: Hipóteses, Método, Solver, Convergência, Limitações
        out << "<h2>2. Hipóteses, Método de Análise, Parâmetros do Solver e Limitações</h2>\n<ul class=\"info-list\">\n"
            << "<li><b>4. Hipóteses e Simplificações:</b> Regime elástico-linear, pequenas deformações e deslocamentos; barras 1D (Euler-Bernoulli/Timoshenko 3D); cascas 2D (Mindlin/Reissner/DKT com massa/rigidez consistente); sólidos 3D (isoparamétrico linear/quadrático com estado triaxial).</li>\n"
            << "<li><b>7. Elementos Utilizados:</b> Barras 1D (" << model.getElements().size() << ") e Malha Contínua 2D/3D (" << model.surfaceMesh().elements.size() << ").</li>\n"
            << "<li><b>12. Método de Análise:</b> Estática Linear Global MEF 3D Unificada (sistema K_ff · u_f = rhs_f com condensação de MPCs e molas).</li>\n"
            << "<li><b>13. Parâmetros do Solver:</b> Solicitado: " << solverTypeDisplayName(so.type)
            << " | Utilizado: " << solverTypeDisplayName(result.solverStats.solverUsed)
            << " | Fallback permitido: " << (so.allowFallback ? "Sim" : "Não")
            << " (Fallback ativado: " << (QString::fromStdString(result.solverStats.message).contains(QStringLiteral("Fallback"), Qt::CaseInsensitive) ? "Sim" : "Não") << ")"
            << " | Tol. Iterativa: " << QString::number(so.tolerance, 'e', 2)
            << " | Máx. Iterações: " << so.maxIterations << "</li>\n"
            << "<li><b>14. Critérios de Convergência:</b> Resíduo Relativo = " << QString::number(result.solverStats.relativeResidual, 'e', 3)
            << " | Iterações = " << result.solverStats.iterations
            << " | Energia de Deformação Total = " << QString::number(result.totalStrainEnergy, 'f', 4) << " kNm</li>\n"
            << "<li><b>19. Limitações Conhecidas:</b> A análise estática linear não modela plastificação J2, flambagem nem efeitos de 2ª ordem geométrica (utilize a Suíte Não Linear/Flambagem quando aplicável). Normas fora da biblioteca local NBR têm cobertura auditada separadamente.</li>\n"
            << "</ul>\n";

        // Itens 5 e 6: Materiais e Seções
        out << "<h2>3. Materiais e Seções Transversais (Itens 5 e 6)</h2>\n";
        out << "<table><tr><th>Mat ID</th><th>Nome</th><th>E (kPa)</th><th>ν</th><th>ρ (kg/m³)</th><th>γ (kN/m³)</th><th>Modelo Constitutivo</th></tr>\n";
        for (const auto &mat : model.getMaterials())
        {
            out << "<tr><td>" << mat.id << "</td><td style=\"text-align:left\">" << QString::fromStdString(mat.name).toHtmlEscaped() << "</td>"
                << "<td>" << QString::number(mat.E, 'e', 3) << "</td>"
                << "<td>" << QString::number(mat.nu, 'f', 2) << "</td>"
                << "<td>" << QString::number(mat.rho, 'f', 1) << "</td>"
                << "<td>" << QString::number(mat.gamma, 'f', 2) << "</td>"
                << "<td>" << (mat.isPlastic ? "Von Mises J2" : "Elástico Isotrópico") << "</td></tr>\n";
        }
        out << "</table>\n";
        if (!model.getCrossSections().empty())
        {
            out << "<table><tr><th>Seção ID</th><th>Nome</th><th>A (m²)</th><th>Iy (m⁴)</th><th>Iz (m⁴)</th><th>J (m⁴)</th></tr>\n";
            for (const auto &sec : model.getCrossSections())
            {
                out << "<tr><td>" << sec.id << "</td><td style=\"text-align:left\">" << QString::fromStdString(sec.name).toHtmlEscaped() << "</td>"
                    << "<td>" << QString::number(sec.A, 'e', 3) << "</td>"
                    << "<td>" << QString::number(sec.Iy, 'e', 3) << "</td>"
                    << "<td>" << QString::number(sec.Iz, 'e', 3) << "</td>"
                    << "<td>" << QString::number(sec.J, 'e', 3) << "</td></tr>\n";
            }
            out << "</table>\n";
        }

        // Itens 8, 9, 10, 11: Malha, Contorno, Carregamentos, Combinações
        out << "<h2>4. Malha, Condições de Contorno, Carregamentos e Combinações (Itens 8 a 11)</h2>\n<ul class=\"info-list\">\n";
        if (!model.surfaceMesh().elements.empty())
        {
            out << "<li><b>8. Malha e Discretização:</b> Elementos: " << meshQuality.totalElements
                << " | Válidos: " << meshQuality.validElements
                << " | Degenerados: " << meshQuality.invertedOrDegenerateCount
                << " | J_min: " << QString::number(meshQuality.minJacobian, 'f', 4)
                << " | Aspect: [" << QString::number(meshQuality.minAspectRatio, 'f', 2)
                << "; " << QString::number(meshQuality.maxAspectRatio, 'f', 2)
                << "] | Skewness máx: " << QString::number(meshQuality.maxSkewness, 'f', 3) << "</li>\n";
        }
        else
        {
            out << "<li><b>8. Malha e Discretização:</b> Modelo exclusivamente reticulado 1D (0 elementos 2D/3D).</li>\n";
        }
        int sup1D = 0, supMesh = 0;
        for (const auto &n : model.getNodes()) if (n.isSupported()) ++sup1D;
        for (const auto *mn : uniqueMeshNodes) if (mn->isSupported()) ++supMesh;
        out << "<li><b>9. Condições de Contorno:</b> Apoios 1D = " << sup1D
            << " | Apoios Malha 2D/3D = " << supMesh
            << " | Rigid Links = " << model.getRigidLinks().size()
            << " | Diafragmas Rígidos = " << model.getRigidDiaphragms().size() << "</li>\n";
        out << "<li><b>10. Carregamentos:</b> Casos de Carga = " << model.getLoadCases().size()
            << " | Resultante Aplicada Global: Fx = " << QString::number(result.equilibrium.sumAppliedForces.x, 'f', 3)
            << " kN, Fy = " << QString::number(result.equilibrium.sumAppliedForces.y, 'f', 3)
            << " kN, Fz = " << QString::number(result.equilibrium.sumAppliedForces.z, 'f', 3) << " kN</li>\n";
        out << "<li><b>11. Combinações Cadastradas:</b> " << model.getLoadCombinations().size() << "</li>\n</ul>\n";

        // Item 17: Advertências e Validação Estruturada
        if (!val.issues.empty() || !model.serializationWarnings().empty())
        {
            out << "<h2>5. Advertências e Diagnóstico de Validação (Item 17)</h2>\n<ul>\n";
            for (const auto &iss : val.issues)
            {
                const QString sev = (iss.severity == IssueSeverity::Error) ? "ERRO"
                                  : ((iss.severity == IssueSeverity::Warning) ? "AVISO" : "INFO");
                out << "<li>[" << sev << " " << QString::fromStdString(iss.code).toHtmlEscaped() << "] "
                    << QString::fromStdString(iss.message).toHtmlEscaped() << "</li>\n";
            }
            for (const auto &sw : model.serializationWarnings())
            {
                out << "<li>[SERIALIZAÇÃO] " << QString::fromStdString(sw).toHtmlEscaped() << "</li>\n";
            }
            out << "</ul>\n";
        }

        // Item 18: Verificação de Equilíbrio
        out << "<h2>" << trUi("Verificação de Equilíbrio Estático Global") << " (Item 18)</h2>\n";
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

        // Item 15: Deslocamentos Nodais (1D + Malha 2D/3D)
        out << "<h2>" << trUi("Deslocamentos Nodais") << " (Item 15)</h2>\n";
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
        for (const auto *mn : uniqueMeshNodes)
        {
            const QString idLbl = model.getNodes().empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
            out << "<tr><td>" << idLbl << "</td>"
                << "<td>" << QString::number(mn->displacement[0] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->displacement[1] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->displacement[2] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->displacement[3] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->displacement[4] * 1000.0, 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->displacement[5] * 1000.0, 'f', 3) << "</td></tr>\n";
        }
        out << "</table>\n";

        // Tabela de Reações de Apoio (1D + Malha 2D/3D)
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
        for (const auto *mn : uniqueMeshNodes)
        {
            if (!mn->isSupported())
                continue;
            const QString idLbl = model.getNodes().empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
            out << "<tr><td>" << idLbl << "</td>"
                << "<td>" << QString::number(mn->reaction[0], 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->reaction[1], 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->reaction[2], 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->reaction[3], 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->reaction[4], 'f', 3) << "</td>"
                << "<td>" << QString::number(mn->reaction[5], 'f', 3) << "</td></tr>\n";
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

        // Tabela de Tensões e Esforços 2D/3D (Casca / Sólido)
        if (!result.continuumElementResults.empty())
        {
            out << "<h2>Esforços e Tensões no Contínuo 2D/3D (Casca / Sólido)</h2>\n";
            out << "<p>Pico von Mises Casca: <b>" << QString::number(result.maxShellVonMises, 'f', 2)
                << " kPa</b> | Pico von Mises Sólido: <b>" << QString::number(result.maxSolidVonMises, 'f', 2) << " kPa</b></p>\n";
            out << "<table>\n<tr><th>Elemento</th><th>Tipo</th><th>σ_vM (kPa)</th><th>σ_1 (kPa)</th><th>σ_3 (kPa)</th><th>Nxx / σxx</th><th>Nyy / σyy</th><th>Mxx / τxz</th></tr>\n";
            for (const auto &ce : result.continuumElementResults)
            {
                const QString c5 = ce.isShell ? (QString::number(ce.membraneResultants[0], 'f', 2) + " kN/m")
                                              : (QString::number(ce.stressVoigt[0], 'f', 2) + " kPa");
                const QString c6 = ce.isShell ? (QString::number(ce.membraneResultants[1], 'f', 2) + " kN/m")
                                              : (QString::number(ce.stressVoigt[1], 'f', 2) + " kPa");
                const QString c7 = ce.isShell ? (QString::number(ce.bendingResultants[0], 'f', 3) + " kNm/m")
                                              : (QString::number(ce.stressVoigt[4], 'f', 2) + " kPa");
                out << "<tr><td>" << ce.elementId << "</td><td>" << QString::fromStdString(ce.cellTypeName).toHtmlEscaped() << "</td>"
                    << "<td>" << QString::number(ce.vonMisesStress, 'f', 2) << "</td>"
                    << "<td>" << QString::number(ce.maxPrincipalStress, 'f', 2) << "</td>"
                    << "<td>" << QString::number(ce.minPrincipalStress, 'f', 2) << "</td>"
                    << "<td>" << c5 << "</td><td>" << c6 << "</td><td>" << c7 << "</td></tr>\n";
            }
            out << "</table>\n";
        }

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
        const auto &meta = model.metadata();
        const auto &so = model.solverOptions();
        const ModelValidation val = model.validate();
        const auto uniqueMeshNodes = collectUniqueMeshNodes(model);
        const QString softwareVer = QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("OmniFrame 3D Kernel C++23 / Schema 2.1.0")
            : QCoreApplication::applicationVersion();
        const QString execStamp = QDateTime::currentDateTime().toString(Qt::ISODate);
        const auto meshQuality = !model.surfaceMesh().elements.empty()
            ? kernel::MeshGenerator::evaluateQuality(model.surfaceMesh())
            : kernel::MeshQualityMetrics{};

        out << "# " << trUi("Relatório de Análise Estrutural 3D - OmniFrame") << "\n\n";
        out << "## 1. Identificação do Projeto, Software, Modelo, Unidades e Execução (Itens 1, 2, 3, 16, 20)\n\n";
        out << "- **1. Projeto:** " << QString::fromStdString(meta.projectName)
            << " | **Cliente:** " << QString::fromStdString(meta.clientName)
            << " | **Local:** " << QString::fromStdString(meta.siteLocation)
            << " | **Engenheiro:** " << QString::fromStdString(meta.engineerName) << "\n";
        out << "- **2. Versão do Software:** " << softwareVer << "\n";
        out << "- **3. Identificação do Modelo:** " << model.getNodes().size() << " nós 1D, "
            << model.getElements().size() << " barras 1D, "
            << model.surfaceMesh().nodes.size() << " nós 2D/3D, "
            << model.surfaceMesh().elements.size() << " elementos 2D/3D, "
            << result.totalDofs << " DOFs (" << result.restrainedDofs << " restringidos)\n";
        out << "- **16. Unidades Adotadas:** SI Estrutural [m, kN, kNm, kPa, kg/m³, s, rad | exibidos mm, mrad]\n";
        out << "- **20. Identificação da Execução:** Timestamp ISO-8601 `" << execStamp << "` | Tempo de solver: " << QString::number(result.executionTimeMs, 'f', 2) << " ms\n\n";

        out << "## 2. Hipóteses, Método, Solver, Convergência e Limitações (Itens 4, 7, 12, 13, 14, 19)\n\n";
        out << "- **4. Hipóteses e Simplificações:** Elasticidade linear, pequenas deformações; 1D Euler-Bernoulli/Timoshenko 3D; 2D Mindlin/Reissner/DKT; 3D Tetra/Hexa isoparamétrico.\n";
        out << "- **7. Elementos Utilizados:** " << model.getElements().size() << " barras 1D e " << model.surfaceMesh().elements.size() << " elementos 2D/3D.\n";
        out << "- **12. Método de Análise:** Estática Linear Global MEF 3D Unificada.\n";
        out << "- **13. Parâmetros do Solver:** Solicitado `" << solverTypeDisplayName(so.type) << "` | Utilizado `"
            << solverTypeDisplayName(result.solverStats.solverUsed) << "` | Fallback permitido: " << (so.allowFallback ? "Sim" : "Não")
            << " (Fallback ativado: " << (QString::fromStdString(result.solverStats.message).contains(QStringLiteral("Fallback"), Qt::CaseInsensitive) ? "Sim" : "Não") << ") | Tol: "
            << QString::number(so.tolerance, 'e', 2) << " | Máx. Iter: " << so.maxIterations << "\n";
        out << "- **14. Critérios de Convergência:** Resíduo Relativo = " << QString::number(result.solverStats.relativeResidual, 'e', 3)
            << " | Iterações = " << result.solverStats.iterations
            << " | Energia de Deformação = " << QString::number(result.totalStrainEnergy, 'f', 4) << " kNm\n";
        out << "- **19. Limitações Conhecidas:** Análise estática linear não considera plasticidade J2 nem efeitos geométricos de 2ª ordem.\n\n";

        out << "## 3. Materiais, Seções, Malha, Contorno, Cargas e Combinações (Itens 5, 6, 8, 9, 10, 11)\n\n";
        out << "| Mat ID | Nome | E (kPa) | ν | ρ (kg/m³) | γ (kN/m³) |\n| :---: | :--- | :---: | :---: | :---: | :---: |\n";
        for (const auto &mat : model.getMaterials())
        {
            out << "| " << mat.id << " | " << QString::fromStdString(mat.name) << " | " << QString::number(mat.E, 'e', 3)
                << " | " << mat.nu << " | " << mat.rho << " | " << mat.gamma << " |\n";
        }
        out << "\n";
        if (!model.getCrossSections().empty())
        {
            out << "| Seção ID | Nome | A (m²) | Iy (m⁴) | Iz (m⁴) | J (m⁴) |\n| :---: | :--- | :---: | :---: | :---: | :---: |\n";
            for (const auto &sec : model.getCrossSections())
            {
                out << "| " << sec.id << " | " << QString::fromStdString(sec.name) << " | "
                    << QString::number(sec.A, 'e', 3) << " | " << QString::number(sec.Iy, 'e', 3)
                    << " | " << QString::number(sec.Iz, 'e', 3) << " | " << QString::number(sec.J, 'e', 3) << " |\n";
            }
            out << "\n";
        }
        if (!model.surfaceMesh().elements.empty())
        {
            out << "- **8. Malha 2D/3D:** " << meshQuality.totalElements << " elem. (Válidos: " << meshQuality.validElements
                << ", Degenerados: " << meshQuality.invertedOrDegenerateCount << ", J_min: " << QString::number(meshQuality.minJacobian, 'f', 4)
                << ", Aspect: [" << QString::number(meshQuality.minAspectRatio, 'f', 2) << "; " << QString::number(meshQuality.maxAspectRatio, 'f', 2)
                << "], Skewness máx: " << QString::number(meshQuality.maxSkewness, 'f', 3) << ")\n";
        }
        else
        {
            out << "- **8. Malha 2D/3D:** Sem elementos contínuos.\n";
        }
        int sup1D = 0, supMesh = 0;
        for (const auto &n : model.getNodes()) if (n.isSupported()) ++sup1D;
        for (const auto *mn : uniqueMeshNodes) if (mn->isSupported()) ++supMesh;
        out << "- **9. Condições de Contorno:** Apoios 1D = " << sup1D << " | Apoios Malha 2D/3D = " << supMesh
            << " | Links = " << model.getRigidLinks().size() << " | Diafragmas = " << model.getRigidDiaphragms().size() << "\n";
        out << "- **10. Carregamentos:** " << model.getLoadCases().size() << " casos de carga.\n";
        out << "- **11. Combinações:** " << model.getLoadCombinations().size() << " combinações.\n\n";

        if (!val.issues.empty() || !model.serializationWarnings().empty())
        {
            out << "## 4. Advertências e Diagnóstico (Item 17)\n\n";
            for (const auto &iss : val.issues)
            {
                const char *sev = (iss.severity == IssueSeverity::Error) ? "ERRO"
                                : ((iss.severity == IssueSeverity::Warning) ? "AVISO" : "INFO");
                out << "- **[" << sev << " " << QString::fromStdString(iss.code) << "]** " << QString::fromStdString(iss.message) << "\n";
            }
            for (const auto &sw : model.serializationWarnings())
            {
                out << "- **[SERIALIZAÇÃO]** " << QString::fromStdString(sw) << "\n";
            }
            out << "\n";
        }

        out << "## " << trUi("Verificação de Equilíbrio Estático") << " (Item 18)\n\n";
        out << "**" << trUi("Status:") << "** " << (result.equilibrium.isBalanced ? trUi("EQUILÍBRIO SATISFEITO (ΣF = 0, ΣM = 0)") : trUi("DESEQUILÍBRIO DETECTADO")) << "\n\n";
        out << "| " << trUi("Componente") << " | " << trUi("Cargas Aplicadas") << " | " << trUi("Reações") << " | " << trUi("Residual") << " |\n";
        out << "| :--- | :---: | :---: | :---: |\n";
        out << "| Fx (kN) | " << result.equilibrium.sumAppliedForces.x << " | " << result.equilibrium.sumReactions.x << " | " << result.equilibrium.residualForces.x << " |\n";
        out << "| Fy (kN) | " << result.equilibrium.sumAppliedForces.y << " | " << result.equilibrium.sumReactions.y << " | " << result.equilibrium.residualForces.y << " |\n";
        out << "| Fz (kN) | " << result.equilibrium.sumAppliedForces.z << " | " << result.equilibrium.sumReactions.z << " | " << result.equilibrium.residualForces.z << " |\n";
        out << "| Mx (kNm) | " << result.equilibrium.sumAppliedMoments.x << " | " << result.equilibrium.sumReactionMoments.x << " | " << result.equilibrium.residualMoments.x << " |\n";
        out << "| My (kNm) | " << result.equilibrium.sumAppliedMoments.y << " | " << result.equilibrium.sumReactionMoments.y << " | " << result.equilibrium.residualMoments.y << " |\n";
        out << "| Mz (kNm) | " << result.equilibrium.sumAppliedMoments.z << " | " << result.equilibrium.sumReactionMoments.z << " | " << result.equilibrium.residualMoments.z << " |\n\n";

        out << "## " << trUi("Deslocamentos Nodais") << " (Item 15)\n\n";
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
        for (const auto *mn : uniqueMeshNodes)
        {
            const QString idLbl = model.getNodes().empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
            out << "| " << idLbl << " | "
                << (mn->displacement[0] * 1000.0) << " | "
                << (mn->displacement[1] * 1000.0) << " | "
                << (mn->displacement[2] * 1000.0) << " | "
                << (mn->displacement[3] * 1000.0) << " | "
                << (mn->displacement[4] * 1000.0) << " | "
                << (mn->displacement[5] * 1000.0) << " |\n";
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
        for (const auto *mn : uniqueMeshNodes)
        {
            if (!mn->isSupported())
                continue;
            const QString idLbl = model.getNodes().empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
            out << "| " << idLbl << " | "
                << mn->reaction[0] << " | "
                << mn->reaction[1] << " | "
                << mn->reaction[2] << " | "
                << mn->reaction[3] << " | "
                << mn->reaction[4] << " | "
                << mn->reaction[5] << " |\n";
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

        if (!result.continuumElementResults.empty())
        {
            out << "## Esforços e Tensões no Contínuo 2D/3D (Casca / Sólido)\n\n";
            out << "- **Pico von Mises Casca:** " << QString::number(result.maxShellVonMises, 'f', 2) << " kPa\n";
            out << "- **Pico von Mises Sólido:** " << QString::number(result.maxSolidVonMises, 'f', 2) << " kPa\n\n";
            out << "| Elemento | Tipo | σ_vM (kPa) | σ_1 (kPa) | σ_3 (kPa) | Nxx / σxx | Nyy / σyy | Mxx / τxz |\n";
            out << "| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n";
            for (const auto &ce : result.continuumElementResults)
            {
                const QString c5 = ce.isShell ? (QString::number(ce.membraneResultants[0], 'f', 2) + " kN/m")
                                              : (QString::number(ce.stressVoigt[0], 'f', 2) + " kPa");
                const QString c6 = ce.isShell ? (QString::number(ce.membraneResultants[1], 'f', 2) + " kN/m")
                                              : (QString::number(ce.stressVoigt[1], 'f', 2) + " kPa");
                const QString c7 = ce.isShell ? (QString::number(ce.bendingResultants[0], 'f', 3) + " kNm/m")
                                              : (QString::number(ce.stressVoigt[4], 'f', 2) + " kPa");
                out << "| " << ce.elementId << " | " << QString::fromStdString(ce.cellTypeName)
                    << " | " << QString::number(ce.vonMisesStress, 'f', 2)
                    << " | " << QString::number(ce.maxPrincipalStress, 'f', 2)
                    << " | " << QString::number(ce.minPrincipalStress, 'f', 2)
                    << " | " << c5 << " | " << c6 << " | " << c7 << " |\n";
            }
            out << "\n";
        }

        out.flush();
        if (out.status() != QTextStream::Ok)
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

} // namespace omniframe
