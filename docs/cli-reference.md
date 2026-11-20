# Referência da Linha de Comando / CLI Reference — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details open>
<summary>Clique para expandir / recolher a referência da CLI em português</summary><br>

# OmniFrame 3D — Referência de Linha de Comando (CLI) e Utilitários

**Versão:** `2026.11.14.0`  
**Data:** `14 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Execução do Aplicativo Principal (`OmniFrame.exe`)

Na versão atual (`2026.11.14.0`, confira [`source/main.cpp`](../source/main.cpp)), o executável **`OmniFrame.exe`** inicia diretamente a interface gráfica desktop Qt 6 (`MainWindow`) maximizada. Não há flags de modo *headless/batch* implementadas no executável GUI principal nesta versão.

- **Execução direta no diretório de build ou distribuição:**
```powershell
.\OmniFrame.exe
```
- **Alias de Execução MSIX (Microsoft Store):** Quando instalado via pacote MSIX ([`repo/AppxManifest.xml`](../repo/AppxManifest.xml)), o Windows registra o App Execution Alias:
```powershell
omniframe.exe
```

---

## 2. Utilitários CLI Oficiais do Repositório (`mocks/`)

O projeto inclui utilitários dedicados em linha de comando para manutenção de internacionalização e auditoria de versão:

### 2.1. `CompileTranslationsTerminal.exe` (Compilação e Verificação de Traduções `.ts` -> `.qm`)
Compilado pelo preset `compile-language-msvc-release` em `build/CompileLanguage/`:

```powershell
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --root .
.\build\CompileLanguage\CompileTranslationsTerminal.exe --root . --verbose
```

### 2.2. `VersionEditor.exe` (Sincronização e Auditoria de Versão e Data)
Compilado pelo preset `version-msvc-release` em `build/Version/`:

```powershell
.\build\Version\VersionEditor.exe --check --root .
.\build\Version\VersionEditor.exe --version 2026.11.14.0 --date 2026-11-14 --apply --root .
```

---

## 3. CLI Dedicada Headless (`omniframe-cli.exe`)

> Além do executável desktop `OmniFrame.exe`, o preset `omniframe-msvc-release` compila o binário headless `build/OmniFrame3D/omniframe-cli.exe` (também compilado e empacotado automaticamente na distribuição externa pela tarefa VS Code `Qt6: Compilar e Executar OmniFrame 3D (MSVC)`, pela tarefa dedicada `Qt6: Compilar omniframe-cli (MSVC)` e por `cmake/gerar_distribuicoes_limpas.ps1`), compartilhando `OmniFrameCore` e o núcleo puro `OmniFrameKernel`:
> - `omniframe-cli.exe --version` | `--help` | `--validate <arquivo.omniframe|dxf|ifc>`
> - `omniframe-cli.exe --analyze <arquivo.omniframe|dxf|ifc> --analysis <static|modal|buckling|nonlinear|pdelta|harmonic|spectrum> [--solver <auto|ldlt|llt|sparselu|cg|bicgstab>] [--modes <N>] [--steps <N>] [--method <nr|arclength>] [--export <json|csv|vtk|dxf|ifc> <caminho>] [--report <md|html|pdf> <caminho>] [--output <caminho.json>] [--json]`
> - Códigos de saída: `0` (sucesso), `1` (erro de uso/I/O), `2` (falha numérica na análise).
> - Verificação automatizada (`20` suítes CTest): `ctest --preset testes-msvc-release --output-on-failure`.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details open>
<summary>Click to expand / collapse CLI reference in English</summary><br>

# OmniFrame 3D — Command Line Reference & Companion Utilities

**Version:** `2026.11.14.0`  
**Date:** `November 14, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Main Application Execution (`OmniFrame.exe`)

In the current version (`2026.11.14.0`, see [`source/main.cpp`](../source/main.cpp)), the **`OmniFrame.exe`** executable launches the Qt 6 desktop graphical interface (`MainWindow`) maximized directly. No *headless/batch* CLI mode flags are implemented in the main GUI executable in this version.

