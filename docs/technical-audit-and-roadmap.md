# Auditoria Técnica, Decisões Arquiteturais, Matriz V&V e Roadmap / Technical Audit, ADRs & Roadmap — OmniFrame 3D

**Versão:** `2026.11.15.0`  
**Data:** `15 de novembro de 2027`  
**Autor:** Fernando Nillsson Cidade  

Este documento consolida a auditoria técnica de engenharia estrutural, decisões arquiteturais (ADRs), matrizes de integração, verificação e validação (V&V), conformidade normativa, rastreabilidade, posicionamento técnico e roadmap do **OmniFrame 3D**, conforme os critérios de qualidade científica e rastreabilidade do projeto.

---

## A. Inventário Funcional e Classificação por Recurso / Elemento

Cada recurso é classificado segundo o escopo estritamente verificado por código, integração e testes executados (`Implementado`, `Integrado`, `Validado`, `Testado`, `Documentado`, `Experimental`, `Pendente`), separando explicitamente 1D, 2D e 3D conforme exigido pelos critérios de aceitação.

| Recurso / Módulo | Escopo / Elemento | Classificação Atual | Evidência Principal | Limitação / Observação |
|---|---|---|---|---|
| **Barras 1D Lineares (`BeamTruss1D`)** | Euler-Bernoulli 3D, Timoshenko 3D, Treliça 3D, rótulas, FEF, térmico | Implementado, Integrado, Validado, Testado, Documentado | `source/kernel/elements/BeamTruss1D.*`, `test_CantileverBenchmark`, `test_Phase2SectionsAnd1D` | Pequenos deslocamentos e elasticidade linear na análise estática padrão |
| **Cascas 2D (`ShellElements`)** | `Tri3-MITC3`, `Quad4-MITC4/QM6`, `Quad8` em `surfaceMesh()` | Implementado, Integrado, Validado, Testado, Documentado | `source/kernel/elements/ShellElements.*`, `FemSolver3D.cpp`, `test_Phase5Shells` | Formulação elástica linear de casca plana/facetada; sem não-linearidade geométrica/material 2D |
| **Sólidos 3D (`SolidElements`)** | `Tet4`, `Tet10`, `Hex8-Bbar`, `Hex20` em `surfaceMesh()` (`mask3`) | Implementado, Integrado, Validado, Testado, Documentado | `source/kernel/elements/SolidElements.*`, `FemSolver3D.cpp`, `test_Phase6Solids` | Nós sólidos puros alocam 3 DOFs translacionais (`mask3`); plasticidade J2 não acoplada em 3D |
| **Acoplamento Misto 1D + 2D/3D** | Fusão espacial de nós coincidentes entre pórticos 1D e malhas 2D/3D | Implementado, Integrado, Testado, Documentado | `setupDofManagerFromModel` (`FemSolver3D.cpp`), `test_Phase5Shells` | Nós não coincidentes exigem divisão de malha coincidente (tie constraints automáticos no roadmap) |
| **Não-Linearidade Geométrica 1D** | `Corotational1D`, P-$\Delta$, Newton-Raphson e Arc-Length + critério energético | Implementado, Integrado, Validado, Testado, Documentado | `NonlinearSolver.*`, `test_Phase3NonlinearAndBuckling`, `test_Phase8ComplianceAndReports` | Validado em 1D (`BeamTruss1D`/`Corotational1D`); não generalizado a cascas 2D e sólidos 3D |
| **Plasticidade Material (`J2PlasticityMaterial`)** | Escoamento von Mises com encruamento isotrópico/cinemático (`sigmaY`, `H_iso`, `H_kin`) | Implementado, Integrado (1D/constitutivo), Testado, Documentado | `J2PlasticityMaterial.*`, `test_Phase1KernelAndSolvers`, `test_Phase3NonlinearAndBuckling` | Não generalizado para integração contínua inelástica de cascas 2D ou sólidos 3D |
| **Flambagem Linear e Imperfeição Inicial** | Autovalores $(K_{ff} + \lambda K_{g,ff})\phi = 0$ + `applyBucklingImperfection()` | Implementado, Integrado, Validado, Testado, Documentado | `BucklingSolver.*`, `FemSolver3D.cpp`, `MainWindow.cpp`, `test_Phase6Solids` | Requer estado de compressão axial inicial não nulo |
| **Dinâmica Estrutural (`DynamicSolver`)** | Modal, Harmônica FRF, Espectro de Resposta (CQC/SRSS/ABSSUM), Transiente Newmark/HHT e Sísmica | Implementado, Integrado (GUI/CLI), Validado, Testado, Documentado | `DynamicSolver.*`, `FemSolver3D.cpp`, `main_cli.cpp`, `test_Phase4Dynamics`, `test_Phase6Solids`, `test_Phase7MeshInteropCli` | Autossolvedor em subespaço adequado para porte acadêmico/médio; Block Lanczos de grande escala no roadmap |
| **Suíte Linear & Fallback (`LinearSolverSuite`)** | `SimplicialLDLT`, `SimplicialLLT`, `SparseLU`, `CG`, `BiCGSTAB` com `allowFallback` | Implementado, Integrado, Validado, Testado, Documentado | `LinearSolverSuite.*`, `test_Phase1KernelAndSolvers`, `test_Phase8ComplianceAndReports` | Fallback de `LLT`/`LDLT` registrado explicitamente em `stats.message` |
| **Validação do Modelo (`ModelValidation::issues`)** | Verificação geométrica, topológica, de conectividade BFS e malhas 2D/3D | Implementado, Integrado (Core/CLI), Testado, Documentado | `StructuralModel.cpp`, `main_cli.cpp`, `PdfReportGenerator.cpp`, `test_Phase8ComplianceAndReports` | Emitida em lista estruturada (`IssueSeverity`, `code`, `category`, `entityId`) |
| **Viewport 3D & Pós-Processamento** | `VonMisesStress`, deformadas, diagramas Hermite, DCR, corte 3D, animações e tabela 2D/3D | Implementado, Integrado, Testado, Documentado | `Viewport3D.*`, `EngineeringDialogs.*`, `MainWindow.*`, `test_Phase9UiAndWorkflows` | — |
| **Interoperabilidade & Exportação** | `.omniframe` Schema `2.1.0` (`surfaceMesh`), DXF, IFC4, CSV, VTK `UNSTRUCTURED_GRID` | Implementado, Integrado, Testado, Documentado | `ProjectSerializer.cpp`, `DxfInterop.cpp`, `IfcInterop.cpp`, `main_cli.cpp`, `test_Phase7MeshInteropCli` | Conversões DXF/IFC4 têm escopo geométrico-estrutural documentado |
| **Relatórios Técnicos Seção 9** | HTML, Markdown e PDF Executivo A4 (23 seções, metadados, qualidade de malha, tensões 2D/3D, validação) | Implementado, Integrado, Testado, Documentado | `ProjectSerializer.cpp`, `PdfReportGenerator.cpp`, `test_Phase8ComplianceAndReports` | Imagens 3D embutidas dependem de snapshot da viewport na GUI |
| **Conformidade Normativa (`NbrCodeChecks`)** | `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024` (`LocalPdfVerified`) | Implementado, Integrado, Testado, Documentado | `NbrCodeChecks.*`, `test_Phase8ComplianceAndReports` | Normas sem PDF local (`NBR 8681`, `14762`, `15421`, `AISC 360`, `EC3`) marcadas explicitamente `OutOfLocalPdfScope` |

