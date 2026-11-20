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

**Versão:** `2026.11.21.0`  
**Data:** `21 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

> [!WARNING]
> **Escopo Auxiliar da CLI (`omniframe-cli.exe`):** A ferramenta de linha de comando, seus resumos JSON, a auditoria `--nbr-audit` e os arquivos ou relatórios exportados (`--export`, `--report`) utilizam exatamente o mesmo motor auxiliar gratuito da interface gráfica, o qual **ainda não está maduro o suficiente para produção ou uso profissional definitivo**. Devido a limitações, falhas humanas e possíveis erros no código-fonte (abrangendo tanto a matemática aplicada quanto a física envolvida, razão pela qual seus algoritmos podem apresentar falhas), **todos os resultados devem ser conferidos manualmente ou comparados com ferramentas mais consolidadas no mercado**. Caso o usuário decida utilizá-los exclusivamente em produção ou uso profissional na análise e dimensionamento de estruturas, todas as consequências serão de sua inteira e exclusiva responsabilidade (consulte [`README.md`](../README.md#ptbr) e o [EULA Oficial](../source/assets/EULA/EULA_pt_BR.txt)).

## 1. Execução do Aplicativo Principal (`OmniFrame.exe`) e Delegação CLI

O executável desktop **`OmniFrame.exe`** inicia a interface gráfica Qt 6 (`MainWindow`) maximizada por padrão e está integrado diretamente ao ecossistema de linha de comando:

- **Abertura direta de projeto pela linha de comando (CMD / PowerShell / Explorador):**
```bat
OmniFrame.exe "C:\Projetos\Pórtico São Paulo\modelo_técnico.omniframe"
```
(aceita `.omniframe`, `.dxf` e `.ifc`).
- **Delegação transparente de comandos CLI para `omniframe-cli.exe`:** ao passar flags *headless* ao executável GUI (`--cli`, `--version`, `-v`, `--help`, `-h`, `--list-examples`, `--validate`, `--analyze`, `--example`, `--nbr-audit`, `--audit-nbr`, `--check-windows-integration`, `--install-windows-integration`, `--uninstall-windows-integration`), `OmniFrame.exe` encaminha a execução de forma síncrona para o binário irmão `omniframe-cli.exe` ([`source/main.cpp`](../source/main.cpp) e [`source/os/WindowsCliIntegration.cpp`](../source/os/WindowsCliIntegration.cpp)):
```powershell
# No PowerShell, prefira chamar omniframe-cli diretamente; via OmniFrame.exe encadeie pipeline:
.\OmniFrame.exe --version | Out-String
```
- **Alias de Execução MSIX (Microsoft Store):** Quando instalado via pacote MSIX (`AppxManifest.xml`), o Windows 10/11 registra dois `uap5:AppExecutionAlias`:
```powershell
omniframe.exe
omniframe-cli.exe
```

---

## 2. Utilitários CLI Oficiais do Repositório (`mocks/`)

O projeto inclui utilitários dedicados em linha de comando para manutenção de internacionalização e auditoria de versão:

### 2.1. `CompileTranslationsTerminal.exe` (Compilação e Verificação de Traduções `.ts` -> `.qm`)
Compilado pelo preset `compile-language-msvc-release` em `build/CompileLanguage/`:

```powershell
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --root .
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --file source\locale\language\omniframe_pt_BR.ts
.\build\CompileLanguage\CompileTranslationsTerminal.exe --root . --verbose
```

*(O modo `--check` / `--check --file` opera de forma estritamente somente leitura sem reescrever arquivos `.qm`.)*

### 2.2. `VersionEditor.exe` (Sincronização e Auditoria de Versão e Data)
Compilado pelo preset `version-msvc-release` em `build/Version/`:

```powershell
.\build\Version\VersionEditor.exe --check --root .
.\build\Version\VersionEditor.exe --version 2026.11.21.0 --date 2026-11-21 --apply --root .
```

---

## 3. CLI Dedicada Headless (`omniframe-cli.exe`) e Associação Windows 10/11

O binário nativo de console `omniframe-cli.exe` (`/SUBSYSTEM:CONSOLE`) é compilado junto com `OmniFrame.exe` em `build/OmniFrame3D/` pelo preset `omniframe-msvc-release` (e empacotado junto com `OmniFrame.exe` e `register_cli_windows.ps1` por `cmake/gerar_distribuicoes_limpas.ps1`), compartilhando integralmente `OmniFrameCore` e `OmniFrameKernel`.

### 3.1. Associação com Windows 10/11 (CMD e PowerShell)
- **Menu GUI em `OmniFrame.exe`:** `Configurações > CLI Windows (CMD / PowerShell)` (registrar sem admin, remover, verificar status, abrir CMD ou PowerShell já prontos com a CLI).
- **Autorregistro HKCU via CLI:** `omniframe-cli --install-windows-integration` / `--check-windows-integration` / `--uninstall-windows-integration`.
- **Script PowerShell Portátil:** `.\cmake\register_cli_windows.ps1` (suporta `-CheckOnly`, `-Uninstall`, `-Action install|uninstall|check`, `-CliPath` e `-Json`) ou `.\register_cli_windows.ps1` dentro da pasta distribuída.
- **Instalador Inno Setup (`OmniFrame.iss`) & MSIX (`AppxManifest.xml`):** tarefas `addtopath` e `assocproj` marcadas por padrão; alias `omniframe-cli.exe` com `desktop4:Subsystem="console"`.
- **Codificação UTF-8 no terminal:** `chcp 65001 >nul` no CMD e `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8` no PowerShell (`CommandLineToArgvW` + caminhos UTF-8 nativos em DXF/IFC4/CSV/VTK/JSON/MD/HTML/PDF).

### 3.2. Comandos e Opções Suportadas

```powershell
omniframe-cli --version
omniframe-cli --help
omniframe-cli --list-examples
omniframe-cli --validate <arquivo.omniframe|dxf|ifc>
omniframe-cli --analyze <arquivo.omniframe|dxf|ifc> [opções]
omniframe-cli --example building --nbr-audit --json
omniframe-cli --check-windows-integration --json
```

| Opção | Argumentos | Descrição |
|---|---|---|
| `--version` / `-v` | — | Imprime versão da CLI, C++23, Eigen 3.4.0 e Schema `2.1.0` e encerra (`0`). |
| `--help` / `-h` | — | Imprime o guia oficial de uso no Windows 10/11 (CMD e PowerShell) e opções (`0`). |
| `--list-examples` | — | Lista os 3 exemplos oficiais embutidos (`1 \| building`, `2 \| cantilever`, `3 \| truss`). |
| `--example` | `<nome\|1\|2\|3>` | Seleciona modelo embutido (`building`/`portico`/`edificio`/`frame`, `cantilever`/`balanco`/`lframe`, `truss`/`trelica`/`passarela`). |
| `--validate` | `[<arquivo.omniframe\|dxf\|ifc>]` | Carrega (ou usa `--example`) e valida topologia, finitude, conectividade BFS, malhas 2D/3D e `validationIssues` (`0`=válido, `2`=inválido). |
| `--analyze` | `[<arquivo.omniframe\|dxf\|ifc>]` | Executa a análise estrutural configurada sobre o arquivo de entrada ou `--example`. |
| `--nbr-audit` / `--audit-nbr` | `[<arquivo>]` | Executa auditoria consolidada ABNT NBR (6118, 8800, 6120, 6122, 6123, 7480), resolvendo estática se necessário, e preenche `nbrAudit`. |
| `--analysis` | `static\|modal\|buckling\|nonlinear\|pdelta\|harmonic\|spectrum\|transient\|seismic` | Seleciona o tipo de análise estrutural (padrão `static`). |
| `--solver` | `auto\|ldlt\|llt\|sparselu\|cg\|bicgstab` | Seleciona o solver linear em `LinearSolverSuite` (padrão `auto`, registra fallback em `usedFallback`). |
| `--modes` | `<N>` | Número de autovalores/modos para `modal`, `buckling` ou `spectrum` (padrão `6`). |
| `--steps` | `<N>` | Número de incrementos para `nonlinear`/`pdelta` ou passos temporais para `transient`/`seismic` (padrão `20`). |
| `--dt` | `<segundos>` | Incremento de tempo para integração `transient` ou `seismic` (mínimo `1e-5`, padrão `0.02`). |
| `--scheme` | `newmark\|hht` | Integrador temporal implícito (`Newmark-beta` ou `HHT-alpha`; padrão `newmark`). |
| `--case` | `<id>` | Seleciona um `LoadCase` específico para execução `static` ou `transient`. |
| `--combo` | `<id>` | Seleciona uma `LoadCombination` específica para execução `static`. |
| `--method` | `nr\|arclength` | Estratégia iterativa não linear: Newton-Raphson ou Arc-Length de Crisfield (padrão `nr`). |
| `--load-factor` | `<fator>` | Multiplicador alvo de carga para análises `nonlinear`/`pdelta` (padrão `1.0`). |
| `--full-tangent` | — | Atualiza a matriz tangente global `Kt` a cada iteração Newton-Raphson (padrão). |
| `--modified-newton` | — | Mantém `Kt` constante dentro de cada passo incremental (Newton Modificado). |
| `--line-search` | — | Habilita busca linear backtracking em Newton-Raphson (padrão). |
| `--no-line-search` | — | Desabilita busca linear em Newton-Raphson. |
| `--imperfection` | `<amp_m>` | Aplica perturbação geométrica pelo modo de flambagem na amplitude especificada (m) antes de resolver/exportar. |
| `--imperfection-mode` | `<N>` | Modo de flambagem base `1` utilizado em `--imperfection` (padrão `1`). |
| `--export` | `<json\|csv\|vtk\|dxf\|ifc> <caminho>` | Exporta resultados 1D/2D/3D (repetível: `.vtk` `UNSTRUCTURED_GRID` com `von_mises_kPa`, `.csv` nodal/contínuo, `.json`, `.dxf` preservando `surfaceMesh`, `.ifc` preservando `surfaceMesh`, apoios e espessuras reais). |
| `--report` | `<md\|html\|pdf> <caminho>` | Gera memorial técnico em Markdown, HTML5 ou PDF executivo A4 (repetível; inclui resultados e auditoria NBR). |
| `--output` | `<caminho.json>` | Grava o resumo estruturado completo em JSON no arquivo informado. |
| `--json` | — | Emite o resumo estruturado em JSON diretamente na saída padrão (`stdout`). |
| `--check-windows-integration` | — | Consulta `PATH` do usuário/sistema/processo, `App Paths` e `.omniframe` no Registro Windows. |
| `--install-windows-integration` | — | Registra `omniframe-cli.exe`, `OmniFrame.exe`, `omniframe.exe` e `.omniframe` em `HKCU` sem exigir admin. |
| `--uninstall-windows-integration` | — | Remove os registros `HKCU` da CLI e associação `.omniframe`. |

### 3.3. Códigos de Saída e Verificação Automatizada

- Códigos de saída (`%ERRORLEVEL%` / `$LASTEXITCODE`): `0` (sucesso), `1` (erro de sintaxe CLI, flag numérica não finita `NaN`/`Inf` ou inválida — validação estrita via `std::from_chars` com emissão de JSON válido quando `--json`/`--output` estão ativos —, falha de I/O ou registro Windows), `2` (falha na validação estrutural `--validate` ou falha numérica/física na solução).
- **Logs Diários e Fontes em Modo Headless:** Cada execução de `omniframe-cli.exe` registra automaticamente eventos em `%LOCALAPPDATA%\OmniFrame\log\omniframe_aaaa-mm-dd.log` (rotação automática com máximo de 10 arquivos). Ao exportar relatórios PDF sem interface gráfica (`--report pdf`), a CLI configura automaticamente `QT_QPA_PLATFORM=offscreen` e resolve `QT_QPA_FONTDIR` para `%WINDIR%\Fonts` no Windows.
- Verificação automatizada (`22` suítes CTest): `ctest --preset testes-msvc-release --output-on-failure`.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details open>
<summary>Click to expand / collapse CLI reference in English</summary><br>

# OmniFrame 3D — Command Line Reference & Companion Utilities

**Version:** `2026.11.21.0`  
**Date:** `November 21, 2026`  
**Author:** Fernando Nillsson Cidade  

---

> [!WARNING]
> **Auxiliary Scope of the CLI (`omniframe-cli.exe`):** The command-line tool, its JSON outputs, `--nbr-audit` checks, and exported files/reports (`--export`, `--report`) use the exact same free auxiliary engine as the desktop GUI, which **is not yet mature enough for definitive production or professional use**. Due to limitations, human flaws, and possible defects in the source code (encompassing both the applied mathematics and the underlying physics, due to which its algorithms may produce errors), **all outputs must be verified manually or compared against more established tools in the market**. Should the user decide to rely exclusively on this tool for production or professional structural analysis and design, all resulting consequences shall be the user's sole and entire responsibility (see [`README.md`](../README.md#enus) and the [Official EULA](../source/assets/EULA/EULA_en_US.txt)).

## 1. Main Application Execution (`OmniFrame.exe`) & CLI Delegation

The **`OmniFrame.exe`** desktop executable opens the Qt 6 graphical workspace (`MainWindow`) maximized by default and integrates directly with the command-line ecosystem:

- **Direct project launch from the command line (CMD / PowerShell / Explorer):**
```bat
OmniFrame.exe "C:\Projects\Pórtico São Paulo\modelo_técnico.omniframe"
```
(accepts `.omniframe`, `.dxf`, and `.ifc`).
- **Transparent CLI delegation to `omniframe-cli.exe`:** passing headless flags to the GUI executable (`--cli`, `--version`, `-v`, `--help`, `-h`, `--list-examples`, `--validate`, `--analyze`, `--example`, `--nbr-audit`, `--audit-nbr`, `--check-windows-integration`, `--install-windows-integration`, `--uninstall-windows-integration`) synchronously forwards execution to the sibling `omniframe-cli.exe` binary ([`source/main.cpp`](../source/main.cpp) and [`source/os/WindowsCliIntegration.cpp`](../source/os/WindowsCliIntegration.cpp)):
```powershell
# In PowerShell, prefer invoking omniframe-cli directly; when calling OmniFrame.exe in CLI mode, pipe through Out-String:
.\OmniFrame.exe --version | Out-String
```
- **MSIX Execution Aliases (Microsoft Store):** When installed via the MSIX package (`AppxManifest.xml`), Windows 10/11 registers two `uap5:AppExecutionAlias` entries:
```powershell
omniframe.exe
omniframe-cli.exe
```

---

## 2. Official Repository CLI Utilities (`mocks/`)

The project includes dedicated command-line utilities for internationalization maintenance and version auditing:

### 2.1. `CompileTranslationsTerminal.exe` (`.ts` -> `.qm` Translation Compilation & Verification)
Built by the `compile-language-msvc-release` preset in `build/CompileLanguage/`:

```powershell
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --root .
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --file source\locale\language\omniframe_en_US.ts
.\build\CompileLanguage\CompileTranslationsTerminal.exe --root . --verbose
```

*(`--check` / `--check --file` runs strictly read-only without rewriting `.qm` binaries on disk.)*

### 2.2. `VersionEditor.exe` (Version and Date Synchronization & Auditing)
Built by the `version-msvc-release` preset in `build/Version/`:

```powershell
.\build\Version\VersionEditor.exe --check --root .
.\build\Version\VersionEditor.exe --version 2026.11.21.0 --date 2026-11-21 --apply --root .
```

---

## 3. Dedicated Headless CLI (`omniframe-cli.exe`) & Windows 10/11 Association

The native Windows console binary `omniframe-cli.exe` (`/SUBSYSTEM:CONSOLE`) is compiled alongside `OmniFrame.exe` in `build/OmniFrame3D/` by `omniframe-msvc-release` (and packaged together with `OmniFrame.exe` and `register_cli_windows.ps1` by `cmake/gerar_distribuicoes_limpas.ps1`), sharing 100% of `OmniFrameCore` and `OmniFrameKernel`.

### 3.1. Windows 10/11 Integration (CMD & PowerShell)
- **Desktop GUI menu (`OmniFrame.exe`):** `Settings > Windows CLI (CMD / PowerShell)` (register without admin, uninstall, check status, launch preconfigured CMD or PowerShell).
- **Non-admin HKCU self-registration:** `omniframe-cli --install-windows-integration` / `--check-windows-integration` / `--uninstall-windows-integration`.
- **Portable PowerShell Script:** `.\cmake\register_cli_windows.ps1` (supports `-CheckOnly`, `-Uninstall`, `-Action install|uninstall|check`, `-CliPath`, and `-Json`) or `.\register_cli_windows.ps1` inside every distribution directory.
- **Inno Setup (`OmniFrame.iss`) & MSIX (`AppxManifest.xml`):** default-checked `addtopath` and `assocproj` tasks; `omniframe-cli.exe` alias with `desktop4:Subsystem="console"`.
- **UTF-8 console support:** `chcp 65001 >nul` in CMD and `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8` in PowerShell (`CommandLineToArgvW` + native UTF-8 DXF/IFC4/CSV/VTK/JSON/MD/HTML/PDF paths).

### 3.2. Commands & Supported Options

```powershell
omniframe-cli --version
omniframe-cli --help
omniframe-cli --list-examples
omniframe-cli --validate <file.omniframe|dxf|ifc>
omniframe-cli --analyze <file.omniframe|dxf|ifc> [options]
omniframe-cli --example building --nbr-audit --json
omniframe-cli --check-windows-integration --json
```

| Flag | Arguments | Description |
|---|---|---|
| `--version` / `-v` | — | Prints CLI version, C++23, Eigen 3.4.0, and Schema `2.1.0` and exits (`0`). |
| `--help` / `-h` | — | Prints official Windows 10/11 (CMD & PowerShell) usage and flags (`0`). |
| `--list-examples` | — | Lists the 3 built-in benchmark models (`1 \| building`, `2 \| cantilever`, `3 \| truss`). |
| `--example` | `<name\|1\|2\|3>` | Loads built-in model (`building`/`portico`/`edificio`/`frame`, `cantilever`/`balanco`/`lframe`, `truss`/`trelica`/`passarela`). |
| `--validate` | `[<file.omniframe\|dxf\|ifc>]` | Loads input (or uses `--example`) and validates topology, finiteness, BFS connectivity, 2D/3D meshes, and `validationIssues` (`0`=valid, `2`=invalid). |
| `--analyze` | `[<file.omniframe\|dxf\|ifc>]` | Executes configured structural analysis on input file or `--example`. |
| `--nbr-audit` / `--audit-nbr` | `[<file>]` | Runs consolidated ABNT NBR audit (6118, 8800, 6120, 6122, 6123, 7480), auto-solving static if needed, emitting `nbrAudit`. |
| `--analysis` | `static\|modal\|buckling\|nonlinear\|pdelta\|harmonic\|spectrum\|transient\|seismic` | Selects structural analysis type (default `static`). |
| `--solver` | `auto\|ldlt\|llt\|sparselu\|cg\|bicgstab` | Selects linear solver backend from `LinearSolverSuite` (default `auto`, tracks `usedFallback`). |
| `--modes` | `<N>` | Number of eigenvalues/modes for `modal`, `buckling`, or `spectrum` (default `6`). |
| `--steps` | `<N>` | Incremental steps for `nonlinear`/`pdelta` or time steps for `transient`/`seismic` (default `20`). |
| `--dt` | `<seconds>` | Time step increment for `transient` or `seismic` integration (minimum `1e-5`, default `0.02`). |
| `--scheme` | `newmark\|hht` | Implicit time integrator (`Newmark-beta` or `HHT-alpha`; default `newmark`). |
| `--case` | `<id>` | Selects a specific `LoadCase` ID for `static` or `transient` execution. |
| `--combo` | `<id>` | Selects a specific `LoadCombination` ID for `static` execution. |
| `--method` | `nr\|arclength` | Nonlinear equilibrium strategy: Newton-Raphson or Crisfield Arc-Length (default `nr`). |
| `--load-factor` | `<factor>` | Target load factor multiplier for `nonlinear`/`pdelta` (default `1.0`). |
| `--full-tangent` | — | Updates global tangent stiffness `Kt` on every Newton-Raphson iteration (default). |
| `--modified-newton` | — | Holds `Kt` constant per incremental step (Modified Newton). |
| `--line-search` | — | Enables backtracking line search in Newton-Raphson (default). |
| `--no-line-search` | — | Disables line search in Newton-Raphson. |
| `--imperfection` | `<amp_m>` | Applies buckling-mode geometric imperfection of amplitude (m) prior to solving/exporting. |
| `--imperfection-mode` | `<N>` | 1-based buckling mode index used by `--imperfection` (default `1`). |
| `--export` | `<json\|csv\|vtk\|dxf\|ifc> <path>` | Exports 1D/2D/3D results (repeatable: `.vtk` `UNSTRUCTURED_GRID` with `von_mises_kPa`, `.csv` nodal/continuum, `.json`, `.dxf` preserving `surfaceMesh`, `.ifc` preserving `surfaceMesh`, supports, and real shell thickness). |
| `--report` | `<md\|html\|pdf> <path>` | Generates Markdown, HTML5, or executive A4 PDF calculation report (repeatable; includes results & NBR audit). |
| `--output` | `<path.json>` | Writes full structured JSON analysis summary cleanly to disk. |
| `--json` | — | Emits structured JSON summary directly to `stdout`. |
| `--check-windows-integration` | — | Queries user/system/process `PATH`, `App Paths`, and `.omniframe` association in Windows Registry. |
| `--install-windows-integration` | — | Registers `omniframe-cli.exe`, `OmniFrame.exe`, `omniframe.exe`, and `.omniframe` in `HKCU` without admin. |
| `--uninstall-windows-integration` | — | Removes `HKCU` CLI entries and `.omniframe` association cleanly. |

### 3.3. Process Exit Codes & Automated Tests

- Exit codes (`%ERRORLEVEL%` / `$LASTEXITCODE`): `0` (success), `1` (CLI usage error, invalid or non-finite `NaN`/`Inf` numeric flag — strictly checked via `std::from_chars`, emitting valid JSON errors when `--json`/`--output` is active — I/O failure, or registry error), `2` (validation `--validate` failure or numerical/physical solver failure).
- **Daily Execution Logs & Headless PDF Font Resolution:** Every `omniframe-cli.exe` invocation automatically records diagnostic events to `%LOCALAPPDATA%\OmniFrame\log\omniframe_yyyy-mm-dd.log` (capped at 10 files via automatic rotation). When generating A4 PDF reports headlessly (`--report pdf`), the CLI automatically initializes `QT_QPA_PLATFORM=offscreen` and points `QT_QPA_FONTDIR` to `%WINDIR%\Fonts` on Windows.
- Modular CTest verification (`22` test suites): `ctest --preset testes-msvc-release --output-on-failure`.

</details>
