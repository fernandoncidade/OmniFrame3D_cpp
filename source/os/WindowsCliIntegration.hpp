#pragma once

#include <QString>
#include <QStringList>
#include <string>
#include <vector>

namespace omniframe::os {

/**
 * Estado consolidado da associação do omniframe-cli.exe e OmniFrame.exe
 * com o sistema operacional Windows 10/11 (CMD, PowerShell e Shell Explorer).
 */
struct CliIntegrationStatus {
    bool isWindows = false;
    bool cliBinaryExists = false;
    bool guiBinaryExists = false;
    bool installDirInUserPath = false;
    bool installDirInSystemPath = false;
    bool installDirInProcessPath = false;
    bool appPathsCliRegistered = false;
    bool appPathsGuiRegistered = false;
    bool fileAssocRegistered = false;

    QString installDirectory;
    QString cliBinaryPath;
    QString guiBinaryPath;
    QString currentExePath;

    bool isReadyForCmdAndPowerShell() const {
        return cliBinaryExists && (installDirInUserPath || installDirInSystemPath || installDirInProcessPath);
    }
};

/**
 * Configura saída/entrada de console para UTF-8 no Windows 10/11 (CMD e PowerShell),
 * garantindo exibição fiel de acentuação técnica em português e símbolos Unicode.
 */
void configureConsoleUtf8();

/**
 * Recupera argumentos de linha de comando em UTF-8 sem corromper caracteres acentuados
 * no Windows (via CommandLineToArgvW UTF-16 -> UTF-8).
 */
std::vector<std::string> commandLineArgumentsUtf8(int argc, char* argv[]);

/**
 * Retorna o caminho absoluto do executável atual.
 */
QString currentExecutableFilePath();

/**
 * Localiza um binário irmão no mesmo diretório do executável atual (ou de referenceExePath).
 */
QString findSiblingBinary(const QString& binaryFileName, const QString& referenceExePath = QString());

/**
 * Consulta no Registro do Windows 10/11 (HKCU/HKLM) o status completo da associação
 * de PATH, App Paths e extensão .omniframe.
 */
CliIntegrationStatus queryWindowsCliIntegration(const QString& referenceExePath = QString());

/**
 * Registra em HKCU (sem exigir privilégios de administrador) o diretório do OmniFrame
 * no PATH do usuário, App Paths (omniframe-cli.exe, OmniFrame.exe, omniframe.exe) e
 * associação de arquivo .omniframe, notificando o shell via WM_SETTINGCHANGE.
 */
bool installWindowsCliIntegration(const QString& referenceExePath = QString(), QString* detailsOut = nullptr);

/**
 * Remove com segurança as entradas HKCU registradas por installWindowsCliIntegration,
 * preservando intactas demais entradas de PATH e associações do sistema.
 */
bool uninstallWindowsCliIntegration(const QString& referenceExePath = QString(), QString* detailsOut = nullptr);

/**
 * Abre um terminal interativo do Windows 10/11 (CMD ou PowerShell) com o diretório do
 * omniframe-cli.exe ativo no PATH e UTF-8 habilitado.
 */
bool openInteractiveCliTerminal(bool usePowerShell,
                                const QString& workingDir = QString(),
                                const QString& referenceExePath = QString(),
                                QString* errorOut = nullptr);

/**
 * Inspeciona argumentos recebidos pelo OmniFrame.exe na inicialização:
 * - Se houver flags de CLI headless (--cli, --help, --version, --validate, --analyze,
 *   --example, --list-examples, --nbr-audit, etc.), delega de forma síncrona ao
 *   omniframe-cli.exe irmão e retorna o código de saída (>= 0).
 * - Caso contrário, extrai eventuais arquivos (.omniframe/.dxf/.ifc) passados pela
 *   linha de comando ou Explorer em outFilesToOpen e retorna -1 para iniciar a GUI.
 */
int forwardGuiArgsToCliIfRequested(int argc, char* argv[], QStringList& outFilesToOpen);

} // namespace omniframe::os