---

## B. Mapa Arquitetural End-to-End e Fluxo de Dados

```text
+------------------------------------------------------------------------------------+
|                          CAMADA DE ENTRADA (GUI & HEADLESS CLI)                    |
|  OmniFrame.exe (MainWindow, EngineeringDialogs, Viewport3D, GuidedSession)         |
|  omniframe-cli.exe (--validate, --analyze, --case, --combo, --dt, --export/--report)
+-----------------------------------------+------------------------------------------+
                                          |
                                          v  (Mesmo modelo e pipeline matematico)
+------------------------------------------------------------------------------------+
|                        CAMADA DE ENGENHARIA E ORQUESTRACAO (OmniFrameCore)         |
|  StructuralModel                                                                   |
|   ├── m_nodes, m_elements (1D Euler-Bernoulli / Timoshenko / Truss)                |
|   ├── m_surfaceMesh (2D Tri3/Quad4/Quad8 + 3D Tet4/Tet10/Hex8/Hex20 + CCs/cargas)  |
|   ├── m_loadCases, m_loadCombinations, m_loadHistories, m_solverOptions            |
|   └── validate() -> ModelValidation { valid, errors, warnings, issues[] }          |
|                                                                                    |
|  FemSolver3D                                                                       |
|   ├── setupDofManagerFromModel() -> fusao espacial 1D+2D/3D; mask6 (1D/2D), mask3 (3D)
|   ├── solve() / solveWithOptions() -> SolverResult + ContinuumElementResult[]      |
|   ├── solveLoadCasesAndCombinations(), solveBuckling(), applyBucklingImperfection()|
|   ├── solveNonlinear() (NR / Arc-Length / P-Delta com energyTol)                   |
|   └── solveModal(), solveHarmonic(), solveResponseSpectrum(), solveTransient/Seismic()
+-----------------------------------------+------------------------------------------+
                                          |
                                          v  (Estritamente livre de Qt)
+------------------------------------------------------------------------------------+
|                       NUCLEO NUMERICO PURO (OmniFrameKernel - C++23)               |
|  DofManager -> GlobalAssembler -> LinearSolverSuite (LDLT/LLT/SparseLU/CG/BiCGSTAB)|
|  BeamTruss1D | Corotational1D | ShellElements | SolidElements | J2Plasticity       |
|  BucklingSolver | DynamicSolver (Modal/FRF/Espectro/Newmark/HHT) | NonlinearSolver |
|  MeshGenerator (1D/2D/Delaunay/3D/Extrusao + evaluateQuality) | NbrCodeChecks      |
+-----------------------------------------+------------------------------------------+
                                          |
                                          v
+------------------------------------------------------------------------------------+
|                     POS-PROCESSAMENTO, SERIALIZACAO E RELATORIOS                   |
|  GUI: Viewport3D (Deformada, Hermite 1D, VonMisesStress 2D/3D, DCR, Clip, Anim.)   |
|       MainWindow Tabela "Esforcos 2D/3D (Casca/Solido)"                            |
|  IO : ProjectSerializer (.omniframe 2.1.0, HTML/MD Secao 9), PdfReportGenerator A4 |
|  CLI: CSV (1D + malha 2D/3D + tensoes), VTK UNSTRUCTURED_GRID (1D/2D/3D), JSON     |
+------------------------------------------------------------------------------------+
```

