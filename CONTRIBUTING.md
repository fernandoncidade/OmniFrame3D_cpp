# Guia de Contribuição / Contributing Guide — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher as instruções em português</summary><br>

# Contribuindo com o OmniFrame 3D

Agradecemos profundamente seu interesse em contribuir com o **OmniFrame 3D**! Sua colaboração auxilia no avanço de ferramentas livres, transparentes e cientificamente rigorosas para análise estrutural tridimensional por elementos finitos.

> [!NOTE]
> **Sobre o Desenvolvimento, Suporte e Contribuições Externas:**  
> Este projeto é concebido, desenvolvido e mantido primariamente como uma iniciativa pessoal e educacional pelo autor, que é iniciante e está em processo contínuo de aprendizado em programação C++ e análise computacional de estruturas.  
> O envio de sugestões, abertura de *Issues* e submissão de *Pull Requests (PRs)* são sempre bem-vindos! No entanto, ressalta-se que a análise de código, interação em tópicos matemáticos/estruturais e incorporação de contribuições são realizadas no próprio ritmo do autor e de forma estritamente voluntária. Caso um chamado ou PR demore para ser respondido ou não venha a ser incorporado à branch principal, por favor compreenda a limitação de tempo. Conforme estabelecido pela licença GNU AGPLv3, você possui total liberdade para criar um *Fork* independente deste repositório e evoluir suas próprias ideias e versões.

---

## 1. Código de Conduta

Ao participar deste projeto, você concorda em cumprir nosso [Código de Conduta](CODE_OF_CONDUCT.md). Trate todos os colaboradores com respeito, urbanidade e profissionalismo.

---

## 2. Formas de Contribuir

Você pode colaborar com o OmniFrame 3D de diversas maneiras:
- **Relatar Bugs:** Identifique inconsistências na matriz de rigidez, problemas em condições de contorno, artefatos gráficos ou crashes de memória.
- **Propor Formulações e Heurísticas:** Sugira leis constitutivas avançadas de materiais (elastoplasticidade, dano), elementos de interface/contato, verificações normativas automatizadas ou novas bibliotecas de seções transversais.
- **Melhorar a Documentação:** Aprimore as deduções matemáticas em `docs/architecture.md`, deduza casos de teste de referência em `docs/compliance.md` ou documente novos atalhos.
- **Enviar Pull Requests:** Implemente novas ferramentas de modelagem, aprimoramentos de performance no solver esparso Cholesky (`SimplicialLDLT`) ou refinamento de shaders GLSL.
- **Internacionalização (i18n):** Auxilie na tradução e localização da interface gráfica.

---

## 3. Configuração do Ambiente de Desenvolvimento

### 3.1. Pré-requisitos
- **Sistema Operacional:** Windows 10 / 11 x64 (compatível com Linux e macOS via compilação cruzada)
- **Compilador:** Microsoft Visual C++ (MSVC) com suporte integral a C++23
- **CMake:** Versão 3.28 ou superior
- **Qt:** Qt 6.11.1 (Módulos: `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`)
- **Eigen:** Eigen 3.4.0 (gerenciado automaticamente via `FetchContent` do CMake ou instalado localmente)
- **Ferramenta de Build:** Ninja (altamente recomendado para compilação paralela acelerada)

### 3.2. Compilando com Presets do CMake

Disponibilizamos presets MSVC pré-configurados e separados por build no arquivo `CMakePresets.json`:

```powershell
# Clonar o repositório
git clone https://github.com/fernandoncidade/OmniFrame_cpp.git
cd OmniFrame_cpp

# Configurar e compilar o aplicativo principal (build/OmniFrame3D)
cmake --preset omniframe-msvc-release
cmake --build --preset omniframe-msvc-release

# Configurar e compilar ferramentas auxiliares e testes separados
cmake --preset version-msvc-release && cmake --build --preset version-msvc-release
cmake --preset compile-language-msvc-release && cmake --build --preset compile-language-msvc-release
cmake --preset testes-msvc-release && cmake --build --preset testes-msvc-release

# Executar a suíte de validação estrutural e testes unitários (20 alvos CTest)
ctest --preset testes-msvc-release --output-on-failure
```

Ou compilar via prompt de comando do desenvolvedor (`vcvars64.bat`):

```powershell
cmake -B build/OmniFrame3D -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" -DBUILD_APP=ON
cmake --build build/OmniFrame3D --config Release
```

---

## 4. Padrões de Código e Diretrizes Arquiteturais

