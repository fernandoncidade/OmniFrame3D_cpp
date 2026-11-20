#include "WindowsCliIntegration.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace omniframe::os {

namespace {

#ifdef _WIN32

QString normalizeWinDirPath(const QString& rawPath) {
    QString cleaned = QDir::toNativeSeparators(QDir::cleanPath(rawPath.trimmed()));
    while (cleaned.size() > 3 && cleaned.endsWith(QLatin1Char('\\'))) {
        cleaned.chop(1);
    }
    return cleaned;
}

QString expandWindowsEnvVars(const QString& input) {
    if (!input.contains(QLatin1Char('%'))) {
        return input;
    }
    const std::wstring wInput = input.toStdWString();
    const DWORD needed = ExpandEnvironmentStringsW(wInput.c_str(), nullptr, 0);
    if (needed == 0) {
        return input;
    }
    std::wstring buf(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(wInput.c_str(), buf.data(), needed);
    if (written == 0) {
        return input;
    }
    if (!buf.empty() && buf.back() == L'\0') {
        buf.pop_back();
    }
    return QString::fromStdWString(buf);
}

bool readRegistryString(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName, QString& outValue) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(rootKey, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD byteCount = 0;
    LONG rc = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &byteCount);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || byteCount < sizeof(wchar_t)) {
        RegCloseKey(hKey);
        if (rc == ERROR_SUCCESS && byteCount == 0) {
            outValue.clear();
            return true;
        }
        return false;
    }
    std::wstring buf(byteCount / sizeof(wchar_t) + 1, L'\0');
    rc = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &byteCount);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        return false;
    }
    const size_t nullPos = buf.find(L'\0');
    if (nullPos != std::wstring::npos) {
        buf.resize(nullPos);
    }
    outValue = QString::fromStdWString(buf);
    return true;
}