---

## C. Matriz de Integração e Auditoria das Alterações

| Item | Estado anterior | Alteração realizada | Testes | Resultado | Pendências |
|---|---|---|---|---|---|
| **Integração E2E de Cascas 2D e Sólidos 3D (`surfaceMesh`)** | `ShellElements` e `SolidElements` existiam no kernel sem montagem unificada completa e pós-processamento na aplicação principal | Unificados no `FemSolver3D` (`setupDofManagerFromModel`, fusão espacial 1D+2D/3D, `mask3` em sólidos puros, recuperação `ContinuumElementResult` e `extractNodalDisplacements`) | `test_Phase5Shells`, `test_Phase6Solids`, `test_Phase7MeshInteropCli` | Integrado, compilado e aprovado nos fluxos estático e acoplado 1D+2D e 3D Hex8 (`mask3`) | Tie constraints para malhas não coincidentes e formulações não-lineares 2D/3D |
| **Controle Explícito de Fallback no `LinearSolverSuite`** | Falhas de Cholesky LLT/LDLT podiam falhar ou mascarar diagnóstico sem flag dedicada | Adicionado `LinearSolverOptions::allowFallback` e estatísticas reais em fallback (`solveTimeMs`, `computeResidualStats`, registro explícito em `stats.message` e relatórios) | `test_Phase1KernelAndSolvers`, `test_Phase8ComplianceAndReports` | Testado com matriz simétrica indefinida (`NOT_SPD` com `allowFallback=false`; `SimplicialLDLT` com `allowFallback=true`) | — |
| **Critério Energético e Diagnóstico Não Linear** | Critérios de força/deslocamento sem rastro energético dedicado no registro de passo | Adicionados `NonlinearOptions::energyTol`, `NonlinearStepRecord::energyRatio` e `NonlinearResult::convergenceDiagnostic` | `test_Phase3NonlinearAndBuckling`, `test_Phase8ComplianceAndReports` | Convergência energética verificada ($\le 10^{-6}$ no passo final) | Extensão da não-linearidade energética para 2D/3D |
| **Validação Estruturada do Modelo (`ValidationIssue`)** | `ModelValidation` possuía apenas strings simples (`errors`/`warnings`) sem verificação completa de malhas 2D/3D e desconexão BFS | Adicionados `IssueSeverity`, `ValidationIssue` (`code`, `message`, `category`, `entityId`), checagens 2D/3D e BFS em `StructuralModel::validate()`, exibidos em CLI e PDF | `test_Phase0ValidationAndBenchmarks`, `test_Phase8ComplianceAndReports` | Erros estruturados validados em modelo desconexo/sem apoios | — |
| **Dinâmica Transiente, Sísmica e Imperfeição de Flambagem na Aplicação** | Rotinas concentradas no kernel sem atalhos completos de modelo e controles dedicados | Adicionados `FemSolver3D::solveTransient`, `solveSeismic`, `applyBucklingImperfection`, controles de UI (`EngineeringDialogs`/`MainWindow`) e flags CLI `--analysis transient\|seismic`, `--dt` | `test_Phase4Dynamics`, `test_Phase6Solids`, `test_Phase7MeshInteropCli` | Execução transiente E2E (20 passos + t=0), sísmica CLI e perturbação geométrica de modo crítico verificadas | Espectro / transiente em modelos de dezenas de milhares de DOFs (Lanczos) |
| **Pós-Processamento 2D/3D na Viewport e UI** | Viewport voltada apenas a diagramas 1D/DCR | Adicionado `PostContourMode::VonMisesStress`, renderização de cascas/sólidos, apoios contínuos, deformada contínua e 4ª aba `"Esforços 2D/3D (Casca/Sólido)"`; corrigido `objectName` dos combos de pós-processamento | `test_Phase9UiAndWorkflows` | Aprovado em teste offscreen de UI e regressão de combos | Isolinhas de momento/tensão principal em corte 2D |
| **Persistência `.omniframe`, VTK/CSV e Relatórios Seção 9** | VTK exportava apenas barras 1D `POLYDATA`; relatórios omitiam seção 2D/3D completa e fallback | Adicionado bloco `"surfaceMesh"` em JSON `2.1.0`; VTK promovido a `UNSTRUCTURED_GRID` (1D/2D/3D + `von_mises_kPa`); CSV e relatórios HTML/MD/PDF expandidos para os 20 itens da Seção 9 | `test_ProjectSerializer`, `test_Phase7MeshInteropCli`, `test_Phase8ComplianceAndReports` | Round-trip `.omniframe`, VTK/CSV contínuos e relatórios HTML/MD/PDF > 15 KB aprovados | Captura automatizada de imagem 3D diretamente na CLI headless sem OpenGL offscreen |
| **Ativos de Interface (`QComboBox` / `QSpinBox` sem `spin_*.png`) em `OmniFrame.exe`** | Commit anterior `2041aa9` removeu 8 `spin_*.png` ainda referenciados em `CMakeLists.txt`, `ThemeManager.cpp` e `AboutDialog.cpp`, quebrando `qrc_omniframe_runtime_assets` | Removidas as referências em `CMakeLists.txt`, `OBSERVACAO.md` e substituídos os PNGs por setas vetoriais/CSS nativas em Qt (`ThemeManager.cpp`, `AboutDialog.cpp`), eliminando os 8 arquivos | Build `omniframe-msvc-release` + `test_Phase9UiAndWorkflows` | `OmniFrame.exe` compilado e renderização das setas verificada sem geração de `spin_*.png` | — |

