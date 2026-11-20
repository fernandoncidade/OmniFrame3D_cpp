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

**Versão:** `2026.11.13.0`  
**Data:** `13 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Execução do Aplicativo Principal (`OmniFrame.exe`)

Na versão atual (`2026.11.13.0`, confira [`source/main.cpp`](../source/main.cpp)), o executável **`OmniFrame.exe`** inicia diretamente a interface gráfica desktop Qt 6 (`MainWindow`) maximizada. Não há flags de modo *headless/batch* implementadas no executável GUI principal nesta versão.

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
.\build\Version\VersionEditor.exe --version 2026.11.13.0 --date 2026-11-13 --apply --root .
```

---

## 3. Flags Batch / Headless Planejadas (Roadmap Futuro — Não Implementadas)

> [!NOTE]
> Em atenção à transparência técnica (Política 10.1.1 da Microsoft Store), os argumentos abaixo **NÃO** estão presentes no executável `OmniFrame.exe` da versão `2026.11.13.0` e representam apenas propostas futuras de automação:
> `--help` (`-h`), `--version` (`-v`), `--input <arquivo.omniframe>` (`-i`), `--headless` (`-b`), `--export <arquivo>` (`-e`), `--validate` (`-c`).  
> Para validação numérica automatizada em CLI na versão atual, utilize a suíte CTest: `ctest --preset testes-msvc-release --output-on-failure`.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details open>
<summary>Click to expand / collapse CLI reference in English</summary><br>

# OmniFrame 3D — Command Line Reference & Companion Utilities

**Version:** `2026.11.13.0`  
**Date:** `November 13, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Main Application Execution (`OmniFrame.exe`)

In the current version (`2026.11.13.0`, see [`source/main.cpp`](../source/main.cpp)), the **`OmniFrame.exe`** executable launches the Qt 6 desktop graphical interface (`MainWindow`) maximized directly. No *headless/batch* CLI mode flags are implemented in the main GUI executable in this version.

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
.\build\Version\VersionEditor.exe --version 2026.11.13.0 --date 2026-11-13 --apply --root .
```

---

## 3. Planned Batch / Headless Flags (Future Roadmap — Not Implemented)

> [!NOTE]
> For strict technical transparency (Microsoft Store Policy 10.1.1), the arguments below are **NOT** present in the `OmniFrame.exe` executable in version `2026.11.13.0` and represent future automation proposals only:
> `--help` (`-h`), `--version` (`-v`), `--input <file.omniframe>` (`-i`), `--headless` (`-b`), `--export <file>` (`-e`), `--validate` (`-c`).  
> For automated CLI numerical validation in the current version, run the CTest suite: `ctest --preset testes-msvc-release --output-on-failure`.

</details>
