# Modelo de Segurança e Resiliência / Security Model — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher o modelo de segurança em português</summary><br>

# OmniFrame 3D — Modelo de Segurança, Integridade de Dados e Resiliência

**Versão:** `2026.11.20.0`  
**Data:** `20 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Princípios de Segurança e Filosofia de Design

O **OmniFrame 3D** é projetado para atuar em ambientes de engenharia de missão crítica, estações de trabalho industriais e computadores acadêmicos. Sua arquitetura de segurança é fundamentada em cinco garantias estruturais:

1. **Soberania Absoluta e Arquitetura 100% Local-First:** O software opera em regime estrito de isolamento (*air-gapped*). Não há conexões de rede, portas de escuta TCP/UDP, telemetria em segundo plano ou dependência de servidores em nuvem.
2. **Segurança de Memória e RAII em C++23:** Uso sistemático de semântica de movimentação (*move semantics*), ponteiros inteligentes (`std::unique_ptr`, `std::shared_ptr`), guardas `QPointer` em janelas/callbacks assíncronos, snapshots imutáveis para relatórios não modais e ausência de ponteiros proprietários manuais com risco de *memory leak* ou *use-after-free*.
3. **Resiliência Numérica e Cancelamento Cooperativo:** Proteção matemática contra matrizes de rigidez singulares, comprimentos de barra nulos ($L \le 0$), estados não finitos (`NaN`/`Inf`), e cancelamento cooperativo de solvers assíncronos via `std::stop_token` sem invocação de `QApplication::processEvents()`.
4. **Hardening de Compilação e Binários:** Compilação MSVC com `/W4 /permissive- /EHsc /GS /sdl /guard:cf` e linkagem `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /GUARD:CF /CETCOMPAT`; builds GCC/Clang com `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=3`, `-fPIE` e `-Wl,-z,relro,-z,now,-z,noexecstack`, além de carregamento explícito de DLLs do sistema no Windows com `LOAD_LIBRARY_SEARCH_SYSTEM32`.
5. **Isolamento de Renderização Gráfica:** Todos os shaders OpenGL 3.3 Core são embutidos e validados internamente no binário (checando compilação, *linking* e descarte de contexto `aboutToBeDestroyed`), com *fallback* seguro via `QPainter` em ambientes *headless*/*offscreen*.

---

## 2. Superfície de Ataque e Modelo de Ameaças

| Vetor Potencial | Nível de Risco | Medida de Mitigação Implementada |
|---|:---:|---|
| **Arquivos de Projeto e Interoperabilidade Maliciosos (`.omniframe`, `.dxf`, `.ifc`)** | Médio | Limites explícitos de tamanho de arquivo, profundidade de aninhamento JSON e contagem de entidades/coordenadas; desserialização estrita com validação de tipos, índices nodais/materiais/seções existentes e checagem de números finitos (`std::isfinite`). |
| **Injeção de Fórmulas e Caracteres de Controle em Exportações** | Baixo | Neutralização de prefixos de fórmula (`=`, `+`, `-`, `@`, TAB) em CSV/XLSX (`escapeCsvField`, `sanitizeTabularCell`), preservação de literais numéricos negativos legítimos e remoção de caracteres ilegais XML 1.0 antes da geração de planilhas e relatórios HTML/Markdown. |
| **Singularidade Estrutural (Mecanismos)** | Baixo | Solvers diretos e iterativos (`SimplicialLDLT`, fallback `SparseLU`, `BiCGSTAB`) com verificação de pivôs e rejeição de matrizes indefinidas/singulares sem travamento do processo. |
| **Vazamento de Dados / Espionagem Industrial** | Inexistente | Nenhuma chamada de rede existe no executável; nenhuma coordenada ou projeto é enviado pela rede. |
| **Falhas de Driver Gráfico / Contexto OpenGL** | Baixo | Shaders GLSL 330 estáticos fixos compilados com verificação obrigatória (`isLinked()`), limpeza determinística em `QOpenGLContext::aboutToBeDestroyed` e fallback automático para `QPainter` quando não há contexto OpenGL ativo. |
| **CLI Windows 10/11, Caminhos UTF-8 & Registro (`HKCU`)** | Baixo | Decodificação UTF-16 `CommandLineToArgvW`, aberturas de arquivo via `pathFromUtf8`, registro opcional restrito a `HKCU` (sem elevação de administrador) e remoção verificada via `--uninstall-windows-integration` e GUI. |
| **Persistência Local, Recuperação, Logs e Utilitários (`%LOCALAPPDATA%`)** | Baixo | Escrita atômica (`QSaveFile` + *debounce*) em `user_preferences.json` (com backup `.corrupt.bak`), `ProjectSerializer`, pacote único atômico por sessão em `recovery/` (`ProjectRecoveryStore` v3 com `QSaveFile` + `QLockFile`) e `VersionEditor` (caminho seguro de execução via `QCoreApplication::applicationDirPath()`); logs em *tail* protegidos por mutex em `log\omniframe_aaaa-mm-dd.log` limitados a **10 arquivos** sem dados pessoais. |
| **Cadeia de Suprimentos, Presets & Fuzzing** | Baixo | Dependência externa (`Eigen 3.4.0`) fixada por *hash* criptográfico `SHA256`; workflow `.github/workflows/update-readme-version.yml` com *actions* fixadas por SHA de 40 caracteres e `permissions` mínimas; símbolos protegidos por `/PDBALTPATH:%_PDB%`; perfis locais em `CMakePresets.json` (`msvc-asan`, `linux-clang-ubsan-asan`, `linux-clang-tsan`), auditoria binária com `dumpbin`, CLI `clang-tidy` e alvos de fuzzing opcionais em `tests/fuzz/` (`BUILD_FUZZERS`). |

---

## 3. Validação Estrita de Entrada e Sanitização de Modelos

Ao importar ou abrir qualquer arquivo `.omniframe`, `.dxf` ou `.ifc`:
- **Limites Preventivos (Anti-DoS / OOM):** Arquivos acima do limite estrutural seguro, JSON excessivamente aninhados e strings IFC/DXF malformadas ou truncadas são rejeitados antes da alocação pesada de memória.
- **Comprimento de Barras e Geometria:** É verificado se as coordenadas são finitas e dentro de faixa física razoável, e se a distância euclidiana entre os nós excede a tolerância mínima (evitando divisões por zero em $EA/L$ e $EI/L^3$).
- **Propriedades Físicas:** Módulo de elasticidade ($E > 0$), coeficiente de Poisson válido, inércias ($I_y > 0, I_z > 0$), área ($A > 0$) e constante de torção ($J > 0$) são checados contra limites fisicamente admissíveis.
- **Topologia Consistente:** Referências a nós, materiais ou seções inexistentes e ciclos/duplicidades inválidos de MPC/diafragma são interceptados antes da montagem da matriz de rigidez global.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse security model in English</summary><br>

# OmniFrame 3D — Security Model, Data Integrity & Resilience

**Version:** `2026.11.20.0`  
**Date:** `November 20, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Security Principles & Architectural Guarantees