bool writeRegistryString(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName, const QString& value, DWORD regType = REG_SZ) {
    HKEY hKey = nullptr;
    const LONG openRc = RegCreateKeyExW(rootKey, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_READ, nullptr, &hKey, nullptr);
    if (openRc != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring wVal = value.toStdWString();
    const DWORD bytes = static_cast<DWORD>((wVal.size() + 1) * sizeof(wchar_t));
    const LONG setRc = RegSetValueExW(hKey, valueName, 0, regType, reinterpret_cast<const BYTE*>(wVal.c_str()), bytes);
    RegCloseKey(hKey);
    return setRc == ERROR_SUCCESS;
}

bool pathListContainsDir(const QString& pathList, const QString& targetDir) {
    const QString normTarget = normalizeWinDirPath(expandWindowsEnvVars(targetDir));
    const QStringList entries = pathList.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        if (normalizeWinDirPath(expandWindowsEnvVars(entry)).compare(normTarget, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void broadcastEnvironmentChange() {
    SendMessageTimeoutW(
        HWND_BROADCAST,
        WM_SETTINGCHANGE,
        0,
        reinterpret_cast<LPARAM>(L"Environment"),
        SMTO_ABORTIFHUNG,
        2500,
        nullptr);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

QString quoteWindowsArgument(const QString& arg) {
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    bool needsQuotes = false;
    for (QChar ch : arg) {
        if (ch.isSpace() || ch == QLatin1Char('"')) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return arg;
    }
    QString quoted;
    quoted.reserve(arg.size() + 4);
    quoted.append(QLatin1Char('"'));
    int backslashes = 0;
    for (QChar ch : arg) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
        } else if (ch == QLatin1Char('"')) {
            quoted.append(QString(backslashes * 2 + 1, QLatin1Char('\\')));
            quoted.append(QLatin1Char('"'));
            backslashes = 0;
        } else {
            if (backslashes > 0) {
                quoted.append(QString(backslashes, QLatin1Char('\\')));
                backslashes = 0;
            }
            quoted.append(ch);
        }
    }
    if (backslashes > 0) {
        quoted.append(QString(backslashes * 2, QLatin1Char('\\')));
    }
    quoted.append(QLatin1Char('"'));
    return quoted;
}

#endif // _WIN32

bool isCliHeadlessFlag(const QString& arg) {
    static const QStringList kCliFlags = {
        QStringLiteral("--cli"),
        QStringLiteral("--help"),
        QStringLiteral("-h"),
        QStringLiteral("--version"),
        QStringLiteral("-v"),
        QStringLiteral("--validate"),
        QStringLiteral("--analyze"),
        QStringLiteral("--list-examples"),
        QStringLiteral("--example"),
        QStringLiteral("--nbr-audit"),
        QStringLiteral("--audit-nbr"),
        QStringLiteral("--install-windows-integration"),
        QStringLiteral("--uninstall-windows-integration"),
        QStringLiteral("--check-windows-integration"),
        QStringLiteral("--analysis"),
        QStringLiteral("--solver"),
        QStringLiteral("--modes"),
        QStringLiteral("--steps"),
        QStringLiteral("--dt"),
        QStringLiteral("--case"),
        QStringLiteral("--combo"),
        QStringLiteral("--method"),
        QStringLiteral("--scheme"),
        QStringLiteral("--imperfection"),
        QStringLiteral("--imperfection-mode"),
        QStringLiteral("--full-tangent"),
        QStringLiteral("--modified-newton"),
        QStringLiteral("--line-search"),
        QStringLiteral("--no-line-search"),
        QStringLiteral("--load-factor"),
        QStringLiteral("--export"),
        QStringLiteral("--report"),
        QStringLiteral("--output"),
        QStringLiteral("--json")
    };
    return kCliFlags.contains(arg, Qt::CaseInsensitive);
}

bool isQtStandardOptionWithArg(const QString& arg) {
    static const QStringList kQtOptsWithVal = {
        QStringLiteral("-platform"),
        QStringLiteral("-platformpluginpath"),
        QStringLiteral("-platformtheme"),
        QStringLiteral("-plugin"),
        QStringLiteral("-style"),
        QStringLiteral("-stylesheet"),
        QStringLiteral("-qwindowgeometry"),
        QStringLiteral("-qwindowtitle"),
        QStringLiteral("-qwindowicon"),
        QStringLiteral("-display")
    };
    return kQtOptsWithVal.contains(arg, Qt::CaseInsensitive);
}

} // namespace

void configureConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

std::vector<std::string> commandLineArgumentsUtf8(int argc, char* argv[]) {
    std::vector<std::string> out;
#ifdef _WIN32
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv != nullptr && wargc > 0) {
        out.reserve(static_cast<size_t>(wargc));
        for (int i = 0; i < wargc; ++i) {
            out.push_back(QString::fromWCharArray(wargv[i]).toUtf8().toStdString());
        }
        LocalFree(wargv);
        return out;
    }
#endif
    out.reserve(static_cast<size_t>(std::max(0, argc)));
    for (int i = 0; i < argc; ++i) {
        out.emplace_back(argv[i] ? argv[i] : "");
    }
    return out;
}

QString currentExecutableFilePath() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return {};
        }
        if (len < buf.size() - 1) {
            buf.resize(len);
            return QDir::cleanPath(QString::fromStdWString(buf));
        }
        buf.resize(buf.size() * 2);
    }
#else
    return {};
#endif
}

QString findSiblingBinary(const QString& binaryFileName, const QString& referenceExePath) {
    const QString refExe = referenceExePath.trimmed().isEmpty()
        ? currentExecutableFilePath()
        : referenceExePath.trimmed();
    if (refExe.isEmpty()) {
        return {};
    }
    const QFileInfo refInfo(refExe);
    const QDir dir = refInfo.isDir() ? QDir(refInfo.absoluteFilePath()) : refInfo.absoluteDir();
    return QDir::cleanPath(dir.filePath(binaryFileName));
}

