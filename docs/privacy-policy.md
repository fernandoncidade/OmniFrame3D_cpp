# Política de Privacidade / Privacy Policy — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details open>
<summary>Clique para expandir / recolher a Política de Privacidade em português</summary><br>

# POLÍTICA DE PRIVACIDADE — OMNIFRAME 3D

**Versão:** `2026.11.15.0`  
**Data:** `15 de novembro de 2027`  
**Desenvolvedor / Controlador:** Fernando Nillsson Cidade (Desenvolvedor Individual)  
**E-mail de Contato e Privacidade:** `linceu_lighthouse@outlook.com`  
**Site Oficial do Projeto:** [https://github.com/fernandoncidade/OmniFrame_cpp](https://github.com/fernandoncidade/OmniFrame_cpp)

---

## 1. Declaração de Independência frente à Política de Privacidade da Microsoft

> [!IMPORTANT]
> **A Microsoft NÃO fornece uma política de privacidade padrão para o OmniFrame 3D, e este aplicativo NÃO é coberto por nenhuma política de privacidade da Microsoft.**
> - Esta Política de Privacidade aplica-se exclusiva e integralmente ao software **OmniFrame 3D** em todas as suas formas de distribuição (Microsoft Store MSIX, instalador Windows ou compilação a partir do código-fonte).
> - Ao navegar, baixar, instalar, atualizar, desinstalar, avaliar ou publicar comentários na loja **Microsoft Store** ou utilizar recursos diagnósticos opcionais do sistema operacional **Windows**, os dados tratados pela própria Microsoft são regidos pela [Política de Privacidade da Microsoft](https://www.microsoft.com/pt-br/privacy/privacystatement).
> - Além disso, a Microsoft não presta suporte técnico aos usuários do OmniFrame 3D; dúvidas sobre privacidade ou operação do aplicativo devem ser direcionadas diretamente ao desenvolvedor por e-mail ou pelo repositório oficial no GitHub.

---

## 2. Arquitetura Local-First e Ausência de Coleta de Dados Pessoais

O **OmniFrame 3D** foi projetado para operar **100% localmente e offline** na estação de trabalho do usuário:

1. **Zero Coleta de Dados Pessoais (PII):** O software **NÃO** coleta, solicita, lê, armazena remotamente, compartilha ou vende nome, CPF/CNPJ, endereço, telefone, e-mail, localização geográfica (GPS/IP), histórico de navegação ou identificadores publicitários.
2. **Zero Rede, Zero Telemetria e Zero Contas:** O código do OmniFrame 3D não exige autenticação, não contém SDKs analíticos, rastreadores, anúncios, servidores em nuvem ou requisições HTTP/HTTPS em segundo plano. *(Observação técnica: a distribuição empacotada inclui bibliotecas padrão do runtime Qt 6 necessárias à execução dos plugins gráficos/sistema, sem uso de APIs de rede pelo aplicativo.)*
3. **Sem Coleta de Dados de Crianças ou Dados Sensíveis:** Em conformidade com os requisitos das Políticas da Microsoft Store (Seções 10.5.5 a 10.5.8), o aplicativo não coleta dados financeiros, de saúde, biométricos ou informações pessoais de crianças.

---

## 3. Dados Processados Localmente pelo Usuário

Todo processamento estrutural permanece sob controle exclusivo do usuário, na RAM, CPU e GPU (OpenGL 3.3 Core) do computador local:

- **Dados de Engenharia em Memória:** Coordenadas nodais $(X, Y, Z)$, conectividade de barras, materiais ($E, \nu, \alpha, \gamma$), seções transversais ($A, I_y, I_z, J$), ângulo de roll, rótulas, apoios, molas, deslocamentos prescritos e cargas nodais/distribuídas.
- **Arquivos Salvos Explicitamente pelo Usuário:** Modelos estruturais no formato JSON aberto **`.omniframe`**, arquivos de interoperabilidade CAD/BIM (**`.dxf`**, **`.ifc`**), relatórios técnicos (**`.pdf`**, **`.html`**, **`.md`**) e conjuntos de resultados (**`.json`**, **`.csv`**, **`.vtk`**) são gravados **apenas** na pasta selecionada explicitamente pelo usuário nos diálogos padrão do Windows ou via argumentos explícitos na CLI.
- **Preferências Locais Mínimas de Interface:** Apenas a escolha de idioma (`language.json`), tema visual (`theme.json`) e perfil gráfico OpenGL (`graphics.json`) são salvos localmente em arquivos JSON na pasta de dados locais do aplicativo (`%LOCALAPPDATA%\OmniFrame\OmniFrame\`, ou no contêiner isolado equivalente quando executado via pacote MSIX).

---

## 4. Capacidade Declarada no Manifesto MSIX (`runFullTrust`)

Para distribuição em pacote MSIX na Microsoft Store (Política 10.6), o manifesto [`repo/AppxManifest.xml`](../repo/AppxManifest.xml) declara exclusivamente a capacidade `<rescap:Capability Name="runFullTrust" />`, estritamente necessária por se tratar de aplicação desktop nativa Win32 em C++23 / Qt 6 com aceleração OpenGL 3.3 Core e acesso às caixas de diálogo padrão do Windows para abrir e salvar projetos `.omniframe` e relatórios escolhidos pelo usuário. Nenhuma capacidade de localização, câmera, microfone ou contatos é declarada ou acessada.

---

## 5. Direitos do Titular, Controle e Desinstalação Limpa (Política 10.2.7 / LGPD / GDPR)

O usuário exerce controle pleno sobre todos os dados locais:
- **Acesso, Edição, Portabilidade e Exclusão:** Você pode abrir, copiar, editar ou excluir livremente qualquer arquivo `.omniframe`, `.dxf`, `.ifc`, `.pdf`, `.html`, `.md`, `.json`, `.csv`, `.vtk`, `language.json`, `theme.json` ou `graphics.json` a qualquer momento.
- **Desinstalação Limpa e Controle de Registro Local:** Você pode desinstalar e remover o OmniFrame 3D de forma limpa a qualquer momento por **Configurações do Windows → Aplicativos → Aplicativos instalados** ou pelo **Menu Iniciar**. A remoção do pacote MSIX elimina os binários e o estado virtualizado do aplicativo sem deixar serviços em segundo plano, drivers ou tarefas agendadas residuais. Caso utilize opcionalmente a associação local da CLI no Windows (`Configurações > CLI Windows`, `--install-windows-integration` ou `register_cli_windows.ps1`), apenas chaves locais do usuário atual (`HKCU` para `PATH`, `App Paths` e `.omniframe`) são gravadas, sendo integralmente removíveis via `--uninstall-windows-integration` ou pelo desinstalador.

---

## 6. Contato do Responsável pela Privacidade e Suporte

- **Responsável:** Fernando Nillsson Cidade
- **E-mail:** `linceu_lighthouse@outlook.com`
- **Suporte / Issues:** [https://github.com/fernandoncidade/OmniFrame_cpp/issues](https://github.com/fernandoncidade/OmniFrame_cpp/issues)

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details open>
<summary>Click to expand / collapse Privacy Policy in English</summary><br>

# PRIVACY POLICY — OMNIFRAME 3D

**Version:** `2026.11.15.0`  
**Date:** `November 15, 2027`  
**Developer / Controller:** Fernando Nillsson Cidade (Individual Developer)  
**Privacy & Support Email:** `linceu_lighthouse@outlook.com`  
**Official Project Website:** [https://github.com/fernandoncidade/OmniFrame_cpp](https://github.com/fernandoncidade/OmniFrame_cpp)

---

## 1. Independence from Microsoft Privacy Statement

> [!IMPORTANT]
> **Microsoft does NOT provide a default privacy policy for OmniFrame 3D, and this application is NOT covered by any Microsoft privacy policy.**
> - This Privacy Policy applies exclusively to the **OmniFrame 3D** software across all distribution channels (Microsoft Store MSIX package, Windows installer, or independent builds from source code).
> - Data collected separately by Microsoft when you browse, download, install, update, uninstall, rate, or review apps in the **Microsoft Store** or use optional **Windows** operating system diagnostic features is governed by the [Microsoft Privacy Statement](https://www.microsoft.com/en-us/privacy/privacystatement).
> - In addition, Microsoft does not provide technical or customer support to OmniFrame 3D users; privacy or application operation inquiries must be directed straight to the developer via email or through the official GitHub repository.

---

## 2. Local-First Architecture & Zero Personal Data Collection

**OmniFrame 3D** is engineered to operate **100% locally and offline** on the user's workstation:

1. **Zero Personal Data Collection (PII):** The software does **NOT** collect, request, read, remotely store, share, or sell names, national/taxpayer IDs, addresses, phone numbers, email addresses, geographic location (GPS/IP), browsing history, or advertising identifiers.
2. **Zero Network, Zero Telemetry, and Zero Accounts:** The OmniFrame 3D codebase requires no authentication and contains no analytics SDKs, trackers, ads, cloud servers, or background HTTP/HTTPS requests. *(Technical note: the packaged distribution includes standard Qt 6 runtime libraries required to run graphics/system plugins, without network API usage by the application.)*
3. **No Collection of Children's or Sensitive Data:** In compliance with Microsoft Store Policies requirements (Sections 10.5.5 to 10.5.8), the application does not collect financial, health, biometric, or children's personal information.

---

## 3. Data Processed Locally by the User

All structural processing remains solely under user control in the RAM, CPU, and GPU (OpenGL 3.3 Core) of the local workstation:

- **In-Memory Engineering Data:** Nodal coordinates $(X, Y, Z)$, member connectivity, materials ($E, \nu, \alpha, \gamma$), cross-sections ($A, I_y, I_z, J$), roll angles, hinges, supports, elastic springs, prescribed displacements, and nodal/distributed loads.
- **Files Explicitly Saved by the User:** Structural models in open JSON **`.omniframe`** format, CAD/BIM interoperability files (**`.dxf`**, **`.ifc`**), technical reports (**`.pdf`**, **`.html`**, **`.md`**), and exported result datasets (**`.json`**, **`.csv`**, **`.vtk`**) are written **only** to the folder explicitly selected by the user through standard Windows dialogs or explicit CLI arguments.
- **Minimal Local UI Preferences:** Only the interface language choice (`language.json`), visual theme (`theme.json`), and OpenGL graphics profile (`graphics.json`) are saved locally as JSON files under the application's local data directory (`%LOCALAPPDATA%\OmniFrame\OmniFrame\`, or in the equivalent isolated container when run as an MSIX package).

---

## 4. Capability Declared in the MSIX Manifest (`runFullTrust`)

For MSIX package distribution on the Microsoft Store (Policy 10.6), the manifest [`repo/AppxManifest.xml`](../repo/AppxManifest.xml) declares exclusively the `<rescap:Capability Name="runFullTrust" />` capability, strictly required because this is a native Win32 C++23 / Qt 6 desktop application with OpenGL 3.3 Core acceleration and access to standard Windows file dialogs to open and save user-chosen `.omniframe` projects and reports. No location, camera, microphone, or contacts capabilities are declared or accessed.

---

## 5. Data Subject Rights, Control & Clean Uninstallation (Policy 10.2.7 / LGPD / GDPR)

The user exercises full control over all local data:
- **Access, Editing, Portability & Deletion:** You may freely open, copy, edit, or delete any `.omniframe`, `.dxf`, `.ifc`, `.pdf`, `.html`, `.md`, `.json`, `.csv`, `.vtk`, `language.json`, `theme.json`, or `graphics.json` file at any time.
- **Clean Uninstallation & Local Registry Control:** You may cleanly uninstall and remove OmniFrame 3D at any time via **Windows Settings → Apps → Installed Apps** or via the **Start Menu**. Removing the MSIX package deletes application binaries and virtualized state cleanly without leaving residual background services, drivers, or scheduled tasks behind. If you optionally enable Windows CLI registration (`Settings > Windows CLI`, `--install-windows-integration`, or `register_cli_windows.ps1`), only current-user registry keys (`HKCU` `PATH`, `App Paths`, and `.omniframe`) are written locally and can be completely removed via `--uninstall-windows-integration` or the uninstaller.

---

## 6. Privacy & Support Contact

- **Developer:** Fernando Nillsson Cidade
- **Email:** `linceu_lighthouse@outlook.com`
- **Support / Issues:** [https://github.com/fernandoncidade/OmniFrame_cpp/issues](https://github.com/fernandoncidade/OmniFrame_cpp/issues)

</details>
