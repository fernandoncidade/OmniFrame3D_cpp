# MANUAL DO USUÁRIO E GUIA DE ENGENHARIA FEA 3D — OMNIFRAME 3D
# USER MANUAL AND 3D FEA ENGINEERING GUIDE — OMNIFRAME 3D

**Versão / Version:** `2026.11.15.0`  
**Data / Date:** `15 de novembro de 2027` / `November 15, 2027`  
**Ambiente Tecnológico / Tech Stack:** C++23 | Qt 6.11.1 | Eigen 3.4.0 Sparse Cholesky | OpenGL 3.3 Core Profile  
**Plataforma / Platform:** Windows 11 / Windows 10 x64  
**Autor / Author:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repositório Oficial / Official Repository:** https://github.com/fernandoncidade/OmniFrame_cpp  

---

## Sumário / Table of Contents

- [PARTE 1: MANUAL DE INSTRUÇÕES (PORTUGUÊS - BR)](#parte-1-manual-de-instruções-português---br)
  - [1. Introdução e Formulação Matemática FEA 3D](#1-introdução-e-formulação-matemática-fea-3d)
  - [2. Modelagem Geométrica, Inspetor Lateral, Malhas e Exemplos](#2-modelagem-geométrica-inspetor-lateral-malhas-e-exemplos)
  - [3. Materiais e Seções Transversais Paramétricas](#3-materiais-e-seções-transversais-paramétricas)
  - [4. Condições de Contorno e Vínculos Nodais](#4-condições-de-contorno-e-vínculos-nodais)
  - [5. Carregamentos, Ações Térmicas e Combinações ABNT NBR](#5-carregamentos-ações-térmicas-e-combinações-abnt-nbr)
  - [6. Resolução Assíncrona, Suíte de Solvers e Auditoria NBR](#6-resolução-assíncrona-suíte-de-solvers-e-auditoria-nbr)
  - [7. Pós-Processamento, Polinômios de Hermite, Corte e Animação](#7-pós-processamento-polinômios-de-hermite-corte-e-animação)
  - [8. Viewport 3D Interativa, Raycasting e Deformada](#8-viewport-3d-interativa-raycasting-e-deformada)
  - [9. Serialização (.omniframe 2.1.0), DXF/IFC4 e Relatórios (HTML/MD/PDF)](#9-serialização-omniframe-210-dxfifc4-e-relatórios-htmlmdpdf)
  - [10. Sessão Guiada (F2), CLI, Atalhos, Desinstalação Limpa & FAQ](#10-sessão-guiada-f2-cli-atalhos-desinstalação-limpa--faq)
- [PART 2: OPERATIONS MANUAL & 3D FEA ENGINEERING GUIDE (ENGLISH - US)](#part-2-operations-manual--3d-fea-engineering-guide-english---us)
  - [1. Overview & 3D Finite Element Formulation](#1-overview--3d-finite-element-formulation)
  - [2. Geometric Modeling, Side Inspector, Meshes & Live Examples](#2-geometric-modeling-side-inspector-meshes--live-examples)
  - [3. Materials & Parametric Cross-Sections](#3-materials--parametric-cross-sections)
  - [4. Boundary Conditions & Support Restraints](#4-boundary-conditions--support-restraints)
  - [5. Applied Loads, Thermal Variations & ABNT NBR Combinations](#5-applied-loads-thermal-variations--abnt-nbr-combinations)
  - [6. Asynchronous Execution, Multi-Solver Suite & NBR Audit](#6-asynchronous-execution-multi-solver-suite--nbr-audit)
  - [7. Post-Processing, Hermite Recovery, Clip Plane & Animation](#7-post-processing-hermite-recovery-clip-plane--animation)
  - [8. Interactive 3D Viewport, Raycasting & Deformed Shape](#8-interactive-3d-viewport-raycasting--deformed-shape)
  - [9. Model Persistence (.omniframe 2.1.0), DXF/IFC4 & Reports (HTML/MD/PDF)](#9-model-persistence-omniframe-210-dxfifc4--reports-htmlmdpdf)
  - [10. Guided Session (F2), CLI, Shortcuts, Clean Uninstall & FAQ](#10-guided-session-f2-cli-shortcuts-clean-uninstall--faq)

---

# PARTE 1: MANUAL DE INSTRUÇÕES (PORTUGUÊS - BR)

## 1. Introdução e Formulação Matemática FEA 3D

O **OmniFrame 3D** é uma plataforma de simulação computacional estrutural desenvolvida para engenheiros civis, estruturais, mecânicos e pesquisadores. A aplicação opera de modo 100% autônomo e offline (Local-First), garantindo a soberania e o sigilo de seus cálculos.

### 1.1. Formulação do Elemento Espacial de Viga-Coluna
O programa utiliza o elemento finito tridimensional de Euler-Bernoulli com 12 graus de liberdade por barra (6 graus de liberdade em cada nó extremidade):
$$\{d\} = \begin{Bmatrix} u_x & u_y & u_z & \theta_x & \theta_y & \theta_z \end{Bmatrix}^T$$

A matriz de rigidez local $[k_e]$ ($12 \times 12$) acopla:
- **Resposta Axial:** termos $EA/L$ governando a tração e compressão no eixo baricêntrico local $x'$;
- **Torção Pura de Saint-Venant:** termos $GJ/L$ governando o giro em torno do eixo longitudinal $x'$;
- **Flexão no Plano Local $x'-y'$:** termos cúbicos $12EI_z/L^3$, $6EI_z/L^2$, $4EI_z/L$ e $2EI_z/L$ associados ao esforço cortante $V_y$ e momento fletor $M_z$;
- **Flexão no Plano Local $x'-z'$:** termos cúbicos $12EI_y/L^3$, $6EI_y/L^2$, $4EI_y/L$ e $2EI_y/L$ associados ao esforço cortante $V_z$ e momento fletor $M_y$.

### 1.2. Transformação de Coordenadas Globais e Ângulo de Roll ($\psi$)
A matriz de rotação espacial $[R]$ de dimensões $3 \times 3$ transforma vetores do sistema local para o referencial cartesiano global. O ângulo de rolamento $\psi$ permite girar os eixos principais de inércia da seção transversal em torno do eixo da barra, viabilizando a modelagem de vigas inclinadas, coberturas espaciais complexas e pilares rotacionados.

---

## 2. Modelagem Geométrica, Inspetor Lateral, Malhas e Exemplos

A topologia da estrutura é definida no espaço tridimensional com histórico completo **Desfazer (`Ctrl+Z`) / Refazer (`Ctrl+Y`)** e janelas filhas **Modeless** (que abrem no primeiro clique sem janelas fantasmas e mantêm gerenciamento ativo de **Z-Order**):
1. **Nós Estruturais:** Criados clicando no Plano Ativo (`N`) ou via coordenadas cartesianas $(X, Y, Z)$ em metros no painel esquerdo. Cada nó recebe um identificador numérico único.
2. **Barras (Elementos):** Conectam um nó de origem $i$ a um nó de destino $j$ (`B`). O comprimento euclidiano $L = \sqrt{(X_j - X_i)^2 + (Y_j - Y_i)^2 + (Z_j - Z_i)^2}$ é calculado automaticamente.
3. **Rótulas nas Extremidades (Releases):** É possível liberar os momentos fletores e de torção nas extremidades inicial ou final, convertendo a barra em um elemento de treliça articulada.
4. **Inspetor Lateral (`Elemento / Nó Selecionado`) & Reedição dos 3 Exemplos:** Todos os **3 exemplos pré-definidos** abrem por padrão no plano de trabalho **`XY (Planta)`** e permanecem 100% abertos para reedição, testes paramétricos e recálculo como exercício; ao selecionar nós ou barras, o inspetor libera edição direta de coordenadas/incrementos $\Delta$, atalhos de seleção em lote (**`Mesmo Material`** / **`Mesma Seção`**) e listas suspensas (**`Material:`** e **`Seção Transversal:`**) exibindo nomes limpos sem prefixos `#ID`.
5. **Gerador Paramétrico de Malha (`Estrutura > Gerador de Malha 1D/2D/3D...` / `Malha 2D/3D...`):** Cria malhas `1D Linha Refinada (Line2)`, cascas (`Quad4`, `Quad8`, `Tri3`, `Polígono Delaunay (Tri3)`) e sólidos (`Hex8`, `Hex20`, `Tet4`, `Extrusão 2D -> 3D Hex8`) com configuração integrada de **`Material da Malha:`**, **`Espessura (Casca 2D):`**, **`Condição de Contorno:`**, **`Pressão / Carga Z (2D/3D):`**, acoplamento automático em nós coincidentes e resolução unificada 1D/2D/3D via **`Calcular Estrutura (F5)`**.

---

## 3. Materiais e Seções Transversais Paramétricas

### 3.1. Materiais Isotrópicos
O software calcula a matriz de elasticidade a partir dos parâmetros:
- Módulo de Elasticidade Longitudinal ($E$);
- Coeficiente de Poisson ($\nu$);
- Módulo de Elasticidade Transversal ($G = \frac{E}{2(1+\nu)}$);
- Massa Específica / Densidade ($\rho$).
O catálogo inclui **Aço Estrutural ASTM A36 / MR250** ($E = 200\text{ GPa}$), **Concreto Armado C25/C30** ($E = 30\text{ GPa}$), **Alumínio 6061-T6** ($E = 70\text{ GPa}$) e **Madeira Estrutural C30**.

### 3.2. Biblioteca de Seções Paramétricas
- **Retangular Maciça:** $A = b \cdot h$, $I_z = \frac{b h^3}{12}$, $I_y = \frac{h b^3}{12}$, $J \approx \beta b^3 h$;
- **Circular Maciça:** $A = \frac{\pi d^2}{4}$, $I_z = I_y = \frac{\pi d^4}{64}$, $J = \frac{\pi d^4}{32}$;
- **Tubo Circular:** $A = \pi (R_{ext}^2 - R_{int}^2)$, $I = \frac{\pi (R_{ext}^4 - R_{int}^4)}{4}$, $J = 2 I$;
- **Perfil Metálico I / W:** Cálculo analítico rigoroso de mesas e alma com inércias em torno dos eixos forte e fraco;
- **Genérica / Customizada:** Inserção direta de propriedades geométricas obtidas de tabelas de fabricantes.

---

## 4. Condições de Contorno e Vínculos Nodais

Os nós de suporte podem ter qualquer combinação de seus 6 graus de liberdade vinculada:
- **Engaste Perfeito:** $u_x = u_y = u_z = \theta_x = \theta_y = \theta_z = 0$;
- **Apoio Fixo (2º Gênero):** $u_x = u_y = u_z = 0$ ($\theta$ livres);
- **Apoio Móvel (1º Gênero):** $u_z = 0$ (translações horizontais e rotações livres);
- **Restrição Personalizada:** Seleção individual de cada DOF restrito, apoios com molas elásticas e deslocamentos prescritos.

---

## 5. Carregamentos, Ações Térmicas e Combinações ABNT NBR

O OmniFrame 3D suporta carregamentos nodais, distribuídos, térmicos e combinações normativas:
- **Ações Nodais:** Forças pontuais $(F_x, F_y, F_z)$ e momentos concentrados $(M_x, M_y, M_z)$ aplicados diretamente sobre os nós.
- **Cargas Distribuídas em Barras:** Carregamento uniforme $q$ (sistema Global ou Local) integrado através das funções cúbicas de forma, transferindo automaticamente o vetor consistente de forças nodais equivalentes para os nós extremos da barra:
  $$\{F_{eq}\} = \int_0^L [N(x')]^T q(x') \, dx'$$
- **Cargas Térmicas Uniformes e Gradientes (`Térmica (ΔT)...`):** Aplica variação uniforme $\Delta T$ (°C) e gradientes transversais convertidos em esforços equivalentes de engastamento perfeito via coeficiente $\alpha$ do material.
- **Casos e Combinações NBR (`Combinações...`):** Organiza casos ($G, Q, W, T, E$), gera automaticamente combinações ELU/ELS (`NBR 6118 / NBR 8800`) e oferece assistentes para ações de vento (**ABNT NBR 6123:2023**) e sobrecargas (**ABNT NBR 6120:2019**).

---

## 6. Resolução Assíncrona, Suíte de Solvers e Auditoria NBR

Ao pressionar **F5** (execução assíncrona em *worker thread* sem bloquear a interface) ou via **Suíte de Análises Avançadas (`Avançadas...`)**:
1. A matriz global $[K]$ é montada no formato esparso (`Eigen::SparseMatrix<double>`) dentro do núcleo puro `OmniFrameKernel`.
2. O usuário pode selecionar `Auto`, `SimplicialLDLT` (SPD Cholesky), `SparseLU (COLAMD)` ou `ConjugateGradient (PCG)` no seletor **`Solver:`** da suíte (com fallback explícito rastreável), além de análises de **Combinações & Envoltória**, **Modal**, **Flambagem Linear** ($\lambda_{cr}$, com campo **`Imperfeição Inicial (amp):`** e botão **`Aplicar Imperfeição à Geometria`**), **Harmônica FRF**, **Espectro de Resposta (`NBR 15421`)**, **Sísmica / Transiente** (`Newmark-Beta (γ=0,50, β=0,25)` ou `HHT-Alpha (α=-0,05)`, com opção **`Acelerograma Sísmico (a_g)`** ou **`Histórico de Força F(t)`**), **P-Delta** e **Não Linear** (`Corrotacional 3D`, **`Tangente Completa`**, **`Line Search`**, Newton-Raphson / Arc-Length e **Plasticidade J2** $\sigma_y, H_{iso}, H_{kin}$).
3. Condições de contorno, molas elásticas e deslocamentos prescritos são aplicados preservando o cálculo exato das reações de apoio $\{R\} = [K] \{U\} - \{F\}$ e detectando nós/mecanismos singulares.
4. **Auditoria Normativa ABNT NBR (`Auditoria NBR...`):** Verifica critérios das normas `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022` e `8800:2024` (`LocalPdfVerified`), sinalizando explicitamente normas fora do pacote PDF local (`OutOfLocalPdfScope`), com projeção do **Mapa de Calor DCR 3D** diretamente na viewport e exportação do Memorial PDF A4.

---

## 7. Pós-Processamento, Polinômios de Hermite, Corte e Animação

A partir dos deslocamentos nodais calculados, os esforços internos em qualquer seção transversal $x'$ ao longo da barra são avaliados analiticamente por interpolação de Hermite:
- **Esforço Normal $N(x')$:** $E A \frac{du}{dx'}$;
- **Momento Fletor $M_z(x')$:** $E I_z \frac{d^2 v}{dx'^2}$;
- **Esforço Cortante $V_y(x')$:** $-\frac{d M_z}{dx'}$;
- **Momento de Torção $T(x')$:** $G J \frac{d\theta_x}{dx'}$.
- **Controles 3D Avançados (`Corte/Contornos...`) & Aba Contínua 2D/3D:** Permite alternar mapas de contorno (`|u|`, **`Tensão de von Mises (Casca/Sólido)`**, `Axial`, `DCR`), exibir/ocultar malha superficial 2D/3D, fatiar o modelo com **Plano de Corte 3D** nos eixos `X`, `Y` ou `Z` (com inversão), reproduzir **Animação Contínua 3D** (deformada, modal, flambagem ou harmônica/transiente) e consultar tensões/resultantes na aba **`Esforços 2D/3D (Casca/Sólido)`** do painel inferior.

---

## 8. Viewport 3D Interativa, Raycasting e Deformada

A viewport OpenGL 3.3 Core Profile oferece renderização de última geração:
- **Navegação 3D:**
  - **Girar / Orbitar:** Botão direito (ou do meio) do mouse pressionado e arrastado.
  - **Transladar (Pan):** `Shift` + botão direito (ou do meio) do mouse pressionado e arrastado.
  - **Aproximar / Afastar (Zoom):** Roda do mouse (*scroll*).
  - **Botão esquerdo do mouse:** Executa a ferramenta ativa na barra superior (`Selecionar`, `Nó`, `Barra`, `Apoio`, etc.).
- **Seleção Espacial por Raycasting:** Clique diretamente com o botão esquerdo (modo `Selecionar`) em nós ou barras na cena 3D para selecionar a entidade e inspecionar suas propriedades.
- **Visualização da Deformada:** Utilize a barra de escala de diagramas e deformada (intervalo de `1` a `100`, padrão `30`, correspondente a ampliação de `1,5x` a `150x` na deformada e escala proporcional `val / 30.0` nos diagramas) sobreposta à geometria indeformada.
- **Gradientes de Cor:** Escolha no seletor de diagramas entre deslocamentos e esforços internos (normal, cortante, torção e momentos fletores) para identificar visualmente o comportamento estrutural.

---

## 9. Serialização (.omniframe 2.1.0), DXF/IFC4 e Relatórios (HTML/MD/PDF)

- **Formato `.omniframe` (Schema `2.1.0`, retrocompatível com `1.0` e `2.0`):** Estrutura JSON auditável (UTF-8) contendo materiais (incluindo $\rho$ e plasticidade J2), seções, nós, barras, apoios, molas, deslocamentos prescritos, cargas e combinações (`LoadSystem`).
- **Interoperabilidade & Relatórios:** Importação/exportação **DXF** e **IFC4**, auditoria normativa **ABNT NBR** (`LocalPdfVerified`: `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; `OutOfLocalPdfScope` explícito para normas ausentes como `NBR 8681`, `14762`, `15421`, `AISC 360`, `Eurocode 3`) e geração de relatórios em **Markdown**, **HTML** e **PDF executivo A4** configurável.

---

## 10. Sessão Guiada (`F2`), CLI, Atalhos, Desinstalação Limpa & FAQ

### Sessão Guiada Interativa (`Ajuda > Sessão Guiada...`, atalho `F2`):
- Demonstra automaticamente, em **30 etapas do zero**, todos os recursos do aplicativo a partir de qualquer um dos **3 exemplos pré-definidos** escolhidos pelo usuário.
- **4 Perfis Científicos de Ritmo de Leitura (PPM):** `Leitor Típico — Fluente (250 PPM)`, `Leitura Técnica / Engenharia (150 PPM — recomendado)`, `Leitura em Voz Alta (155 PPM)` e `Dislexia / Acessível (130 PPM + margem para regressões)`.
- **Callout e Destaque Unificados em 2 Fases (Sem Sobreposição):** Antes de cada ação, um destaque luminoso e um único balão explicativo em **amarelo** detalham o recurso no ritmo proporcional às palavras/PPM; na execução (**verde**), o aplicativo posiciona eventuais janelas filhas no canto inferior esquerdo mantendo o balão legível e livre de sobreposição, com suporte completo a **Temas Escuro/Claro**, **Modeless/Z-Order** e **restauração automática do exemplo selecionado para o estado inicial** ao concluir.

### CLI Dedicada e Integração com Windows 10/11 (`omniframe-cli.exe` e `OmniFrame.exe`):

O `omniframe-cli.exe` é o executável oficial de linha de comando (*headless*, `/SUBSYSTEM:CONSOLE`), compilado em conjunto na mesma pasta de `OmniFrame.exe` com compartilhamento integral de `OmniFrameCore` e `OmniFrameKernel` — produzindo resultados FEA 3D, validações de malha, auditorias ABNT NBR e exportações rigorosamente idênticos aos da interface gráfica, sem abrir nenhuma janela.

#### Para que serve e como acessar no Windows 10/11 (CMD e PowerShell):
- **Propósito:** automação em lote (`.bat`, `.cmd`, `.ps1`), validação offline de projetos (`.omniframe`, `.dxf`, `.ifc`), varreduras paramétricas de não linearidade/imperfeição/dinâmica, auditorias normativas ABNT em CI/CD, geração de conjuntos VTK/CSV/DXF/IFC4 e emissão automatizada de memoriais de cálculo (MD/HTML/PDF).
- **Menu Gráfico Integrado (zero digitação):** em `OmniFrame.exe`, abra **`Configurações > CLI Windows (CMD / PowerShell)`** para registrar no Windows sem administrador (`PATH` + `App Paths` + `.omniframe`), remover o registro, verificar o status ou abrir imediatamente um terminal **CMD** ou **PowerShell** já pronto com `omniframe-cli`.
- **Auto-registro HKCU via CLI (sem administrador):** execute `omniframe-cli --install-windows-integration` (registra `HKCU\Environment\Path`, `App Paths` para `omniframe-cli.exe` / `OmniFrame.exe` / `omniframe.exe` e a associação `.omniframe` -> `OmniFrame3D.Project` com broadcast `WM_SETTINGCHANGE`/`SHChangeNotify`).
- **Script PowerShell Portátil (`register_cli_windows.ps1`):** presente em `cmake/register_cli_windows.ps1` e na raiz das distribuições limpas (suporta instalação padrão, `-CheckOnly` e `-Uninstall`).
- **Instalador Desktop (Inno Setup) & MSIX (Microsoft Store):** o instalador Inno Setup (`repo/OmniFrame.iss`) oferece as tarefas `addtopath` e `assocproj` marcadas por padrão; o pacote MSIX (`repo/AppxManifest.xml`) registra `uap5:AppExecutionAlias` oficiais para `omniframe.exe` e `omniframe-cli.exe` (`desktop4:Subsystem="console"`).
- **Integração Direta no `OmniFrame.exe`:**
  - Abrir modelo na GUI: `OmniFrame.exe "caminho\modelo.omniframe|.dxf|.ifc"` inicia a interface 3D já com o arquivo carregado.
  - Delegação transparente para CLI: passar flags headless ao `OmniFrame.exe` (`--cli`, `--version`, `-v`, `--help`, `-h`, `--list-examples`, `--validate`, `--analyze`, `--example`, `--nbr-audit`, `--audit-nbr`, `--check-windows-integration`, `--install-windows-integration`, `--uninstall-windows-integration`) delega de forma síncrona ao `omniframe-cli.exe` irmão (no CMD/PowerShell prefira chamar `omniframe-cli` diretamente, ou use `| Out-String` no PowerShell se invocar `OmniFrame.exe` em modo CLI).
- **Suporte UTF-8 no CMD e PowerShell:** execute `chcp 65001 >nul` no CMD ou `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8` no PowerShell para preservar acentos em JSON/texto; os leitores e gravadores (`CommandLineToArgvW`, DXF, IFC4, CSV, VTK, JSON, HTML, MD, PDF) suportam caminhos UTF-8 com espaços e acentos entre aspas duplas.

#### Referência Completa de Comandos e Opções (`omniframe-cli.exe`):

| Flag / Comando | Argumentos | Descrição |
|---|---|---|
| `--version` / `-v` | — | Exibe a versão da CLI, padrão C++23, Eigen 3.4.0 e schema `.omniframe` e sai (`0`). |
| `--help` / `-h` | — | Imprime a ajuda completa de uso no Windows 10/11 (CMD e PowerShell) e sai (`0`). |
| `--list-examples` | — | Lista os 3 modelos embutidos (`1 \| building`, `2 \| cantilever`, `3 \| truss`) em texto ou `--json`/`--output`. |
| `--example` | `<nome\|1\|2\|3>` | Seleciona modelo embutido sem arquivo em disco (aceita `1`/`building`/`portico`/`edificio`/`frame`, `2`/`cantilever`/`balanco`/`lframe`, `3`/`truss`/`trelica`/`passarela`). |
| `--validate` | `[<arquivo.omniframe\|dxf\|ifc>]` | Valida topologia, conectividade em grafo (BFS), finitude, apoios e qualidade de malhas 2D/3D (`0` válido, `2` inválido). |
| `--analyze` | `[<arquivo.omniframe\|dxf\|ifc>]` | Executa a análise FEA 3D configurada sobre o arquivo ou sobre `--example` (também acionado automaticamente com `--output`, `--export` ou `--report`). |
| `--nbr-audit` / `--audit-nbr` | `[<arquivo>]` | Executa auditoria normativa ABNT NBR (6118, 8800, 6120, 6122, 6123, 7480), resolvendo estática prévia se necessário, e preenche `nbrAudit` no JSON. |
| `--analysis` | `static\|modal\|buckling\|nonlinear\|pdelta\|harmonic\|spectrum\|transient\|seismic` | Define o tipo de análise estrutural (padrão `static`). |
| `--solver` | `auto\|ldlt\|llt\|sparselu\|cg\|bicgstab` | Seleciona o solver linear de `LinearSolverSuite` (padrão `auto`, registra `usedFallback`). |
| `--modes` | `<N>` | Número de autovalores/modos em `modal`, `buckling` ou `spectrum` (padrão `6`). |
| `--steps` | `<N>` | Passos incrementais em `nonlinear`/`pdelta` ou passos temporais em `transient`/`seismic` (padrão `20`). |
| `--dt` | `<segundos>` | Incremento de tempo em segundos para `transient`/`seismic` (mínimo `1e-5`, padrão `0.02`). |
| `--scheme` | `newmark\|hht` | Integrador temporal implícito para `transient`/`seismic`: Newmark-beta ou HHT-alpha (padrão `newmark`). |
| `--case` | `<id>` | ID inteiro do Caso de Carga (`LoadCase`) em `static` ou `transient`. |
| `--combo` | `<id>` | ID inteiro da Combinação ABNT (`LoadCombination`) em `static`. |
| `--method` | `nr\|arclength` | Estratégia não linear incremental-iterativa: Newton-Raphson (`nr`, padrão) ou Arc-Length Crisfield (`arclength`). |
| `--load-factor` | `<fator>` | Multiplicador alvo de carregamento para análises `nonlinear`/`pdelta` (padrão `1.0`). |
| `--full-tangent` | — | Atualiza a matriz de rigidez tangente global `Kt` a cada iteração Newton-Raphson (padrão). |
| `--modified-newton` | — | Mantém `Kt` constante por incremento de carga (Newton Modificado). |
| `--line-search` | — | Habilita busca linear com retrocesso (backtracking) em Newton-Raphson (padrão). |
| `--no-line-search` | — | Desabilita busca linear em Newton-Raphson. |
| `--imperfection` | `<amp_m>` | Pré-calcula flambagem linear e perturba a geometria inicial na amplitude (em metros) antes de resolver/exportar. |
| `--imperfection-mode` | `<N>` | Índice base `1` do modo de flambagem aplicado em `--imperfection` (padrão `1`). |
| `--export` | `<json\|csv\|vtk\|dxf\|ifc> <caminho>` | Exporta modelo/resultados (repetível): `.vtk` `UNSTRUCTURED_GRID` (`von_mises_kPa`), `.csv` (nodal + tensões contínuas), `.json`, `.dxf`, `.ifc`. |
| `--report` | `<md\|html\|pdf> <caminho>` | Gera memorial técnico em Markdown, HTML5 ou PDF A4 (repetível; integra estática, modal, flambagem, não linear e `nbrAudit`). |
| `--output` | `<caminho.json>` | Grava o resumo JSON estruturado completo em disco. |
| `--json` | — | Emite o resumo JSON estruturado completo diretamente no `stdout`. |
| `--check-windows-integration` | — | Inspeciona `PATH` do usuário/sistema/processo, `App Paths` e associação `.omniframe` no Windows 10/11. |
| `--install-windows-integration` | — | Registra `omniframe-cli.exe`, `OmniFrame.exe` e `.omniframe` no `HKCU` sem necessidade de privilégios de administrador. |
| `--uninstall-windows-integration` | — | Remove do registro `HKCU` as chaves e entradas de `PATH` criadas pela integração Windows da CLI. |

#### Códigos de Retorno (`%ERRORLEVEL%` no CMD / `$LASTEXITCODE` no PowerShell):
- `0`: execução, validação, análise, auditoria NBR ou registro no Windows concluídos com êxito.
- `1`: erro de sintaxe nos argumentos, arquivo ilegível/inexistente, formato desconhecido ou falha de gravação/registro Windows.
- `2`: falha na validação estrutural (`--validate`) ou falha numérica/física (mecanismo, singularidade, falta de modo de flambagem positivo em `--imperfection` ou não convergência).

#### Exemplos Práticos (CMD e PowerShell):
```bat
:: Windows Command Prompt (CMD)
chcp 65001 >nul
omniframe-cli --install-windows-integration --json
omniframe-cli --example building --nbr-audit --report pdf "C:\Projetos\memorial_edificio.pdf" --export vtk "C:\Projetos\edificio.vtk" --json
omniframe-cli --validate "C:\Projetos\Pórtico São Paulo\modelo_técnico.omniframe"
OmniFrame.exe "C:\Projetos\Pórtico São Paulo\modelo_técnico.omniframe"
```
```powershell
# Windows PowerShell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$res = omniframe-cli --example building --analysis nonlinear --steps 10 --load-factor 1.25 --modified-newton --no-line-search --imperfection 0.005 --imperfection-mode 1 --json | ConvertFrom-Json
if ($LASTEXITCODE -eq 0 -and $res.success) { $res.appliedImperfection }
omniframe-cli --example cantilever --analysis transient --scheme hht --dt 0.01 --steps 50 --export csv ".\saida_hht.csv" --json | ConvertFrom-Json
OmniFrame.exe --example truss --analysis buckling --modes 4 --json | Out-String
```

Consulte também [docs/cli-reference.md](docs/cli-reference.md) e o Capítulo 10 do Manual interativo embutido (`F1`).

### Atalhos Úteis:
- `F1`: Abrir Manual Completo (`ManualDialog`)
- `F2`: Abrir Sessão Guiada Interativa (`GuidedSessionDialog`)
- `Ctrl+N`: Novo Projeto
- `Ctrl+O`: Abrir Arquivo `.omniframe`
- `Ctrl+S`: Salvar Projeto
- `Ctrl+Z` / `Ctrl+Y`: Desfazer / Refazer edições e alterações no modelo
- `Ctrl+Q`: Sair do Aplicativo
- `F5`: Executar Solver FEA (`Calcular`)
- `F`: Enquadrar Estrutura na Câmera (`Zoom All`)
- `S` / `N` / `B` / `M`: Ferramentas `Selecionar` / `Nó` / `Barra` / `Medir` (`S` / `N` / `B` / `M`)
- `Del`: Excluir Entidade Selecionada
- `Esc`: Cancelar Operação / Voltar à Ferramenta `Selecionar`

### Desinstalação Limpa (Política Microsoft Store 10.2.7):
- A desinstalação via Configurações do Windows / Microsoft Store ou pelo desinstalador do Inno Setup remove os arquivos do programa sem deixar serviços em segundo plano. As preferências locais ficam em `%LOCALAPPDATA%\OmniFrame\OmniFrame\` (`language.json`, `theme.json`, `graphics.json`), além dos projetos `.omniframe` e relatórios salvos manualmente pelo usuário.

---

# PART 2: OPERATIONS MANUAL & 3D FEA ENGINEERING GUIDE (ENGLISH - US)

## 1. Overview & 3D Finite Element Formulation

**OmniFrame 3D** is a computational structural simulation platform engineered for civil, structural, and mechanical engineers and researchers. The application operates 100% autonomously and offline (Local-First), guaranteeing sovereignty and confidentiality for your calculations.

### 1.1. 3D Euler-Bernoulli Space Frame Element
The program employs the three-dimensional Euler-Bernoulli finite element with 12 degrees of freedom per member (6 degrees of freedom at each end node):
$$\{d\} = \begin{Bmatrix} u_x & u_y & u_z & \theta_x & \theta_y & \theta_z \end{Bmatrix}^T$$

The local stiffness matrix $[k_e]$ ($12 \times 12$) couples:
- **Axial Response:** terms $EA/L$ governing tension and compression along the local centroidal axis $x'$;
- **Saint-Venant Pure Torsion:** terms $GJ/L$ governing twist about the longitudinal axis $x'$;
- **Bending in Local Plane $x'-y'$:** cubic terms $12EI_z/L^3$, $6EI_z/L^2$, $4EI_z/L$, and $2EI_z/L$ associated with shear force $V_y$ and bending moment $M_z$;
- **Bending in Local Plane $x'-z'$:** cubic terms $12EI_y/L^3$, $6EI_y/L^2$, $4EI_y/L$, and $2EI_y/L$ associated with shear force $V_z$ and bending moment $M_y$.

### 1.2. Spatial Coordinate Transformation & Roll Angle ($\psi$)
The $3 \times 3$ spatial rotation matrix $[R]$ transforms vectors from the local system to the global Cartesian frame. The roll angle $\psi$ allows rotating the cross-section principal axes of inertia around the member axis, enabling the modeling of inclined beams, complex space frames, and rotated columns.

---

## 2. Geometric Modeling, Side Inspector, Meshes & Live Examples

Structural topology is defined in three-dimensional space with full multi-level **Undo (`Ctrl+Z`) / Redo (`Ctrl+Y`)** history and **Modeless** child dialogs (opening on a single click without ghost outlines while preserving active **Z-Order** stacking):
1. **Structural Nodes:** Created by clicking the Active Plane (`N`) or entering Cartesian coordinates $(X, Y, Z)$ in meters in the left dock. Each node receives a unique numeric identifier.
2. **Members (Elements):** Connect a start node $i$ to an end node $j$ (`B`). Euclidean length $L = \sqrt{(X_j - X_i)^2 + (Y_j - Y_i)^2 + (Z_j - Z_i)^2}$ is computed automatically.
3. **End Releases (Hinges):** Bending and torsional moments may be released at the start or end node, converting the member into an articulated truss element.
4. **Side Property Inspector (`Selected Node / Member`) & Built-in Example Re-Editing:** All **3 built-in examples** open in the **`XY (Plan)`** work plane by default and remain 100% open to live edits, parametric trials, and re-solving as hands-on practice; selecting nodes or members unlocks inline coordinate/offset ($\Delta$) controls, batch selection buttons (**`Same Material`** / **`Same Section`**), and clean **`Material:`** / **`Cross-Section:`** dropdown comboboxes without `#ID` prefixes.
5. **Parametric Mesh Generator (`Structure > 1D/2D/3D Mesh Generator...` / `2D/3D Mesh...`):** Builds `1D Refined Line (Line2)`, shell (`Quad4`, `Quad8`, `Tri3`, `Delaunay Polygon (Tri3)`), and solid (`Hex8`, `Hex20`, `Tet4`, `Extrusion 2D -> 3D Hex8`) meshes with integrated **`Mesh Material:`**, **`Thickness (2D Shell):`**, **`Boundary Condition:`**, and **`Pressure / Z Load (2D/3D):`**, automatic coincident-node coupling, and unified 1D/2D/3D solving via **`Solve Structure (F5)`**.

---

## 3. Materials & Parametric Cross-Sections

### 3.1. Isotropic Materials
The software computes the elasticity matrix from physical parameters:
- Modulus of Elasticity ($E$);
- Poisson's Ratio ($\nu$);
- Shear Modulus ($G = \frac{E}{2(1+\nu)}$);
- Specific Weight / Density ($\rho$).
The catalog includes **Structural Steel ASTM A36 / MR250** ($E = 200\text{ GPa}$), **Reinforced Concrete C25/C30** ($E = 30\text{ GPa}$), **Aluminum 6061-T6** ($E = 70\text{ GPa}$), and **Structural Timber C30**.

### 3.2. Parametric Cross-Section Library
- **Solid Rectangular:** $A = b \cdot h$, $I_z = \frac{b h^3}{12}$, $I_y = \frac{h b^3}{12}$, $J \approx \beta b^3 h$;
- **Solid Circular:** $A = \frac{\pi d^2}{4}$, $I_z = I_y = \frac{\pi d^4}{64}$, $J = \frac{\pi d^4}{32}$;
- **Circular Pipe:** $A = \pi (R_{ext}^2 - R_{int}^2)$, $I = \frac{\pi (R_{ext}^4 - R_{int}^4)}{4}$, $J = 2 I$;
- **Wide-Flange I / W Profile:** Rigorous analytical calculation of flanges and web with moments of inertia about strong and weak axes;
- **Generic / Custom:** Direct entry of geometric properties obtained from manufacturer catalogs.

---

## 4. Boundary Conditions & Support Restraints

Support nodes may constrain any combination of their 6 degrees of freedom:
- **Fixed Support:** $u_x = u_y = u_z = \theta_x = \theta_y = \theta_z = 0$;
- **Pinned Support (2nd Order):** $u_x = u_y = u_z = 0$ ($\theta$ free);
- **Roller Support (1st Order):** $u_z = 0$ (horizontal translations and rotations free);
- **Custom Restraint:** Individual selection of each restrained DOF, elastic spring supports, and prescribed displacements.

---

## 5. Applied Loads, Thermal Variations & ABNT NBR Combinations

OmniFrame 3D supports nodal, distributed, thermal, and code-combination loading:
- **Nodal Actions:** Concentrated forces $(F_x, F_y, F_z)$ and concentrated moments $(M_x, M_y, M_z)$ applied directly to nodes.
- **Distributed Member Loads:** Uniform distributed load $q$ (Global or Local axes) integrated via cubic shape functions, automatically transferring the consistent equivalent nodal force vector to the member end nodes:
  $$\{F_{eq}\} = \int_0^L [N(x')]^T q(x') \, dx'$$
- **Uniform & Gradient Thermal Loads (`Thermal (ΔT)...`):** Applies uniform temperature shifts $\Delta T$ (°C) and transverse gradients converted into equivalent fixed-end forces via material thermal expansion $\alpha$.
- **Load Cases & NBR Combinations (`Combinations...`):** Organizes cases ($G, Q, W, T, E$), auto-generates ULS/SLS combinations (`NBR 6118 / NBR 8800`), and offers **ABNT NBR 6123:2023** wind and **ABNT NBR 6120:2019** occupancy live-load assistants.

---

## 6. Asynchronous Execution, Multi-Solver Suite & NBR Audit

Upon pressing **F5** (running asynchronously on a worker thread without blocking the UI) or via **Advanced Analysis Suite (`Advanced...`)**:
1. The global matrix $[K]$ is assembled in Compressed Column Storage (`Eigen::SparseMatrix<double>`) inside the Qt-free `OmniFrameKernel`.
2. Users can pick `Auto`, `SimplicialLDLT (SPD Cholesky)`, `SparseLU (COLAMD)`, or `ConjugateGradient (PCG)` from the suite **`Solver:`** selector (with tracked explicit fallback), alongside **Combinations & Envelope**, **Modal**, **Linear Buckling** ($\lambda_{cr}$, with **`Initial Imperfection (amp):`** and **`Apply Imperfection to Geometry`**), **Harmonic FRF**, **Response Spectrum (`NBR 15421`)**, **Seismic / Transient** (`Newmark-Beta` or `HHT-Alpha (α=-0.05)` with **`Seismic Accelerogram (a_g)`** or **`Force Time History F(t)`**), **P-Delta**, and **Nonlinear** analyses (`Corotational 3D`, **`Full Tangent`**, **`Line Search`**, Newton-Raphson / Arc-Length, and **J2 Plasticity** $\sigma_y, H_{iso}, H_{kin}$).
3. Boundary conditions, elastic springs, and prescribed displacements are applied while recovering exact support reactions $\{R\} = [K] \{U\} - \{F\}$ and flagging singular nodes/mechanisms.
4. **ABNT NBR Code Compliance Audit (`NBR Audit...`):** Runs local PDF-verified checks (`NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024` as `LocalPdfVerified`), explicitly marks absent standards as `OutOfLocalPdfScope`, projects the **3D DCR Heatmap** directly onto the viewport, and exports executive A4 PDF reports.

---

## 7. Post-Processing, Hermite Recovery, Clip Plane & Animation

From calculated nodal displacements, internal forces at any cross-section $x'$ along the member are evaluated analytically via Hermite interpolation:
- **Axial Force $N(x')$:** $E A \frac{du}{dx'}$;
- **Bending Moment $M_z(x')$:** $E I_z \frac{d^2 v}{dx'^2}$;
- **Shear Force $V_y(x')$:** $-\frac{d M_z}{dx'}$;
- **Torsional Moment $T(x')$:** $G J \frac{d\theta_x}{dx'}$.
- **Advanced 3D Controls (`Clip/Contours...`) & 2D/3D Continuum Tab:** Toggles surface/element contour maps (`|u|`, **`von Mises Stress (Shell/Solid)`**, `Axial`, `DCR`), shows/hides 2D/3D meshes, slices the model with interactive **3D Clip Planes (`X`, `Y`, `Z` with invert)**, plays real-time **Continuous 3D Animations** (deformed, modal, buckling, harmonic, or transient responses), and lists continuum stresses/resultants inside the **`2D/3D Forces (Shell/Solid)`** bottom results tab.

---

## 8. Interactive 3D Viewport, Raycasting & Deformed Shape

The OpenGL 3.3 Core Profile viewport delivers state-of-the-art rendering:
- **3D Navigation:**
  - **Rotate / Orbit:** Press and drag the right (or middle) mouse button.
  - **Pan Lateral:** `Shift` + right (or middle) mouse button pressed and dragged.
  - **Zoom In / Out:** Mouse scroll wheel.
  - **Left mouse button:** Executes the active tool from the top toolbar (`Select`, `Node`, `Member`, `Support`, etc.).
- **3D Raycast Entity Picking:** Left-click directly (`Select` mode) on nodes or members in the 3D scene to select the entity and inspect its properties.
- **Deformed Shape Visualization:** Use the diagram and deformed scale bar (range `1` to `100`, default `30`, mapping to `1.5x`–`150x` deformed amplification and `val / 30.0` diagram scale) overlaid on the undeformed geometry.
- **Color Gradients:** Choose in the diagram selector between displacements and internal forces (axial, shear, torsion, and bending moments) to inspect structural behavior visually.

---

## 9. Model Persistence (.omniframe 2.1.0), DXF/IFC4 & Reports (HTML/MD/PDF)

- **`.omniframe` Format (Schema `2.1.0`, backward-compatible with `1.0` and `2.0`):** Auditable UTF-8 JSON structure storing materials (including $\rho$ and J2 plasticity), sections, nodes, members, supports, springs, prescribed displacements, loads, and combinations (`LoadSystem`).
- **Interoperability & Reports:** Bidirectional **DXF** and **IFC4** exchange, **ABNT NBR** code checks (`LocalPdfVerified`: `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; explicit `OutOfLocalPdfScope` flag for absent standards such as `NBR 8681`, `14762`, `15421`, `AISC 360`, `Eurocode 3`), and configurable native **Markdown**, **HTML**, and **A4 executive PDF** report generation.

---

## 10. Guided Session (`F2`), CLI, Shortcuts, Clean Uninstall & FAQ

### Interactive Guided Session (`Help > Guided Session...`, shortcut `F2`):
- Automatically walks through **30 steps from scratch**, demonstrating every tool, inspector control, and engineering dialog starting from any user-selected **built-in structural example** (`Example 1`, `2`, or `3`).
- **4 Scientific Cognitive Reading Speed Profiles (WPM):** `Fluent Typical Reader (250 WPM)`, `Technical / Engineering Reading (150 WPM — recommended)`, `Read Aloud Pace (155 WPM)`, and `Dyslexia / Accessible (130 WPM + regression margin)`.
- **Unified Two-Phase Spotlight & Callout (Zero Overlap):** Before executing an action, a unified highlight and **yellow** explanatory callout balloon introduce the control proportionally to word count and WPM; during execution (**green**), auxiliary child windows dock neatly at the lower-left without overlapping the callout balloon, supporting **Dark/Light themes**, **Modeless/Z-Order**, and **automatic restoration of the chosen example to its initial state** when finished.

### Dedicated CLI & Windows 10/11 Integration (`omniframe-cli.exe` and `OmniFrame.exe`):

`omniframe-cli.exe` is the official headless command-line executable (`/SUBSYSTEM:CONSOLE`), built alongside `OmniFrame.exe` in the exact same directory while sharing 100% of `OmniFrameCore` and `OmniFrameKernel` — ensuring 3D FEA formulations, mesh/topology validation, ABNT NBR code audits, and exporters match desktop GUI results identically with zero windows opened.

#### Purpose and How to Access on Windows 10/11 (CMD and PowerShell):
- **Purpose:** batch automation (`.bat`, `.cmd`, `.ps1`), offline structural validation (`.omniframe`, `.dxf`, `.ifc`), parametric nonlinear/imperfection/dynamic studies, CI/CD normative ABNT compliance audits, ParaView VTK/CSV/DXF/IFC4 exports, and unattended calculation reports (MD/HTML/PDF).
- **Integrated Desktop GUI Menu (zero typing):** inside `OmniFrame.exe`, open **`Settings > Windows CLI (CMD / PowerShell)`** to register on Windows without admin rights (`PATH` + `App Paths` + `.omniframe`), remove registration, inspect status, or launch a preconfigured **CMD** or **PowerShell** session directly.
- **Non-Admin HKCU Self-Registration:** run `omniframe-cli --install-windows-integration` to register `HKCU\Environment\Path`, `App Paths` keys for `omniframe-cli.exe` / `OmniFrame.exe` / `omniframe.exe`, and the `.omniframe` -> `OmniFrame3D.Project` association with `WM_SETTINGCHANGE`/`SHChangeNotify` broadcast.
- **Portable PowerShell Script (`register_cli_windows.ps1`):** shipped in `cmake/register_cli_windows.ps1` and every clean distribution root (supports default registration, `-CheckOnly`, and `-Uninstall`).
- **Desktop Installer (Inno Setup) & MSIX (Microsoft Store):** `repo/OmniFrame.iss` provides default-checked `addtopath` and `assocproj` tasks; `repo/AppxManifest.xml` declares native `uap5:AppExecutionAlias` entries for `omniframe.exe` and `omniframe-cli.exe` (`desktop4:Subsystem="console"`).
- **Direct Integration in `OmniFrame.exe`:**
  - Open project in GUI: `OmniFrame.exe "path\model.omniframe|.dxf|.ifc"` opens the workspace maximized with that model loaded.
  - Transparent CLI forwarding: passing headless flags to `OmniFrame.exe` (`--cli`, `--version`, `-v`, `--help`, `-h`, `--list-examples`, `--validate`, `--analyze`, `--example`, `--nbr-audit`, `--audit-nbr`, `--check-windows-integration`, `--install-windows-integration`, `--uninstall-windows-integration`) forwards execution cleanly to sibling `omniframe-cli.exe` (prefer calling `omniframe-cli` directly in scripts, or pipe through `| Out-String` in PowerShell if delegating via `OmniFrame.exe`).
- **UTF-8 Support in CMD and PowerShell:** run `chcp 65001 >nul` in CMD or `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8` in PowerShell when redirecting JSON/text output; argument parsing (`CommandLineToArgvW`) and DXF/IFC4/CSV/VTK/JSON/HTML/MD/PDF file I/O support full UTF-8 accented paths inside double quotes.

#### Complete Command and Option Reference (`omniframe-cli.exe`):

| Flag / Command | Arguments | Description |
|---|---|---|
| `--version` / `-v` | — | Prints CLI version string, C++23, Eigen 3.4.0, and `.omniframe` schema version and exits (`0`). |
| `--help` / `-h` | — | Prints official Windows 10/11 (CMD & PowerShell) usage summary and exits (`0`). |
| `--list-examples` | — | Lists the 3 built-in models (`1 \| building`, `2 \| cantilever`, `3 \| truss`) in plain text or `--json`/`--output`. |
| `--example` | `<name\|1\|2\|3>` | Loads a built-in example without a file on disk (`1`/`building`/`portico`/`edificio`/`frame`, `2`/`cantilever`/`balanco`/`lframe`, `3`/`truss`/`trelica`/`passarela`). |
| `--validate` | `[<file.omniframe\|dxf\|ifc>]` | Validates topology, BFS graph connectivity, finiteness, supports, and 2D/3D mesh quality (`0` valid, `2` invalid). |
| `--analyze` | `[<file.omniframe\|dxf\|ifc>]` | Executes configured 3D FEA analysis on input file or `--example` (also implied with `--output`, `--export`, or `--report`). |
| `--nbr-audit` / `--audit-nbr` | `[<file>]` | Runs consolidated ABNT NBR audit (6118, 8800, 6120, 6122, 6123, 7480), auto-solving static forces if needed, emitting `nbrAudit`. |
| `--analysis` | `static\|modal\|buckling\|nonlinear\|pdelta\|harmonic\|spectrum\|transient\|seismic` | Selects structural analysis solver (default `static`). |
| `--solver` | `auto\|ldlt\|llt\|sparselu\|cg\|bicgstab` | Selects `LinearSolverSuite` backend (default `auto`, tracks `usedFallback`). |
| `--modes` | `<N>` | Number of eigenvalues/modes for `modal`, `buckling`, or `spectrum` (default `6`). |
| `--steps` | `<N>` | Load increments for `nonlinear`/`pdelta` or time steps for `transient`/`seismic` (default `20`). |
| `--dt` | `<seconds>` | Time step increment in seconds for `transient`/`seismic` (minimum `1e-5`, default `0.02`). |
| `--scheme` | `newmark\|hht` | Time-integration algorithm for `transient`/`seismic`: Newmark-beta or HHT-alpha (default `newmark`). |
| `--case` | `<id>` | Integer `LoadCase` ID for `static` or `transient`. |
| `--combo` | `<id>` | Integer ABNT `LoadCombination` ID for `static`. |
| `--method` | `nr\|arclength` | Nonlinear equilibrium strategy: Newton-Raphson (`nr`, default) or Crisfield Arc-Length (`arclength`). |
| `--load-factor` | `<factor>` | Target load factor multiplier for `nonlinear`/`pdelta` (default `1.0`). |
| `--full-tangent` | — | Updates global tangent stiffness `Kt` on every Newton-Raphson iteration (default). |
| `--modified-newton` | — | Freezes tangent stiffness `Kt` during each increment (Modified Newton). |
| `--line-search` | — | Enables backtracking line search in Newton-Raphson (default). |
| `--no-line-search` | — | Disables line search in Newton-Raphson. |
| `--imperfection` | `<amp_m>` | Pre-runs linear buckling and perturbs initial geometry by mode amplitude (meters) prior to solving/exporting. |
| `--imperfection-mode` | `<N>` | 1-based buckling mode index used by `--imperfection` (default `1`). |
| `--export` | `<json\|csv\|vtk\|dxf\|ifc> <path>` | Exports model/results (repeatable): `.vtk` `UNSTRUCTURED_GRID` (`von_mises_kPa`), `.csv` (nodal + continuum stresses), `.json`, `.dxf`, `.ifc`. |
| `--report` | `<md\|html\|pdf> <path>` | Writes engineering calculation report in Markdown, HTML5, or A4 PDF (repeatable; embeds static/modal/buckling/nonlinear & `nbrAudit`). |
| `--output` | `<path.json>` | Writes full indented JSON analysis summary cleanly to disk. |
| `--json` | — | Prints full indented JSON summary directly to `stdout`. |
| `--check-windows-integration` | — | Audits user/system/process `PATH`, `App Paths`, and `.omniframe` file association on Windows 10/11. |
| `--install-windows-integration` | — | Registers `omniframe-cli.exe`, `OmniFrame.exe`, and `.omniframe` in `HKCU` without Administrator elevation. |
| `--uninstall-windows-integration` | — | Removes `HKCU` Registry keys and user `PATH` entries created by CLI Windows integration. |

#### Process Exit Codes (`%ERRORLEVEL%` in CMD / `$LASTEXITCODE` in PowerShell):
- `0`: query, validation, solver run, NBR audit, or Windows registration completed cleanly.
- `1`: CLI syntax error, missing/unreadable input file, unknown format, disk write failure, or Windows registry write failure.
- `2`: structural model validation failure (`--validate`) or numerical/physical solver failure (mechanism, singularity, missing positive buckling mode for `--imperfection`, or non-convergence).

#### Ready-to-Run Workflows (CMD and PowerShell):
```bat
:: Windows Command Prompt (CMD)
chcp 65001 >nul
omniframe-cli --install-windows-integration --json
omniframe-cli --example building --nbr-audit --report pdf "C:\Projects\building_report.pdf" --export vtk "C:\Projects\building.vtk" --json
omniframe-cli --validate "C:\Projects\Pórtico São Paulo\modelo_técnico.omniframe"
OmniFrame.exe "C:\Projects\Pórtico São Paulo\modelo_técnico.omniframe"
```
```powershell
# Windows PowerShell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$res = omniframe-cli --example building --analysis nonlinear --steps 10 --load-factor 1.25 --modified-newton --no-line-search --imperfection 0.005 --imperfection-mode 1 --json | ConvertFrom-Json
if ($LASTEXITCODE -eq 0 -and $res.success) { $res.appliedImperfection }
omniframe-cli --example cantilever --analysis transient --scheme hht --dt 0.01 --steps 50 --export csv ".\hht_out.csv" --json | ConvertFrom-Json
OmniFrame.exe --example truss --analysis buckling --modes 4 --json | Out-String
```

See also [docs/cli-reference.md](docs/cli-reference.md) and Chapter 10 of the built-in interactive Manual (`F1`).

### Useful Shortcuts:
- `F1`: Open Complete Operations Manual (`ManualDialog`)
- `F2`: Open Interactive Guided Session (`GuidedSessionDialog`)
- `Ctrl+N`: New Project
- `Ctrl+O`: Open `.omniframe` File
- `Ctrl+S`: Save Project
- `Ctrl+Z` / `Ctrl+Y`: Undo / Redo model edits and property assignments
- `Ctrl+Q`: Exit Application
- `F5`: Solve FEA Model (`Solve`)
- `F`: Frame Structure in Camera (`Fit All`)
- `S` / `N` / `B` / `M`: `Select` / `Node` / `Member` / `Measure` tools (`S` / `N` / `B` / `M`)
- `Del`: Delete Selected Entity
- `Esc`: Cancel Operation / Return to `Select` Tool

### Clean Uninstallation (Microsoft Store Policy 10.2.7):
- Uninstalling via Windows Settings / Microsoft Store or the Inno Setup uninstaller removes program files cleanly without leaving background services. Local preferences are stored under `%LOCALAPPDATA%\OmniFrame\OmniFrame\` (`language.json`, `theme.json`, `graphics.json`), alongside `.omniframe` projects and exported reports saved manually by the user.