CliIntegrationStatus queryWindowsCliIntegration(const QString& referenceExePath) {
    CliIntegrationStatus status;
    const QString currentExe = referenceExePath.trimmed().isEmpty()
        ? currentExecutableFilePath()
        : QDir::cleanPath(referenceExePath.trimmed());
    status.currentExePath = currentExe;

    const QFileInfo exeInfo(currentExe);
    const QDir dir = exeInfo.isDir() ? QDir(exeInfo.absoluteFilePath()) : exeInfo.absoluteDir();
    status.installDirectory = QDir::toNativeSeparators(dir.absolutePath());
    status.cliBinaryPath = QDir::toNativeSeparators(dir.filePath(QStringLiteral("omniframe-cli.exe")));
    status.guiBinaryPath = QDir::toNativeSeparators(dir.filePath(QStringLiteral("OmniFrame.exe")));
    status.cliBinaryExists = QFileInfo::exists(status.cliBinaryPath);
    status.guiBinaryExists = QFileInfo::exists(status.guiBinaryPath);

#ifdef _WIN32
    status.isWindows = true;
    const QString normDir = normalizeWinDirPath(status.installDirectory);

    QString hkcuPath;
    if (readRegistryString(HKEY_CURRENT_USER, L"Environment", L"Path", hkcuPath)) {
        status.installDirInUserPath = pathListContainsDir(hkcuPath, normDir);
    }
    QString hklmPath;
    if (readRegistryString(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
            L"Path",
            hklmPath)) {
        status.installDirInSystemPath = pathListContainsDir(hklmPath, normDir);
    }
    const QString procPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    status.installDirInProcessPath = pathListContainsDir(procPath, normDir);

    auto checkAppPaths = [](const wchar_t* subKey, const QString& expectedExe) -> bool {
        QString val;
        const QString normExpected = normalizeWinDirPath(expectedExe);
        if (readRegistryString(HKEY_CURRENT_USER, subKey, nullptr, val) &&
            normalizeWinDirPath(val).compare(normExpected, Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (readRegistryString(HKEY_LOCAL_MACHINE, subKey, nullptr, val) &&
            normalizeWinDirPath(val).compare(normExpected, Qt::CaseInsensitive) == 0) {
            return true;
        }
        return false;
    };

    status.appPathsCliRegistered = checkAppPaths(
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe-cli.exe",
        status.cliBinaryPath);
    status.appPathsGuiRegistered = checkAppPaths(
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OmniFrame.exe",
        status.guiBinaryPath);

    QString assocProgId;
    const bool hasExt =
        readRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\.omniframe", nullptr, assocProgId) ||
        readRegistryString(HKEY_LOCAL_MACHINE, L"Software\\Classes\\.omniframe", nullptr, assocProgId);
    if (hasExt && assocProgId == QStringLiteral("OmniFrame3D.Project")) {
        QString openCmd;
        if (readRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\OmniFrame3D.Project\\shell\\open\\command", nullptr, openCmd) ||
            readRegistryString(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OmniFrame3D.Project\\shell\\open\\command", nullptr, openCmd)) {
            status.fileAssocRegistered = openCmd.contains(status.guiBinaryPath, Qt::CaseInsensitive) ||
                                         openCmd.contains(QStringLiteral("OmniFrame.exe"), Qt::CaseInsensitive);
        }
    }
#endif
    return status;
}

bool installWindowsCliIntegration(const QString& referenceExePath, QString* detailsOut) {
#ifndef _WIN32
    Q_UNUSED(referenceExePath);
    if (detailsOut) {
        *detailsOut = QStringLiteral("Windows Registry integration is available only on Windows 10/11.");
    }
    return false;
#else
    const CliIntegrationStatus st = queryWindowsCliIntegration(referenceExePath);
    if (st.installDirectory.isEmpty() || !st.cliBinaryExists) {
        if (detailsOut) {
            *detailsOut = QStringLiteral("Binário omniframe-cli.exe não encontrado no diretório: %1").arg(st.installDirectory);
        }
        return false;
    }

    const QString normDir = normalizeWinDirPath(st.installDirectory);

    // 1. Atualiza HKCU\Environment\Path sem apagar entradas existentes
    QString currentUserPath;
    readRegistryString(HKEY_CURRENT_USER, L"Environment", L"Path", currentUserPath);
    QStringList parts = currentUserPath.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QStringList cleanedParts;
    bool alreadyPresent = false;
    for (const QString& item : parts) {
        const QString trimmed = item.trimmed();
        if (trimmed.isEmpty()) continue;
        if (normalizeWinDirPath(expandWindowsEnvVars(trimmed)).compare(normDir, Qt::CaseInsensitive) == 0) {
            if (!alreadyPresent) {
                cleanedParts.push_back(normDir);
                alreadyPresent = true;
            }
        } else {
            cleanedParts.push_back(trimmed);
        }
    }
    if (!alreadyPresent) {
        cleanedParts.push_back(normDir);
    }
    const QString newUserPath = cleanedParts.join(QLatin1Char(';'));
    const bool pathOk = writeRegistryString(HKEY_CURRENT_USER, L"Environment", L"Path", newUserPath, REG_EXPAND_SZ);

    // Atualiza PATH do processo atual para efeito imediato em subshells (CMD/PowerShell)
    const QString procPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    if (!pathListContainsDir(procPath, normDir)) {
        const QString updatedProcPath = normDir + QLatin1Char(';') + procPath;
        SetEnvironmentVariableW(L"PATH", updatedProcPath.toStdWString().c_str());
    }

    // 2. App Paths para omniframe-cli.exe, OmniFrame.exe e omniframe.exe
    bool appPathsOk = true;
    appPathsOk &= writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe-cli.exe",
        nullptr,
        st.cliBinaryPath);
    appPathsOk &= writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe-cli.exe",
        L"Path",
        normDir);

    if (st.guiBinaryExists) {
        appPathsOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OmniFrame.exe",
            nullptr,
            st.guiBinaryPath);
        appPathsOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OmniFrame.exe",
            L"Path",
            normDir);
        appPathsOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe.exe",
            nullptr,
            st.guiBinaryPath);
        appPathsOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe.exe",
            L"Path",
            normDir);
    }

    // 3. Associação .omniframe -> OmniFrame.exe no Explorer / CMD / PowerShell
    bool assocOk = true;
    if (st.guiBinaryExists) {
        assocOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Classes\\.omniframe",
            nullptr,
            QStringLiteral("OmniFrame3D.Project"));
        assocOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Classes\\OmniFrame3D.Project",
            nullptr,
            QStringLiteral("Projeto Estrutural OmniFrame 3D"));
        assocOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Classes\\OmniFrame3D.Project\\DefaultIcon",
            nullptr,
            QLatin1Char('"') + st.guiBinaryPath + QStringLiteral("\",0"));
        assocOk &= writeRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Classes\\OmniFrame3D.Project\\shell\\open\\command",
            nullptr,
            QLatin1Char('"') + st.guiBinaryPath + QStringLiteral("\" \"%1\""));
    }

    broadcastEnvironmentChange();

    if (detailsOut) {
        *detailsOut = QStringLiteral("PATH=%1; AppPaths=%2; Assoc=%3; Dir=%4")
            .arg(pathOk ? QStringLiteral("OK") : QStringLiteral("FAIL"))
            .arg(appPathsOk ? QStringLiteral("OK") : QStringLiteral("FAIL"))
            .arg(assocOk ? QStringLiteral("OK") : QStringLiteral("FAIL"))
            .arg(normDir);
    }
    return pathOk && appPathsOk;
