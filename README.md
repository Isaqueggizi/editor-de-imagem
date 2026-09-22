# NexaFi

Aplicativo desktop nativo para Windows 10/11 que diagnostica a conexão Wi-Fi e aplica ajustes TCP **suportados pelo próprio Windows**.

## Recursos

- Painel moderno com adaptador, sinal, latência e **NexaScore**.
- Análise sem privilégios administrativos usando a API WLAN do Windows.
- **NexaRoute adaptativo**: seleciona, conforme a qualidade atual do enlace, um perfil conservador de autoajuste TCP, ativa RSS e limpa o cache DNS.
- Elevação solicitada pelo Windows apenas ao aplicar o perfil.
- Transparência: o programa não promete aumentar a velocidade contratada nem substitui um roteador ou sinal de qualidade.

## Executar

Baixe o artefato **NexaFi-Windows-x64** produzido pela ação **Windows build** (ou compile localmente) e execute `NexaFi.exe`. O binário é portátil e não requer instalação. Ao clicar em **Aplicar NexaRoute**, aceite a confirmação de administrador do Windows.

## Compilar

Pré-requisitos: CMake 3.20+ e MinGW-w64.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Para compilação cruzada em Linux:

```bash
cmake -S . -B build-win -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

## Restaurar padrão

Abra o Terminal como administrador e execute:

```powershell
netsh int tcp set global autotuninglevel=normal
netsh int tcp set global rss=enabled
```

## Privacidade e limites

Nenhum dado é coletado. A latência é medida por conexão TCP a `1.1.1.1:443`; nenhum conteúdo é enviado. O desempenho real também depende do plano, roteador, distância, interferência e servidor remoto.