---

## D. Registro de Decisões Arquiteturais (ADRs)

- **ADR-001 — Unificação 1D/2D/3D via `StructuralModel::surfaceMesh()` e `mask3` para Sólidos Puros:** Em vez de criar um segundo solver separado para cascas e sólidos, `FemSolver3D` mapeia barras 1D, cascas 2D e sólidos 3D no mesmo `DofManager` via `setupDofManagerFromModel`, fundindo nós 1D/2D/3D espacialmente coincidentes e atribuindo máscara de 3 DOFs (`mask3`: $u_x, u_y, u_z$) a nós pertencentes exclusivamente a elementos sólidos, preservando ausência de singularidades rotacionais artificiais em nós 3D puros.
- **ADR-002 — Retrocompatibilidade do Formato `.omniframe` (`2.1.0`):** A persistência contínua 2D/3D foi incorporada como objeto JSON opcional `"surfaceMesh"` (incluindo BCs, molas, recalques, pressão normal, carga distribuída, $\Delta T$ e tensões), preservando abertura transparente de arquivos `1.0` e `2.0` e campos desconhecidos (`preservedUnknownJson`).
- **ADR-003 — Política Explícita e Auditável de Fallback Linear:** O `LinearSolverSuite` mantém `allowFallback = true` apenas quando autorizado nas opções do solver e registra obrigatoriamente a substituição (`SimplicialLLT -> SimplicialLDLT/SparseLU`) em `LinearSolverStats::solverUsed` e `stats.message`, propagando o registro para CLI, HTML, Markdown e PDF. Com `allowFallback = false`, matrizes não-SPD falham explicitamente (`NOT_SPD`).
- **ADR-004 — Exportação Unificada `DATASET UNSTRUCTURED_GRID` para VTK:** O formato antigo `POLYDATA` suportava apenas segmentos 1D; `UNSTRUCTURED_GRID` representa simultaneamente barras 1D (célula tipo `3`), triângulos/quadriláteros 2D (`5, 9, 23`) e tetraedros/hexaedros 3D (`10, 24, 12, 25`) com `POINT_DATA` (deslocamentos/reações) e `CELL_DATA` (`von_mises_kPa`, `sigma1_kPa`, `sigma3_kPa`).
- **ADR-005 — Identificação Explícita por `objectName` em Widgets de Pós-Processamento:** Ao ampliar `PostProcessControlDialog` com `VonMisesStress` (passando a ter 4 modos, igual aos 4 eixos de corte), atribuiu-se `objectName` explícito (`postContourCombo`, `clipAxisCombo`) para eliminar colisão de descoberta por contagem posicional em testes de interface.

