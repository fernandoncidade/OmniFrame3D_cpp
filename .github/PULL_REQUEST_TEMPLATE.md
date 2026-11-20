# Modelo de Pull Request / Pull Request Template

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

> [!NOTE]
> **Aviso ao Colaborador / Contributor Notice:**  
> 🇧🇷 *Este repositório é um projeto pessoal mantido pelo autor em ritmo próprio de aprendizado em C++ e engenharia de estruturas. Pull Requests são bem-vindos, mas a análise e incorporação de código ocorrem de forma voluntária e sem compromisso de prazo. Caso não haja manifestação ou merge, sinta-se plenamente livre para manter e utilizar suas melhorias em seu próprio fork sob a licença GNU AGPLv3.*  
> 🇺🇸 *This repository is a personal project maintained by the author at their own learning pace in C++ and structural computational mechanics. Pull Requests are welcome, but review and merging are carried out voluntarily without any guaranteed timeframe. If there is no response or merge, please feel free to maintain and use your improvements in your own fork under the GNU AGPLv3 license.*

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher o template em português</summary><br>

## Descrição
Descreva sucintamente o objetivo deste PR, os fundamentos matemáticos e mecânicos envolvidos e qual issue ele soluciona.

## Tipo de Alteração
- [ ] Correção de bug estrutural ou numérico (ex: condicionamento matricial, forças nodais equivalentes)
- [ ] Nova funcionalidade de cálculo (ex: não linearidade, novos tipos de carregamento, vigas de Timoshenko)
- [ ] Melhorias na Viewport 3D / Shaders OpenGL (ex: visualização de diagramas, seleção por raycast)
- [ ] Otimização de desempenho computacional (ex: solver esparso Cholesky, montagem da matriz de rigidez)
- [ ] Atualização e aprofundamento de documentação técnica
- [ ] Internacionalização e tradução da interface gráfica

## Testes e Validação Numérica
- [ ] Compilado com MSVC (`cmake --preset omniframe-msvc-release` e presets auxiliares)
- [ ] Casos de teste estruturais validados analiticamente (`ctest --test-dir build/Testes --output-on-failure`)
- [ ] Verificação de estabilidade na viewport OpenGL (órbita, pan, zoom e renderização de diagramas)

## Conformidade Arquitetural
- [ ] O módulo analítico `core/` e o solver `solver/` permanecem desacoplados de componentes de GUI (`QtWidgets`).
- [ ] A arquitetura permanece estritamente **100% local e offline**, sem introdução de telemetria ou chamadas de rede.
- [ ] O formato aberto `.omni` preserva retrocompatibilidade e validação estrita de esquemas.

## Issues Relacionadas
Corrige #(número da issue)

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse template in English</summary><br>

## Description
Briefly describe the purpose of this PR, mechanical/numerical formulations involved, and which issue it addresses.

## Type of Change
- [ ] Structural or numerical bug fix (e.g., stiffness conditioning, equivalent nodal loads)
- [ ] New computational FEA feature (e.g., nonlinearity, new load types, Timoshenko beams)
- [ ] 3D Viewport / OpenGL Shader enhancements (e.g., diagram rendering, raycast picking)
- [ ] Computational performance optimization (e.g., sparse Cholesky solver, matrix assembly)
- [ ] Documentation and mathematical reference updates
- [ ] Internationalization / Localization updates

## Verification & Numerical Tests
- [ ] Built with MSVC (`cmake --preset omniframe-msvc-release` and separated tool presets)
- [ ] Structural benchmark test cases passed (`ctest --test-dir build/Testes --output-on-failure`)
- [ ] OpenGL viewport stability tested (orbit, pan, zoom, and diagram color contours)

## Architectural Compliance
- [ ] Analytical modules in `core/` and `solver/` remain independent of `QtWidgets`.
- [ ] The software remains strictly **100% local-first and offline** with zero telemetry or network calls.
- [ ] Project serialization format (`.omni`) maintains backwards compatibility and strict input validation.

## Related Issues
Fixes #(issue number)

</details>