#endif
}

bool uninstallWindowsCliIntegration(const QString& referenceExePath, QString* detailsOut) {
#ifndef _WIN32
    Q_UNUSED(referenceExePath);
    if (detailsOut) {
        *detailsOut = QStringLiteral("Windows Registry integration is available only on Windows 10/11.");
    }
    return false;
#else
    const CliIntegrationStatus st = queryWindowsCliIntegration(referenceExePath);
    const QString normDir = normalizeWinDirPath(st.installDirectory);

    QString currentUserPath;
    if (readRegistryString(HKEY_CURRENT_USER, L"Environment", L"Path", currentUserPath)) {
        const QStringList parts = currentUserPath.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QStringList kept;
        for (const QString& item : parts) {
            const QString trimmed = item.trimmed();
            if (trimmed.isEmpty()) continue;
            if (normalizeWinDirPath(expandWindowsEnvVars(trimmed)).compare(normDir, Qt::CaseInsensitive) != 0) {
                kept.push_back(trimmed);
            }
        }
        writeRegistryString(HKEY_CURRENT_USER, L"Environment", L"Path", kept.join(QLatin1Char(';')), REG_EXPAND_SZ);
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe-cli.exe");

    QString regGuiPath;
    if (readRegistryString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OmniFrame.exe", L"Path", regGuiPath) &&
        normalizeWinDirPath(regGuiPath).compare(normDir, Qt::CaseInsensitive) == 0) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OmniFrame.exe");
    }
    if (readRegistryString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe.exe", L"Path", regGuiPath) &&
        normalizeWinDirPath(regGuiPath).compare(normDir, Qt::CaseInsensitive) == 0) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\omniframe.exe");
    }

    QString assocProgId;
    if (readRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\.omniframe", nullptr, assocProgId) &&
        assocProgId == QStringLiteral("OmniFrame3D.Project")) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.omniframe");
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\OmniFrame3D.Project");
    }

    broadcastEnvironmentChange();

    if (detailsOut) {
        *detailsOut = QStringLiteral("Removed=%1").arg(normDir);
    }
    return true;