1. **Padrão de Linguagem:** C++23 moderno. Utilize `std::expected`, `std::span`, inicialização direta, lambdas e conceitos (`concepts`) onde apropriado.
2. **Separação Rígida de Camadas:**
   - `kernel/`: Núcleo numérico puro em C++23 + Eigen 3.4.0 (`OmniFrameKernel`), estritamente livre de dependências Qt (`analysis/`, `assembly/`, `codes/`, `core/`, `elements/`, `geometry/`, `materials/`, `mesh/`, `postprocessing/`, `solver/`).
   - `core/`: Modelo estrutural analítico, materiais, seções e nós (`OmniFrameCore`, neutro à UI).
   - `solver/`: Ponte de orquestração conectando `StructuralModel` ao `OmniFrameKernel` (`FemSolver3D`).
   - `render/`: Shaders OpenGL 3.3 Core Profile, VBO/VAO/EBO, câmera de órbita 3D, planos de corte, contornos DCR e algoritmos de raycasting.
   - `ui/`: Interface Qt Widgets, docking, inspetores de propriedades editáveis em linha, tabelas interativas e multilayout (`ResultTableConfigurator`), sessão guiada (`GuidedSessionDialog`), configuração de inicialização/logs (`StartupSessionDialog`), alternância de interface (`Customizado`/`Nativo` em `ThemeManager`) e diálogos de engenharia modeless com gerenciamento `Z-Order`.
   - `io/`: Serialização `.omniframe` (JSON Schema `2.1.0`), persistência atômica consolidada (`UserPreferencesStore` v2), recuperação de sessão (`ProjectRecoveryStore`), rotação diária de logs (`AppLogger`), intercâmbio DXF/IFC4 com suporte a caminhos UTF-8 (`Utf8Path.hpp`) e exportação de relatórios em HTML/Markdown/PDF A4.
   - `os/`: Integração Windows 10/11 (`WindowsCliIntegration`) para argumentos UTF-8 no console, registro não administrativo `HKCU` (`PATH` / `App Paths` / `.omniframe`), abertura de terminais CMD/PowerShell e delegação de flags CLI a partir de `OmniFrame.exe`.
   - `cli/`: Executável console nativo `omniframe-cli.exe` integrado ao `OmniFrame.exe`, compartilhando `OmniFrameCore` e `OmniFrameKernel` com os mesmos exemplos, validações e auditoria ABNT NBR.
3. **Gerenciamento de Memória e RAII:** Proibido o uso de ponteiros brutos proprietários (`new`/`delete` manuais). Utilize `std::unique_ptr`, `std::shared_ptr` ou gerenciamento por árvore de objetos `QObject`.
4. **Tratamento de Exceções e Resiliência Numérica:** Condicionamento de matrizes singulares ou mecanismos estruturais devem ser detectados e sinalizados graciosamente ao usuário sem abortar a aplicação.

---

## 5. Processo de Envio de Pull Requests

1. Crie uma branch específica a partir de `main` (`git checkout -b feature/minha-melhoria`).
2. Implemente seu código acompanhado dos devidos testes automatizados em `tests/`.
3. Garanta que o projeto compila sem *warnings* severos e que todos os testes passem.
4. Faça commits com mensagens claras e imperativas (ex: `feat(solver): implementar liberacao de momentos nas extremidades dos elementos`).
5. Abra o Pull Request descrevendo o problema resolvido, referências bibliográficas e capturas de tela quando houver impacto visual na viewport 3D.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse instructions in English</summary><br>

# Contributing to OmniFrame 3D

Thank you for your interest in contributing to **OmniFrame 3D**! Your support helps advance open, transparent, and mathematically rigorous tools for 3D structural finite element analysis.

> [!NOTE]
> **Development Context, Support & External Contributions:**  
> This project is designed, developed, and maintained primarily as a personal and educational initiative by the author, who is a beginner in an ongoing learning process regarding C++ programming and computational structural analysis.  
> Suggestions, *Issues*, and *Pull Requests (PRs)* are always welcome! However, please note that code review, engagement in mathematical/structural topics, and merging of contributions take place at the author's own pace and on a strictly voluntary basis. If an issue or PR takes time to be reviewed or is not merged into the main branch, please understand the time constraints. Under the terms of the GNU AGPLv3 license, you have complete freedom to create an independent *Fork* of this repository and evolve your own ideas and versions.

---

## 1. Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md). Treat all contributors with respect, courtesy, and professionalism.

---

## 2. Ways to Contribute

You can contribute to OmniFrame 3D in several ways:
- **Reporting Bugs:** Identify stiffness matrix discrepancies, boundary condition issues, graphics artifacts, or memory crashes.
- **Formulations & Heuristics:** Propose advanced constitutive material laws (elastoplasticity, damage), contact/interface elements, automated design-code checks, or new cross-section libraries.
- **Improving Documentation:** Enhance mathematical derivations in `docs/architecture.md`, reference verification test cases in `docs/compliance.md`, or document new shortcuts.
- **Sending Pull Requests:** Implement new modeling tools, performance improvements in the sparse Cholesky solver (`SimplicialLDLT`), or refine GLSL shaders.
- **Internationalization (i18n):** Assist in translating and localizing the graphical user interface.