OmniFrame 3D is designed to operate in mission-critical engineering environments, industrial workstations, and academic computers. Its security architecture rests on five structural guarantees:

1. **Absolute Sovereignty & 100% Local-First Architecture:** Operates under strict isolation (*air-gapped*). There are zero network connections, zero TCP/UDP listening ports, zero background telemetry, and zero cloud server dependencies.
2. **Memory Safety & RAII in C++23:** Systematic use of move semantics, smart pointers (`std::unique_ptr`, `std::shared_ptr`), `QPointer` guards on asynchronous/modeless callbacks, immutable snapshots for modeless PDF generation, and complete avoidance of owning raw pointers that risk memory leaks or use-after-free defects.
3. **Numerical Resilience & Cooperative Cancellation:** Mathematical guards against singular stiffness matrices, zero-length members ($L \le 0$), propagation of non-finite values (`NaN`/`Inf`), and cooperative cancellation of background solvers via `std::stop_token` without nested `QApplication::processEvents()` event loops.
4. **Compiler & Binary Hardening:** MSVC builds enforce `/W4 /permissive- /EHsc /GS /sdl /guard:cf` and `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /GUARD:CF /CETCOMPAT`; GCC/Clang builds enforce `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=3`, `-fPIE`, and `-Wl,-z,relro,-z,now,-z,noexecstack`, alongside `LOAD_LIBRARY_SEARCH_SYSTEM32` for dynamic Windows system library resolution.
5. **Graphics Rendering Isolation:** All OpenGL 3.3 Core shaders are embedded and validated internally inside the binary (enforcing compile/link checks and deterministic teardown via `QOpenGLContext::aboutToBeDestroyed`), with an automatic `QPainter` fallback in headless/offscreen environments.