---

## E. Biblioteca de Benchmarks e Matriz de Validação Quantitativa

| Benchmark | Referência Analítica / Literatura | Suíte CTest | Critério Verificado em Execução |
|---|---|---|---|
| **Viga em Balanço 3D + Torção** | Timoshenko / Euler-Bernoulli ($\delta = PL^3/3EI$, $\theta = TL/GJ$) | `test_CantileverBenchmark` | Erro relativo nulo nos DOFs de flexão, torção e reação na base |
| **Equilíbrio Estático Global** | $\sum F = 0$, $\sum M = 0$ | `test_EquilibriumReactions`, `test_Phase5Shells`, `test_Phase6Solids` | Resíduo global equilibrado em 1D, placa 2D, acoplamento 1D+2D e barra sólida 3D |
| **Placa Fina Quadrada de Navier** | Série dupla de Navier ($w_{\text{centro}}$) | `test_Phase5Shells` | Convergência E2E da malha `Quad4` (MITC4/QM6) sem *shear locking* |
| **Casca Cilíndrica de Scordelis-Lo** | Benchmark MacNeal-Harder ($u_z = 0.3024\text{ in}$ em sistema equivalente) | `test_Phase5Shells` | Aderência numérica dentro da tolerância da malha de casca |
| **Cilindro Espesso de Lamé (Hex20) e B-Bar ($v = 0.499$)** | Solução exata de Lamé em elasticidade triaxial | `test_Phase6Solids` | $u_r(a) = 9.5299\times 10^{-6}$ vs. $9.5333\times 10^{-6}$; B-bar sem travamento volumétrico |
| **Barra Sólida `Hex8` Uniaxial (`mask3`)** | Patch test $u = PL/EA$, $\sigma_{xx} = P/A = 400\text{ kPa}$ | `test_Phase6Solids` | `60` DOFs (`mask3`), $u_x(L) = PL/EA$, `maxSolidVonMises = 400 kPa` |
| **Acoplamento Misto 1D + 2D (Laje sobre 4 Pilares)** | Transferência integral de pressão normal da laje ($192\text{ kN}$) às bases | `test_Phase5Shells` | $\sum R_z = 192.0\text{ kN}$ e igualdade de recalque nos nós coincidentes |
| **Não-Linearidade, Flambagem & Dinâmica SDOF/MDOF** | Carga crítica de Euler, frequência natural $\omega = \sqrt{k/m}$, Arc-Length | `test_Phase3NonlinearAndBuckling`, `test_Phase4Dynamics`, `test_Phase6Solids` | Autovalores modais/flambagem, 20 passos Newmark (+ $t=0$) e critério energético |

---

## F. Matriz de Conformidade Normativa