---

## 3. Development Environment Setup

### 3.1. Prerequisites
- **Operating System:** Windows 10 / 11 x64 (compatible with Linux and macOS via cross-compilation)
- **Compiler:** Microsoft Visual C++ (MSVC) with full C++23 support
- **CMake:** Version 3.28 or newer
- **Qt:** Qt 6.11.1 (Modules: `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`)
- **Eigen:** Eigen 3.4.0 (managed automatically via CMake `FetchContent` or installed locally)
- **Build System:** Ninja (highly recommended for accelerated parallel builds)

### 3.2. Building with CMake Presets

We provide pre-configured MSVC presets separated by build in `CMakePresets.json`:

```powershell
# Clone the repository
git clone https://github.com/fernandoncidade/OmniFrame_cpp.git
cd OmniFrame_cpp

# Configure and compile the main application (build/OmniFrame3D)
cmake --preset omniframe-msvc-release
cmake --build --preset omniframe-msvc-release

# Configure and compile separate auxiliary tools and automated tests
cmake --preset version-msvc-release && cmake --build --preset version-msvc-release
cmake --preset compile-language-msvc-release && cmake --build --preset compile-language-msvc-release
cmake --preset testes-msvc-release && cmake --build --preset testes-msvc-release

# Run structural validation suite and unit tests (20 CTest targets)
ctest --preset testes-msvc-release --output-on-failure
```

Or compile via Developer Command Prompt (`vcvars64.bat`):

```powershell
cmake -B build/OmniFrame3D -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" -DBUILD_APP=ON
cmake --build build/OmniFrame3D --config Release
```

---

## 4. Coding Standards & Architectural Guidelines

1. **Language Standard:** Modern C++23. Use `std::expected`, `std::span`, direct initialization, lambdas, and `concepts` where appropriate.
2. **Strict Layer Separation:**
   - `kernel/`: Pure C++23 + Eigen 3.4.0 computational library (`OmniFrameKernel`), strictly Qt-free (`analysis/`, `assembly/`, `codes/`, `core/`, `elements/`, `geometry/`, `materials/`, `mesh/`, `postprocessing/`, `solver/`).
   - `core/`: Analytical structural model, materials, cross-sections, and nodes (`OmniFrameCore`, UI-neutral).
   - `solver/`: Orchestration bridge connecting `StructuralModel` to `OmniFrameKernel` (`FemSolver3D`).
   - `render/`: OpenGL 3.3 Core Profile shaders, VBO/VAO/EBO, 3D orbit camera, clip planes, DCR contours, and raycasting algorithms.
   - `ui/`: Qt Widgets interface, docking, inline-editable property inspectors, interactive multi-layout result tables (`ResultTableConfigurator`), guided session (`GuidedSessionDialog`), startup/log settings (`StartupSessionDialog`), dual UI style mode (`Custom`/`Native` in `ThemeManager`), and modeless engineering dialogs with `Z-Order` management.
   - `io/`: `.omniframe` JSON Schema `2.1.0` serialization, atomic consolidated preference storage (`UserPreferencesStore` v2), session draft recovery (`ProjectRecoveryStore`), daily rotating diagnostic logs (`AppLogger`), UTF-8 path-aware DXF/IFC4 interoperability (`Utf8Path.hpp`), and HTML/Markdown/A4 PDF report export.
   - `os/`: Windows 10/11 integration (`WindowsCliIntegration`) providing UTF-8 console argv handling, non-admin `HKCU` registry integration (`PATH` / `App Paths` / `.omniframe`), CMD/PowerShell session launch, and CLI forwarding from `OmniFrame.exe`.
   - `cli/`: Dedicated native console executable `omniframe-cli.exe` integrated with `OmniFrame.exe`, sharing `OmniFrameCore`, `OmniFrameKernel`, built-in examples, validation, and ABNT NBR auditing.
3. **Memory Management & RAII:** Manual owning raw pointers (`new`/`delete`) are prohibited. Use `std::unique_ptr`, `std::shared_ptr`, or `QObject` parent-child object tree ownership.
4. **Exception Handling & Numerical Resilience:** Singular matrix conditioning or structural mechanisms must be detected and gracefully reported to the user without aborting the application.

---

## 5. Pull Request Submission Process

1. Create a specific branch from `main` (`git checkout -b feature/my-improvement`).
2. Implement your code accompanied by proper automated tests in `tests/`.
3. Verify that the project compiles without severe warnings and that all tests pass.
4. Write commits with clear, imperative messages (e.g., `feat(solver): implement moment releases at member ends`).
5. Open the Pull Request describing the problem solved, bibliographic references, and screenshots whenever visual changes occur in the 3D viewport.

</details>
