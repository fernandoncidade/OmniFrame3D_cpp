#include "io/ProjectSerializer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0 || size > 262144u) {
        return 0;
    }

    static int argc = 1;
    static char arg0[] = "fuzz_project_serializer";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
    static QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        return 0;
    }

    const QString inputPath = QDir(tmpDir.path()).filePath(QStringLiteral("fuzz_input.omniframe"));
    {
        QFile f(inputPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return 0;
        }
        f.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    }

    omniframe::StructuralModel model;
    (void)omniframe::ProjectSerializer::loadFromFile(inputPath, model);
    return 0;
}
