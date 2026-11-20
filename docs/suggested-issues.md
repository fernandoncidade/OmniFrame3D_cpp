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

### Issue 1: Formulação de Elemento de Viga de Timoshenko (Deformação por Cisalhamento)
- **Área:** `solver` / `mecanica`
- **Dificuldade:** Média
- **Descrição:** Implementar a matriz de rigidez local 12x12 considerando a teoria de vigas de Timoshenko para barras esbeltas e curtas onde a deformação por esforço cortante é significativa ($\Phi_y = \frac{12 E I_z}{G A_{sy} L^2}$, $\Phi_z = \frac{12 E I_y}{G A_{sz} L^2}$).
- **Benefício:** Maior precisão na modelagem de vigas-parede, pilares maciços de concreto e seções de pontes.

---

### Issue 2: Análise Geométrica Não Linear (Efeitos P-$\Delta$ e P-$\delta$)
- **Área:** `solver` / `estabilidade`
- **Dificuldade:** Avançada
- **Descrição:** Adicionar a matriz de rigidez geométrica $[k_g]$ de 12x12 dependente da força axial $N$. Implementar algoritmo iterativo de Newton-Raphson para convergência da análise de segunda ordem e cálculo do coeficiente de estabilidade global $\gamma_z$.
- **Benefício:** Atendimento direto aos requisitos de verificação de estabilidade da ABNT NBR 8800 e AISC 360 Direct Analysis Method.

---

### Issue 3: Análise Dinâmica Modal (Frequências Naturais e Modos de Vibração)
- **Área:** `solver` / `dinamica`
- **Dificuldade:** Avançada
- **Descrição:** Montagem da matriz de massa consistente $[M]$ ou concentrada (*lumped mass*) e resolução do problema de autovalor generalizado $[K] \{\phi\} = \omega^2 [M] \{\phi\}$ utilizando o solver esparso de autovalores do Eigen (`Eigen::SelfAdjointEigenSolver` ou Spectra). Animação dos modos de vibrar na viewport 3D.
- **Benefício:** Análise de conforto humano, vibrações de pisos e ações sísmicas.

---

### Issue 4: Importador e Exportador de Arquivos CAD / BIM (DXF e IFC)
- **Área:** `io` / `interoperabilidade`
- **Dificuldade:** Média
- **Descrição:** Permitir a importação de linhas e camadas 3D em arquivos DXF (AutoCAD) e exportação de elementos estruturais para o padrão aberto IFC4 (Industry Foundation Classes).
- **Benefício:** Interoperabilidade com softwares de modelagem arquitetônica (Revit, ArchiCAD, Blender).

---

### Issue 5: Gerador de Relatórios Executivos em PDF e Envelopes de Esforços
- **Área:** `ui` / `relatorios`
- **Dificuldade:** Média
- **Descrição:** Implementar diálogo de exportação de memória de cálculo detalhada em PDF com renderização de diagramas em alta resolução, tabela de reações de apoio e envelopes máximos e mínimos para combinações de ações.
- **Benefício:** Entregável profissional pronto para revisão de projeto estrutural, aprovação de clientes e protocolo em órgãos técnicos.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse suggested issues in English</summary><br>

# OmniFrame 3D — Technical Backlog & Contributor Suggestions

This document compiles mapped technical topics, analytical extensions, and interface improvements to guide contributors and developers wishing to collaborate on **OmniFrame 3D**.

---

### Issue 1: Timoshenko Beam Element Formulation (Shear Deformation)
- **Area:** `solver` / `mechanics`
- **Difficulty:** Medium
- **Description:** Implement the 12x12 local stiffness matrix incorporating Timoshenko beam theory for stocky and short members where transverse shear deformation is significant ($\Phi_y = \frac{12 E I_z}{G A_{sy} L^2}$, $\Phi_z = \frac{12 E I_y}{G A_{sz} L^2}$).
- **Benefit:** Greater accuracy when modeling deep transfer beams, solid concrete columns, and bridge box girders.

---

### Issue 2: Geometric Nonlinear Analysis (P-$\Delta$ and P-$\delta$ Effects)
- **Area:** `solver` / `stability`
- **Difficulty:** Advanced
- **Description:** Add the 12x12 geometric stiffness matrix $[k_g]$ dependent on axial force $N$. Implement an iterative Newton-Raphson algorithm for second-order convergence and computation of the global stability coefficient $\gamma_z$.
- **Benefit:** Direct compliance with stability verification requirements in ABNT NBR 8800 and the AISC 360 Direct Analysis Method.

---

### Issue 3: Modal Dynamic Analysis (Natural Frequencies and Mode Shapes)
- **Area:** `solver` / `dynamics`
- **Difficulty:** Advanced
- **Description:** Assemble consistent or lumped mass matrices $[M]$ and solve the generalized eigenvalue problem $[K] \{\phi\} = \omega^2 [M] \{\phi\}$ using Eigen's sparse eigenvalue solver (`Eigen::SelfAdjointEigenSolver` or Spectra), animating mode shapes in the 3D viewport.
- **Benefit:** Human comfort assessment, floor vibration analysis, and seismic response studies.

---

### Issue 4: CAD / BIM File Importer and Exporter (DXF and IFC4)
- **Area:** `io` / `interoperability`
- **Difficulty:** Medium
- **Description:** Support importing 3D wireframe lines and layers from DXF files (AutoCAD) and exporting structural members to the open IFC4 (Industry Foundation Classes) standard.
- **Benefit:** Interoperability with architectural modeling software (Revit, ArchiCAD, Blender).

---

### Issue 5: Executive PDF Report Generator and Action Envelopes
- **Area:** `ui` / `reporting`
- **Difficulty:** Medium
- **Description:** Implement an export dialog for detailed PDF engineering calculation reports featuring high-resolution diagram renders, support reaction tables, and maximum/minimum envelopes for load combinations.
- **Benefit:** Professional deliverable ready for structural project review, client approval, and regulatory filing.

</details>