| Norma Técnica | Classificação | Aplicação Verificada | Observações de Escopo |
|---|---|---|---|
| **ABNT NBR 6118:2023** | `LocalPdfVerified` | Concreto armado (flexão ELU, pilares $P\text{-}M_x\text{-}M_y$, ELS flecha/fissura, combinações) | Validada em `test_Phase8ComplianceAndReports` contra rotinas verificadas no PDF local |
| **ABNT NBR 6120:2019** | `LocalPdfVerified` | Ações permanentes, pesos específicos e sobrecargas | Integrada ao catálogo/cálculo de ações |
| **ABNT NBR 6122:2022** | `LocalPdfVerified` | Verificação de tensões em sapatas frente a $\sigma_{\text{adm}}$ | Alerta de levantamento ($e > L/6$) coberto em teste |
| **ABNT NBR 6123:2023** | `LocalPdfVerified` | Velocidade característica do vento $V_k$ e pressão dinâmica $q$ | Integrada em `NbrCodeChecks` |
| **ABNT NBR 7480:2022** | `LocalPdfVerified` | Propriedades nominais de aços de armadura CA-50/CA-60 | Aplicada em dimensionamento de vigas/pilares |
| **ABNT NBR 8800:2024** | `LocalPdfVerified` | Aço estrutural (tração, compressão, flexão, cortante, estabilidade) | Coberta por suíte unitária em `test_Phase8ComplianceAndReports` |
| **NBR 8681 / 14762 / 15421, AISC 360-22, Eurocode 3** | `OutOfLocalPdfScope` | Sinalizadas explicitamente como fora do acervo PDF local | Sem cláusulas fabricadas; transparência total nos relatórios e diálogos |

---

## G. Matriz de Rastreabilidade

`Requisito -> Implementação -> Teste -> Evidência -> Documentação`

| Requisito | Implementação Principal | Suíte de Teste | Evidência Verificada | Documentação |
|---|---|---|---|---|
| Kernel FEM sem Qt (`OmniFrameKernel`) | `source/kernel/**` (`CMakeLists.txt` sem módulos Qt no target `OmniFrameKernel`) | `test_Phase1`..`test_Phase6` | Linkagem limpa de `OmniFrameKernel.lib` sem Qt | [`architecture.md`](architecture.md) |
| Estática mista 1D+2D+3D e `mask3` | `source/solver/FemSolver3D.cpp` | `test_Phase5Shells`, `test_Phase6Solids` | $60$ DOFs `Hex8`, placa Navier e laje sobre pilares equilibrada | [`architecture.md`](architecture.md) |
| Fallback rastreado e critério energético | `LinearSolverSuite.cpp`, `NonlinearSolver.*` | `test_Phase8ComplianceAndReports` | Fallback LLT->LDLT testado com `NOT_SPD` e `energyRatio` | [`architecture.md`](architecture.md) |
| Validação estruturada (`ValidationIssue`) | `StructuralModel.cpp`, `main_cli.cpp` | `test_Phase8ComplianceAndReports` | Emissão de `code`/`category`/`Error` em modelo desconexo | [`cli-reference.md`](cli-reference.md) |
| CLI headless completa (`static\|modal\|buckling\|nonlinear\|pdelta\|harmonic\|spectrum\|transient\|seismic`) | `source/cli/main_cli.cpp` | `test_Phase7MeshInteropCli`, `test_Phase8ComplianceAndReports` | Execuções reais `--case`, `--combo`, `--dt`, VTK, CSV, PDF | [`cli-reference.md`](cli-reference.md) |
| Pós-processamento 2D/3D e UI estável | `Viewport3D.*`, `EngineeringDialogs.*`, `MainWindow.*` | `test_Phase9UiAndWorkflows` | Renderização, contorno `VonMisesStress`, aba 2D/3D e combos UI aprovados | [`architecture.md`](architecture.md) |
| Relatórios profissionais Seção 9 | `ProjectSerializer.cpp`, `PdfReportGenerator.cpp` | `test_Phase8ComplianceAndReports` | HTML, Markdown e PDF A4 com metadados, solver e tensões 2D/3D | [`compliance.md`](compliance.md) |

---

## H. Posicionamento Técnico Competitivo

