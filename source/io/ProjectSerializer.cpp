#include "ProjectSerializer.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QDateTime>
#include <iomanip>
#include <sstream>

namespace omniframe
{

    namespace
    {
        inline QString trUi(const char *text)
        {
            return QCoreApplication::translate("InterfaceGrafica", text);
        }
    } // namespace

    bool ProjectSerializer::saveToFile(const QString &filePath, const StructuralModel &model)
    {
        QJsonObject rootObj;
        rootObj["version"] = "1.0.0";
        rootObj["application"] = "OmniFrame 3D";
        rootObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

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
            sObj["A"] = sec.A;
            sObj["Iy"] = sec.Iy;
            sObj["Iz"] = sec.Iz;
            sObj["J"] = sec.J;
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

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }

        QJsonDocument doc(rootObj);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }

    bool ProjectSerializer::loadFromFile(const QString &filePath, StructuralModel &model)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }

        QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }

        QJsonObject rootObj = doc.object();
        model.clear();

        // 1. Materiais
        if (rootObj.contains("materials") && rootObj["materials"].isArray())
        {
            model.getMaterials().clear();
            for (const auto &val : rootObj["materials"].toArray())
            {
                QJsonObject mObj = val.toObject();
                Material mat;
                mat.id = mObj["id"].toInt();
                mat.name = mObj["name"].toString().toStdString();
                mat.E = mObj["E"].toDouble(2.0e8);
                mat.nu = mObj["nu"].toDouble(0.3);
                mat.alpha = mObj["alpha"].toDouble(1.2e-5);
                mat.gamma = mObj["gamma"].toDouble(78.5);
                model.addMaterial(mat);
            }
        }

        // 2. Seções
        if (rootObj.contains("sections") && rootObj["sections"].isArray())
        {
            model.getCrossSections().clear();
            for (const auto &val : rootObj["sections"].toArray())
            {
                QJsonObject sObj = val.toObject();
                CrossSection sec;
                sec.id = sObj["id"].toInt();
                sec.name = sObj["name"].toString().toStdString();
                sec.shape = static_cast<SectionShape>(sObj["shape"].toInt());
                sec.dim1 = sObj["dim1"].toDouble();
                sec.dim2 = sObj["dim2"].toDouble();
                sec.dim3 = sObj["dim3"].toDouble();
                sec.dim4 = sObj["dim4"].toDouble();
                sec.A = sObj["A"].toDouble();
                sec.Iy = sObj["Iy"].toDouble();
                sec.Iz = sObj["Iz"].toDouble();
                sec.J = sObj["J"].toDouble();
                model.addCrossSection(sec);
            }
        }

        // 3. Nós
        if (rootObj.contains("nodes") && rootObj["nodes"].isArray())
        {
            for (const auto &val : rootObj["nodes"].toArray())
            {
                QJsonObject nObj = val.toObject();
                Node n;
                n.id = nObj["id"].toInt();
                n.position.x = nObj["x"].toDouble();
                n.position.y = nObj["y"].toDouble();
                n.position.z = nObj["z"].toDouble();

                if (nObj.contains("support") && nObj["support"].isObject())
                {
                    QJsonObject supObj = nObj["support"].toObject();
                    QJsonArray fixArr = supObj["fixed"].toArray();
                    QJsonArray sprArr = supObj["springs"].toArray();
                    QJsonArray prsArr = supObj["prescribed"].toArray();
                    QJsonArray hasPrsArr = supObj["hasPrescribed"].toArray();

                    for (int k = 0; k < 6; ++k)
                    {
                        if (k < fixArr.size())
                            n.support.fixed[k] = fixArr[k].toBool();
                        if (k < sprArr.size())
                            n.support.springStiffness[k] = sprArr[k].toDouble();
                        if (k < prsArr.size())
                            n.support.prescribedValue[k] = prsArr[k].toDouble();
                        if (k < hasPrsArr.size())
                            n.support.hasPrescribed[k] = hasPrsArr[k].toBool();
                    }
                }

                if (nObj.contains("load") && nObj["load"].isObject())
                {
                    QJsonObject ldObj = nObj["load"].toObject();
                    n.load.force.x = ldObj["fx"].toDouble();
                    n.load.force.y = ldObj["fy"].toDouble();
                    n.load.force.z = ldObj["fz"].toDouble();
                    n.load.moment.x = ldObj["mx"].toDouble();
                    n.load.moment.y = ldObj["my"].toDouble();
                    n.load.moment.z = ldObj["mz"].toDouble();
                }

                model.addNode(n);
            }
        }

        // 4. Elementos
        if (rootObj.contains("elements") && rootObj["elements"].isArray())
        {
            for (const auto &val : rootObj["elements"].toArray())
            {
                QJsonObject eObj = val.toObject();
                Element e;
                e.id = eObj["id"].toInt();
                e.nodeStart = eObj["nodeStart"].toInt();
                e.nodeEnd = eObj["nodeEnd"].toInt();
                e.materialId = eObj["materialId"].toInt(1);
                e.sectionId = eObj["sectionId"].toInt(1);
                e.rollAngle = eObj["rollAngle"].toDouble(0.0);

                if (eObj.contains("releases") && eObj["releases"].isObject())
                {
                    QJsonObject relObj = eObj["releases"].toObject();
                    QJsonArray sArr = relObj["start"].toArray();
                    QJsonArray eArr = relObj["end"].toArray();
                    for (int k = 0; k < 6; ++k)
                    {
                        if (k < sArr.size())
                            e.releases.start[k] = sArr[k].toBool();
                        if (k < eArr.size())
                            e.releases.end[k] = eArr[k].toBool();
                    }
                }

                if (eObj.contains("distLoad") && eObj["distLoad"].isObject())
                {
                    QJsonObject qObj = eObj["distLoad"].toObject();
                    e.distLoad.active = qObj["active"].toBool();
                    e.distLoad.coordSystem = static_cast<CoordSystem>(qObj["coordSystem"].toInt());
                    e.distLoad.w.x = qObj["qx"].toDouble();
                    e.distLoad.w.y = qObj["qy"].toDouble();
                    e.distLoad.w.z = qObj["qz"].toDouble();
                }

                if (eObj.contains("thermalLoad") && eObj["thermalLoad"].isObject())
                {
                    QJsonObject thObj = eObj["thermalLoad"].toObject();
                    e.thermalLoad.active = thObj["active"].toBool();
                    e.thermalLoad.deltaT = thObj["deltaT"].toDouble();
                }

                model.addElement(e);
            }
        }

        model.clearUndoHistory();
        model.pushUndo();
        return true;
    }

    bool ProjectSerializer::exportTechnicalReportHtml(const QString &filePath, const StructuralModel &model, const SolverResult &result)
    {
        QFile file(filePath);
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
        file.close();
        return true;
    }

    bool ProjectSerializer::exportTechnicalReportMarkdown(const QString &filePath, const StructuralModel &model, const SolverResult &result)
    {
        QFile file(filePath);
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

        file.close();
        return true;
    }

} // namespace omniframe
