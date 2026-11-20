#include "io/IfcInterop.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0 || size > 262144u) {
        return 0;
    }

    static QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        return 0;
    }

    const std::string inputPath = QDir(tmpDir.path()).filePath(QStringLiteral("fuzz_input.ifc")).toStdString();
    {
        QFile f(QString::fromStdString(inputPath));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return 0;
        }
        f.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    }

    omniframe::StructuralModel model;
    omniframe::kernel::MeshContainer mesh;
    omniframe::io::IfcGuidMap guidMap;
    omniframe::io::IfcInteropReport rep;
    (void)omniframe::io::IfcInterop::importFromFile(inputPath, model, &mesh, &guidMap, &rep);
    return 0;
}