#endif
}

bool openInteractiveCliTerminal(bool usePowerShell,
                                const QString& workingDir,
                                const QString& referenceExePath,
                                QString* errorOut) {
#ifndef _WIN32
    Q_UNUSED(usePowerShell);
    Q_UNUSED(workingDir);
    Q_UNUSED(referenceExePath);
    if (errorOut) {
        *errorOut = QStringLiteral("Terminal integration helper is Windows-only.");
    }
    return false;
#else
    const CliIntegrationStatus st = queryWindowsCliIntegration(referenceExePath);
    const QString binDir = normalizeWinDirPath(st.installDirectory);
    if (binDir.isEmpty() || !st.cliBinaryExists) {
        if (errorOut) {
            *errorOut = QStringLiteral("Executável omniframe-cli.exe não encontrado em: %1").arg(binDir);
        }
        return false;
    }

    // Garante que o diretório esteja no PATH do bloco de ambiente herdado pelo terminal filho
    const QString existingPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    if (!pathListContainsDir(existingPath, binDir)) {
        const QString combinedPath = binDir + QLatin1Char(';') + existingPath;
        SetEnvironmentVariableW(L"PATH", combinedPath.toStdWString().c_str());
    }

    const QString workDir = workingDir.trimmed().isEmpty()
        ? binDir
        : QDir::toNativeSeparators(QDir::cleanPath(workingDir));

    QString appName;
    QString cmdLine;
    if (usePowerShell) {
        appName = QStringLiteral("powershell.exe");
        const QString psScript =
            QStringLiteral("[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ") +
            QStringLiteral("[Console]::InputEncoding=[System.Text.Encoding]::UTF8; ") +
            QStringLiteral("$OutputEncoding=[System.Text.Encoding]::UTF8; ") +
            QStringLiteral("omniframe-cli --version; ") +
            QStringLiteral("Write-Host 'OmniFrame 3D CLI (PowerShell) - Execute: omniframe-cli --help' -ForegroundColor Cyan");
        cmdLine = QStringLiteral("powershell.exe -NoLogo -NoExit -Command %1").arg(quoteWindowsArgument(psScript));
    } else {
        appName = QStringLiteral("cmd.exe");
        cmdLine = QStringLiteral("cmd.exe /K \"chcp 65001 >nul && title OmniFrame 3D CLI (CMD) && omniframe-cli --version && echo Execute: omniframe-cli --help\"");
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring wCmdLine = cmdLine.toStdWString();
    const std::wstring wWorkDir = workDir.toStdWString();

    const BOOL ok = CreateProcessW(
        nullptr,
        wCmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE,
        nullptr,
        wWorkDir.c_str(),
        &si,
        &pi);

    if (!ok) {
        if (errorOut) {
            *errorOut = QStringLiteral("CreateProcessW falhou (%1) ao iniciar %2.")
                .arg(static_cast<unsigned long>(GetLastError()))
                .arg(appName);
        }
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#endif
}

int forwardGuiArgsToCliIfRequested(int argc, char* argv[], QStringList& outFilesToOpen) {
    outFilesToOpen.clear();
    const auto utf8Args = commandLineArgumentsUtf8(argc, argv);
    if (utf8Args.size() <= 1) {
        return -1;
    }

    bool shouldDelegateCli = false;
    QStringList forwardedArgs;
    forwardedArgs.reserve(static_cast<qsizetype>(utf8Args.size()));

    for (size_t i = 1; i < utf8Args.size(); ++i) {
        const QString arg = QString::fromStdString(utf8Args[i]);
        if (isQtStandardOptionWithArg(arg)) {
            if (i + 1 < utf8Args.size()) {
                ++i;
            }
            continue;
        }
        if (arg.startsWith(QStringLiteral("-platform")) || arg.startsWith(QStringLiteral("-style"))) {
            continue;
        }
        if (isCliHeadlessFlag(arg)) {
            shouldDelegateCli = true;
            if (arg.compare(QStringLiteral("--cli"), Qt::CaseInsensitive) != 0) {
                forwardedArgs.push_back(arg);
            }
            continue;
        }
        forwardedArgs.push_back(arg);
        if (!arg.startsWith(QLatin1Char('-'))) {
            const QString lower = arg.toLower();
            if (lower.endsWith(QStringLiteral(".omniframe")) ||
                lower.endsWith(QStringLiteral(".dxf")) ||
                lower.endsWith(QStringLiteral(".ifc")) ||
                QFileInfo::exists(arg)) {
                outFilesToOpen.push_back(arg);
            }
        }
    }

    if (!shouldDelegateCli) {
        return -1;
    }

#ifdef _WIN32
    const QString cliExe = findSiblingBinary(QStringLiteral("omniframe-cli.exe"));
#else
    const QString cliExe = findSiblingBinary(QStringLiteral("omniframe-cli"));
#endif
    if (cliExe.isEmpty() || !QFileInfo::exists(cliExe)) {
#ifdef _WIN32
        AttachConsole(ATTACH_PARENT_PROCESS);
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stderr);
#endif
        std::cerr << "Erro: binario headless omniframe-cli.exe nao encontrado ao lado de OmniFrame.exe.\n";
        return 1;
    }

#ifdef _WIN32
    const HANDLE hOutBefore = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE hErrBefore = GetStdHandle(STD_ERROR_HANDLE);
    const DWORD outType = (hOutBefore != nullptr && hOutBefore != INVALID_HANDLE_VALUE)
        ? GetFileType(hOutBefore)
        : FILE_TYPE_UNKNOWN;
    const DWORD errType = (hErrBefore != nullptr && hErrBefore != INVALID_HANDLE_VALUE)
        ? GetFileType(hErrBefore)
        : FILE_TYPE_UNKNOWN;

    bool attachedParent = false;
    if (outType == FILE_TYPE_UNKNOWN || errType == FILE_TYPE_UNKNOWN) {
        attachedParent = (AttachConsole(ATTACH_PARENT_PROCESS) != FALSE);
    }
    configureConsoleUtf8();

    QString fullCmd = quoteWindowsArgument(QDir::toNativeSeparators(cliExe));
    for (const QString& fArg : forwardedArgs) {
        fullCmd.append(QLatin1Char(' '));
        fullCmd.append(quoteWindowsArgument(fArg));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (outType != FILE_TYPE_UNKNOWN || attachedParent) {
        const HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        if ((hOut != nullptr && hOut != INVALID_HANDLE_VALUE) ||
            (hErr != nullptr && hErr != INVALID_HANDLE_VALUE)) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = (hIn != INVALID_HANDLE_VALUE) ? hIn : nullptr;
            si.hStdOutput = (hOut != INVALID_HANDLE_VALUE) ? hOut : nullptr;
            si.hStdError = (hErr != INVALID_HANDLE_VALUE) ? hErr : nullptr;
        }
    }

    PROCESS_INFORMATION pi{};
    std::wstring wCmd = fullCmd.toStdWString();
    const BOOL started = CreateProcessW(
        QDir::toNativeSeparators(cliExe).toStdWString().c_str(),
        wCmd.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!started) {
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
#else
    QProcess proc;
    proc.setProcessChannelMode(QProcess::ForwardedChannels);
    proc.start(cliExe, forwardedArgs);
    if (!proc.waitForStarted(10000)) {
        return 1;
    }
    proc.waitForFinished(-1);
    return proc.exitCode();
#endif
}

} // namespace omniframe::os
