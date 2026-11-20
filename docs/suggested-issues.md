# Sugestões de Issues e Roadmap / Suggested Issues & Roadmap — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher sugestões em português</summary><br>

# OmniFrame 3D — Backlog Técnico e Sugestões para Contribuidores

Este documento reúne tópicos técnicos, extensões analíticas e melhorias de interface mapeadas pelo projeto para orientar colaboradores e desenvolvedores que desejam contribuir com o **OmniFrame 3D**.

---

### Issue 1: Elementos de Casca Curva de Ordem Superior e Vinculação Viga-Casca Automática
- **Área:** `kernel` / `mecanica`
- **Dificuldade:** Avançada
- **Descrição:** Expandir os elementos atuais 1D (Euler-Bernoulli e Timoshenko), placas/cascas 2D e sólidos hexaédricos 3D da versão `2026.11.17.0` com formulações isoparamétricas curvas de 8/9 nós para cascas espessas e restrições cinemáticas automáticas (*tie constraints*) entre barras 1D e malhas 2D/3D sem nós coincidentes.
- **Benefício:** Maior fidelidade geométrica em cascas cilíndricas, cúpulas e interfaces pilares-laje/blocos de fundação.

---

### Issue 2: Rótulas Plásticas Concentradas com Interação $N\text{-}M_y\text{-}M_z$ e Não-Linearidade Combinada
- **Área:** `solver` / `estabilidade`
- **Dificuldade:** Avançada
- **Descrição:** Sobre a base P-$\Delta$, corrotacional, Arco-Comprimento e plasticidade J2 entregue na versão `2026.11.17.0`, adicionar formação automática de rótulas plásticas concentradas por superfície de escoamento axial-flexional ($N\text{-}M_y\text{-}M_z$) para análise *pushover* tridimensional e verificação direta do coeficiente $\gamma_z$.
- **Benefício:** Análise avançada de colapso progressivo e estabilidade inelástica conforme ABNT NBR 8800 e AISC 360 Direct Analysis Method.

---

### Issue 3: Subespaço Vetorial (Ritz/Lanczos) e Amortecimento Não-Clássico na Análise Dinâmica
- **Área:** `solver` / `dinamica`
- **Dificuldade:** Avançada
- **Descrição:** Ampliar o módulo dinâmico atual (`2026.11.17.0`: modal, harmônica FRF, Newmark-$\beta$ e espectro de resposta) com extração iterativa de autovalores por Lanczos/Subespaço para modelos com dezenas de milhares de DOFs e geração automática de matrizes de massa consistente/amortecimento modal por subestrutura.
- **Benefício:** Desempenho escalável para arranha-céus, passarelas longas e plataformas industriais sob carregamentos sísmicos e de vento.

---

### Issue 4: Round-Trip Paramétrico de Conjuntos de Propriedades Estruturais BIM (IFC4 Analytical View)
- **Área:** `io` / `interoperabilidade`
- **Dificuldade:** Média
- **Descrição:** Sobre o importador/exportador **DXF** e **IFC4** da versão `2026.11.17.0`, adicionar mapeamento completo de `IfcStructuralCurveMember`, vínculos de excentricidade, casos de carga (`IfcStructuralLoadGroup`) e perfis metálicos padronizados (*property sets* completos).
- **Benefício:** Intercâmbio analítico sem perdas com plataformas BIM estruturais (Revit, Tekla, ArchiCAD, BlenderBIM).

---

### Issue 5: Gráficos Vetoriais 3D Embutidos e Envelopes Completos nos Relatórios PDF A4
- **Área:** `ui` / `relatorios`
- **Dificuldade:** Média
- **Descrição:** Complementar o gerador de memoriais **HTML / Markdown / PDF A4** entregue na versão `2026.11.17.0` com renderização *offscreen* da viewport 3D (isométricas de deformada, mapas DCR e planos de corte) incorporada diretamente às seções do PDF, além de gráficos comparativos de envelopes máximos/mínimos por combinação normativa.
- **Benefício:** Memorial técnico autossuficiente pronto para auditoria estrutural, aprovação de clientes e protocolo em órgãos técnicos.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse suggested issues in English</summary><br>

# OmniFrame 3D — Technical Backlog & Contributor Suggestions

This document compiles mapped technical topics, analytical extensions, and interface improvements to guide contributors and developers wishing to collaborate on **OmniFrame 3D**.

---

### Issue 1: Higher-Order Curved Shell Elements & Automated Beam-Shell Kinematic Ties
- **Area:** `kernel` / `mechanics`
- **Difficulty:** Advanced
- **Description:** Extend the current `2026.11.17.0` element library — 1D (Euler-Bernoulli and Timoshenko), 2D plates/shells, and 3D hexahedral solids — with curved 8/9-node isoparametric thick-shell formulations and automated tie constraints between 1D frame members and non-coincident 2D/3D meshes.
- **Benefit:** Higher geometric fidelity in cylindrical shells, domes, slab-column junctions, and pile-cap foundations.

---

### Issue 2: Concentrated Plastic Hinges with $N\text{-}M_y\text{-}M_z$ Interaction & Combined Nonlinearity
- **Area:** `solver` / `stability`
- **Difficulty:** Advanced
- **Description:** Building on the P-$\Delta$, 3D corotational, Arc-Length, and J2 plasticity foundation delivered in `2026.11.17.0`, add automated concentrated plastic hinge formation driven by axial-biaxial-moment ($N\text{-}M_y\text{-}M_z$) yield surfaces for 3D pushover studies and direct $\gamma_z$ stability indexing.
- **Benefit:** Advanced progressive-collapse and inelastic stability assessment aligned with ABNT NBR 8800 and the AISC 360 Direct Analysis Method.

---

### Issue 3: Block Lanczos / Subspace Iteration & Non-Classical Damping in Dynamics
- **Area:** `solver` / `dynamics`
- **Difficulty:** Advanced
- **Description:** Expand the `2026.11.17.0` dynamics suite (modal, harmonic FRF, Newmark-$\beta$, and response spectrum) with iterative Block Lanczos / Subspace eigensolvers for models exceeding tens of thousands of DOFs and substructure-level modal damping definitions.
- **Benefit:** Scalable dynamic performance for high-rise towers, long-span pedestrian bridges, and industrial structures under wind and seismic loads.

---

### Issue 4: Parametric Structural Analytical Property-Set Round-Tripping (IFC4 Analytical View)
- **Area:** `io` / `interoperability`
- **Difficulty:** Medium
- **Description:** Expand the `2026.11.17.0` **DXF** and **IFC4** importer/exporter with full property-set round-tripping for `IfcStructuralCurveMember`, eccentric member offsets, `IfcStructuralLoadGroup` combinations, and standardized steel profile catalogs.
- **Benefit:** Lossless analytical model exchange with BIM authoring platforms (Revit, Tekla, ArchiCAD, BlenderBIM).

---

### Issue 5: Embedded 3D Viewport Renders & Envelope Charts Inside Executive A4 PDF Reports
- **Area:** `ui` / `reporting`
- **Difficulty:** Medium
- **Description:** Enhance the `2026.11.17.0` **HTML / Markdown / A4 PDF** calculation report generator with offscreen 3D OpenGL snapshot embedding (isometric deformed shapes, DCR heatmaps, and clip-plane sections) plus comparative max/min envelope charts across code combinations.
- **Benefit:** Self-contained engineering deliverable ready for design audit, client sign-off, and regulatory submission.

</details>