| Ferramenta de Referência | Pontos Fortes Reconhecidos no Ecossistema | Diferenciação Técnica Efetiva do OmniFrame 3D |
|---|---|---|
| **Ftool** | Excelente didática bidimensional, produtividade instantânea em pórticos planos, linhas de influência e trens-tipo | Modelagem e análise nativas **tridimensionais**, cascas 2D e sólidos 3D integrados, análise modal/harmônica/transiente/flambagem, não-linearidade 1D e CLI headless |
| **MASTAN2** | Maturidade acadêmica consolidada em análise inelástica de segunda ordem de pórticos de aço | Binário nativo **C++23** independente de MATLAB, núcleo FEM extensível sem dependência gráfica, elementos 2D/3D contínuos, dinâmica transiente e interoperabilidade DXF/IFC4/VTK |
| **OpenSees** | Vasto ecossistema científico de elementos avançados, materiais cíclicos e pesquisa sísmica | Interface gráfica 3D integrada em OpenGL 3.3 Core com pós-processamento interativo e fluxo único acoplado à mesma biblioteca matemática utilizada na CLI e geração nativa de memoriais A4 |
| **SAP2000** | Amplitude industrial consolidada em dimensionamento multimaterial, etapas construtivas e vasto catálogo de solvers e normas | Código aberto em C++23 com total transparência das formulações matriciais, formato aberto `.omniframe` JSON auditável, execução 100% local e linha de comando leve para pipelines automatizados |

---

## I. Limitações Conhecidas e Escopo Experimental

1. **Plasticidade Material J2 (`J2PlasticityMaterial`) em 2D/3D:** O retorno radial J2 foi integrado e testado no nível constitutivo e em barras/pórticos 1D; elementos de casca 2D e sólidos 3D operam atualmente em regime elástico-linear isotrópico no `FemSolver3D`.
2. **Não-Linearidade Geométrica em Contínuos 2D/3D:** Análise corrotacional 3D e Arc-Length de Crisfield aplicam-se a elementos 1D; extensão para grandes rotações em cascas e hiperelasticidade/grandes deformações em sólidos permanece pendente.
3. **Acoplamento por Coincidência Espacial:** Elementos 1D e malhas 2D/3D conectam-se diretamente quando compartilham coordenadas nodais coincidentes; vinculações cinemáticas automáticas entre malhas não conformes (*tie constraints*) e elementos de casca curva de 9 nós ainda não foram implementados.
4. **Escalabilidade Modal de Grande Porte:** A iteração em subespaço modal atual atende modelos pequenos e médios; estruturas com dezenas de milhares de DOFs demandam introdução futura de Block Lanczos e pré-condicionamento avançado.
5. **Interoperabilidade DXF/IFC4:** Preserva geometria estrutural primária, seções básicas e superfícies mapeadas, sem cobertura integral de excentricidades analíticas avançadas ou detalhamento de armaduras/ligações.

---

## J. Roadmap Técnico Consolidado

| Prioridade | Frente Técnica | Entregas Planejadas | Critério Objetivo de Aceitação |
|---|---|---|---|
| **P0 (Concluída / Consolidada)** | Integração Unificada 1D/2D/3D & Qualidade Core | `surfaceMesh` + `mask3`, `allowFallback`, `energyTol`, `ValidationIssue`, UI `VonMisesStress`, VTK `UNSTRUCTURED_GRID`, relatórios Seção 9 e 20 testes CTest aprovados | Usuário cria, analisa, visualiza, exporta e audita modelos 1D, 2D e 3D via GUI e CLI sem APIs internas |
| **P1 (Consolidada / Expansão)** | Dinâmica e Sísmica | Fluxo unificado de históricos transientes (`Newmark`/`HHT`), sísmica, espectro NBR 15421 e FRF em GUI e CLI | Expansão futura para autossolvedor Block Lanczos e amortecimento não-clássico por subestrutura |
| **P2** | Não-Linearidade 2D/3D e Estabilidade Avançada | Rótulas plásticas concentradas $N\text{-}M_y\text{-}M_z$ e integração constitutiva J2 / não-linearidade geométrica em cascas e sólidos | Benchmarks de colapso plástico 2D/3D validados contra soluções de referência |
| **P3** | Modelagem e Interoperabilidade Avançada | *Tie constraints* automáticos 1D-2D-3D sem nós coincidentes, cascas curvas de 8/9 nós e round-trip estendido IFC4 Analytical View | Conexão pilar-laje não conforme e importação/exportação analítica sem perda de excentricidade |
| **P4** | Desempenho e Estudos Paramétricos | Benchmarks de convergência $h/p$ automatizados, paralelização de montagem em grande escala e comparação documentada por modelos equivalentes com ferramentas de referência | Curvas quantitativas de convergência de deslocamento/tensão e escalabilidade reprodutíveis no CI |