---

## 2. Attack Surface & Threat Model

| Threat Vector | Risk Level | Implemented Mitigation |
|---|:---:|---|
| **Malicious Project & Interoperability Files (`.omniframe`, `.dxf`, `.ifc`)** | Medium | Explicit file-size caps, JSON nesting-depth bounds, and entity/coordinate limits; strict JSON type validation and defensive DXF/IFC parsers verifying node/material/section references and finite numbers (`std::isfinite`). |
| **Formula Injection & Control Characters in Exports** | Low | Spreadsheet formula neutralization (`=`, `+`, `-`, `@`, leading TAB) in CSV/XLSX exports (`escapeCsvField`, `sanitizeTabularCell`) while preserving valid signed numeric literals, plus XML 1.0 illegal character stripping and HTML escaping. |
| **Structural Singularity (Mechanisms)** | Low | Direct and iterative solvers (`SimplicialLDLT`, `SparseLU`, `BiCGSTAB`) verifying pivots and rejecting indefinite/singular matrices cleanly without crashing the process. |
| **Data Exfiltration / Industrial Espionage** | None | Zero network calls exist in the executable; no coordinate or structural project ever leaves the local workstation. |
| **Graphics Driver Faults / OpenGL Context Loss** | Low | Static fixed-source GLSL 330 shaders validated via `isLinked()`, clean GL resource teardown on `aboutToBeDestroyed`, and automatic `QPainter` fallback when no OpenGL context exists. |
| **Windows 10/11 CLI, UTF-8 Paths & Registry (`HKCU`)** | Low | UTF-16 `CommandLineToArgvW` decoding, Unicode `pathFromUtf8` file I/O, optional `HKCU`-only user registry integration (zero admin elevation), and verified reversal via `--uninstall-windows-integration` and the GUI. |
| **Local Persistence, Recovery, Logs & Auxiliary Tools (`%LOCALAPPDATA%`)** | Low | Debounced atomic writes (`QSaveFile`) for `user_preferences.json` (with `.corrupt.bak` fallback), `ProjectSerializer`, single atomic per-session recovery bundle (`ProjectRecoveryStore` v3 with `QSaveFile` + `QLockFile`), and `VersionEditor` (safe launch path via `QCoreApplication::applicationDirPath()`); mutex-protected tail-seek logs in `log\omniframe_yyyy-mm-dd.log` strictly capped at **10 files** with zero personal data. |
| **Supply Chain, Presets & Fuzzing** | Low | External dependency (`Eigen 3.4.0`) pinned via `SHA256`; `.github/workflows/update-readme-version.yml` actions pinned to 40-char commit SHAs with least-privilege permissions; PDB paths stripped via `/PDBALTPATH:%_PDB%`; local presets in `CMakePresets.json` (`msvc-asan`, `linux-clang-ubsan-asan`, `linux-clang-tsan`), `dumpbin` checks, CLI `clang-tidy`, and optional fuzzer targets in `tests/fuzz/` (`BUILD_FUZZERS`). |

---

## 3. Strict Input Validation & Model Sanitization

When importing or opening any `.omniframe`, `.dxf`, or `.ifc` file:
- **Anti-DoS / OOM Bounds:** Oversized inputs, deeply nested JSON payloads, and malformed/truncated IFC/DXF records are rejected before large container allocations occur.
- **Member Length & Geometry:** Coordinates are verified to be finite and within physical engineering bounds, and Euclidean distances must exceed minimum geometric tolerances (preventing division by zero in $EA/L$ and $EI/L^3$ terms).
- **Physical Properties:** Modulus of elasticity ($E > 0$), valid Poisson's ratio, moments of inertia ($I_y > 0, I_z > 0$), area ($A > 0$), and torsion constant ($J > 0$) are enforced.
- **Consistent Topology:** References to nonexistent nodes/materials/sections and invalid MPC/diaphragm cycles or duplicates are intercepted prior to global stiffness matrix allocation.

</details>