- **Direct launch from the build or distribution directory:**
```powershell
.\OmniFrame.exe
```
- **MSIX Execution Alias (Microsoft Store):** When installed via the MSIX package ([`repo/AppxManifest.xml`](../repo/AppxManifest.xml)), Windows registers the App Execution Alias:
```powershell
omniframe.exe
```

---

## 2. Official Repository CLI Utilities (`mocks/`)

The project includes dedicated command-line utilities for internationalization maintenance and version auditing:

### 2.1. `CompileTranslationsTerminal.exe` (`.ts` -> `.qm` Translation Compilation & Verification)
Built by the `compile-language-msvc-release` preset in `build/CompileLanguage/`:

```powershell
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --root .
.\build\CompileLanguage\CompileTranslationsTerminal.exe --root . --verbose
```

### 2.2. `VersionEditor.exe` (Version and Date Synchronization & Auditing)
Built by the `version-msvc-release` preset in `build/Version/`:

```powershell
.\build\Version\VersionEditor.exe --check --root .
.\build\Version\VersionEditor.exe --version 2026.11.14.0 --date 2026-11-14 --apply --root .
```

---

## 3. Dedicated Headless CLI (`omniframe-cli.exe`)

Alongside the interactive desktop binary `OmniFrame.exe`, the release build (`omniframe-msvc-release`) compiles `build/OmniFrame3D/omniframe-cli.exe` (also automatically built and packaged into the external distribution by the VS Code task `Qt6: Compilar e Executar OmniFrame 3D (MSVC)`, the dedicated task `Qt6: Compilar omniframe-cli (MSVC)`, and `cmake/gerar_distribuicoes_limpas.ps1`), which shares `OmniFrameCore` and the Qt-free `OmniFrameKernel` to validate models, execute structural analyses in batch mode, and export datasets/reports without opening a GUI window.

### 3.1. Commands & Supported Options

```powershell
.\build\OmniFrame3D\omniframe-cli.exe --version
.\build\OmniFrame3D\omniframe-cli.exe --help
.\build\OmniFrame3D\omniframe-cli.exe --validate <file.omniframe|dxf|ifc>
.\build\OmniFrame3D\omniframe-cli.exe --analyze <file.omniframe|dxf|ifc> [options]
```

| Flag | Arguments | Description |
|---|---|---|
| `--version` | — | Prints CLI version information and exits (`0`). |
| `--help` | — | Prints CLI usage and flags (`0`). |
| `--validate` | `<file.omniframe\|dxf\|ifc>` | Loads and validates topology, finiteness, and physical properties offline. |
| `--analyze` | `<file.omniframe\|dxf\|ifc>` | Executes the selected structural analysis on the input file. |
| `--analysis` | `static\|modal\|buckling\|nonlinear\|pdelta\|harmonic\|spectrum` | Selects analysis type. |
| `--solver` | `auto\|ldlt\|llt\|sparselu\|cg\|bicgstab` | Selects linear solver backend from `LinearSolverSuite`. |
| `--modes` | `<N>` | Number of eigenvalues/modes for `modal` or `buckling` (default `6`). |
| `--steps` | `<N>` | Number of incremental steps for nonlinear analyses (default `20`). |
| `--method` | `nr\|arclength` | Nonlinear equilibrium iteration strategy (default `nr`). |
| `--export` | `<json\|csv\|vtk\|dxf\|ifc> <path>` | Exports post-processed model or interoperability dataset. |
| `--report` | `<md\|html\|pdf> <path>` | Generates Markdown, HTML, or executive A4 PDF calculation report. |
| `--output` | `<path.json>` | Writes structured JSON analysis summary to file. |
| `--json` | — | Emits structured JSON summary directly to `stdout`. |

### 3.2. Process Exit Codes & Automated Tests

- `0`: Validation, analysis, export, and report generation completed cleanly.
- `1`: Invalid CLI arguments, unreadable input file, unsupported format, or export/report write failure.
- `2`: Numerical analysis failure (mechanism, singularity, or convergence failure).
- Modular CTest verification (`20` test suites): `ctest --preset testes-msvc-release --output-on-failure`.

</details>
