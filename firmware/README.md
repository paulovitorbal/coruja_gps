# Firmware — instalação, build e testes

Regra 6 do `coding_rules.md`. Este documento cobre o firmware; a montagem do
hardware está no `bom_schematic.md` e os requisitos no `requirements.md`.

## Dependências

| Ferramenta | Para quê | Instalação no macOS |
| :--- | :--- | :--- |
| CMake ≥ 3.13 | build dos dois alvos | `brew install cmake` |
| Ninja | gerador | `brew install ninja` |
| GoogleTest | suíte de testes | `brew install googletest` |
| Arm GNU Toolchain | compilar para o RP2350 | ver o aviso abaixo |
| Pico SDK ≥ 2.0 | biblioteca do alvo | `git clone --depth 1 https://github.com/raspberrypi/pico-sdk` |

> ⚠️ **Não use a formula `brew install arm-none-eabi-gcc`.** Ela instala um
> toolchain **só de C**: não vem com a biblioteca padrão de C++, e qualquer
> `#include <cmath>` ou `<cstdio>` falha com *"No such file or directory"*.
> Verificado com a 16.2.0 — não há um único `cmath` no Cellar.

O que serve é o toolchain oficial da Arm. O cask `gcc-arm-embedded` instala via
`.pkg` e **pede senha de administrador**, o que não funciona em terminal não
interativo. Dá para contornar extraindo o `.pkg` sem `sudo` e sem tocar em `/`:

```sh
brew fetch --cask gcc-arm-embedded          # só baixa, não instala
PKG=$(find ~/Library/Caches/Homebrew/downloads -name '*arm-gnu-toolchain*.pkg' | head -1)
pkgutil --expand-full "$PKG" /tmp/armx
mkdir -p ~/arm-gnu-toolchain && cp -R /tmp/armx/Payload/. ~/arm-gnu-toolchain/
export PATH="$HOME/arm-gnu-toolchain/bin:$PATH"
```

Ocupa ~1,0 GB. É o que está em uso aqui: **Arm GNU Toolchain 14.2.Rel1**.

O Pico SDK precisa do submódulo `lib/tinyusb` inicializado:

```sh
git -C "$HOME/pico-sdk" submodule update --init --depth 1 lib/tinyusb
```

### Variáveis de ambiente

Duas variáveis, e vale deixá-las permanentes em vez de exportar a cada sessão.

**fish** — em `~/.config/fish/config.fish`:

```fish
set -gx PICO_SDK_PATH $HOME/pico-sdk
fish_add_path $HOME/arm-gnu-toolchain/bin
```

**bash ou zsh** — em `~/.bashrc` ou `~/.zshrc`:

```sh
export PICO_SDK_PATH="$HOME/pico-sdk"
export PATH="$HOME/arm-gnu-toolchain/bin:$PATH"
```

Só o alvo Pico precisa delas. Os testes no host rodam sem nenhuma.

## Testes no host

Não precisa de ARM nem de placa. Ver `docs/adr/0001`.

```sh
cmake -S firmware -B firmware/build-host -G Ninja -DCORUJA_TESTES=ON
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
```

> O diretório é `build-host`, e não `build-teste`: é o nome que o
> `scripts/libera_fw.sh` e o `scripts/mutante.sh` usam. Enquanto este
> documento dizia outro nome, quem seguisse o README mantinha uma segunda
> árvore de build inteira — 24 MB — que nenhum script olhava.

### Dados de teste

A suíte roda **completa** num clone limpo. Ela usa uma fixture de 100 pontos
sintéticos versionada em `firmware/test/dados/fixture_igo8.txt`, que o CMake
converte em tempo de build chamando o próprio `converte.py` — então os testes
exercitam a cadeia inteira, conversor em Python e carregador em C++. Por isso
`python3` é dependência de build dos testes.

As coordenadas da fixture são pontos no Atlântico Sul aberto: **nenhuma é um
local real de fiscalização**. Ver `firmware/test/dados/README.md`.

Um teste extra carrega uma base completa e **se pula** por padrão. Para
exercitá-lo:

```sh
python3 converte.py sua_base.txt radares.bin
cmake -S firmware -B firmware/build-teste -G Ninja -DCORUJA_TESTES=ON \
      -DCORUJA_BASE_REAL="$PWD/radares.bin"
```

### Rodar um teste só

```sh
./firmware/build-teste/test/testes --gtest_filter='Decodificador.*'
./firmware/build-teste/test/testes --gtest_filter='*Wraparound*'
./firmware/build-teste/test/testes --gtest_list_tests   # lista tudo
```

### Cobertura

O mínimo do projeto é 80%.

```sh
cmake -S firmware -B firmware/build-cov -G Ninja \
      -DCORUJA_TESTES=ON -DCORUJA_COBERTURA=ON
cmake --build firmware/build-cov && ./firmware/build-cov/test/testes
cd firmware/build-cov && xcrun gcov -b src/CMakeFiles/coruja_nucleo.dir/nucleo/*.gcda
```

## Firmware para o Pico 2 W

```sh
cmake -S firmware -B firmware/build-pico -G Ninja -DPICO_BOARD=pico2_w \
      -DPICO_TOOLCHAIN_PATH="$HOME/arm-gnu-toolchain"
cmake --build firmware/build-pico
```

Com as variáveis já no config do shell. Sem elas, prefixe a linha do `cmake`
com `PICO_SDK_PATH=$HOME/pico-sdk PATH=$HOME/arm-gnu-toolchain/bin:$PATH`, que
funciona igual em fish, bash e zsh.

Sai `firmware/build-pico/coruja_gps.uf2`.

### Conferir o orçamento de memória

```sh
arm-none-eabi-size firmware/build-pico/coruja_gps.elf
```

Medição de 2026-09-17, com núcleo, log, LED e encoder:

| | Uso | De |
| :--- | ---: | ---: |
| Flash | **66,6 KiB** | 4096 KiB |
| SRAM | **284,2 KiB** | 520 KiB |
| Livre | **235,8 KiB** | |

A base é reserva estática de 24.000 pontos, 281,2 KiB. Ver `docs/adr/0006`
para por que isso **não** é o teto de 40.000 do formato. Para gravar: segure `BOOTSEL`, conecte o
USB e copie o `.uf2` para o volume `RP2350` que aparece.

⚠️ **Aparelho novo, ou que ainda não tem partições A/B:** grave antes, uma
vez, a tabela de partições (seção "Migração para partições A/B", abaixo). Sem
ela o firmware roda igual, mas o aparelho nunca vai poder receber firmware
pela rede.

O `stdio` sai pelo **USB-CDC**, não pela UART — a UART0 fica para o GPS. Para
ler o log: `screen /dev/tty.usbmodem* 115200`.

### Versão da imagem

Desde o ADR 0012 a imagem carrega no bloco de metadados uma **versão
numérica e um hash**. Com a tabela de partições A/B, o bootrom sobe no boot
normal a partição de maior versão. Logo depois de uma gravação por cabo, sobe
a gravada, qualquer que seja a versão (ver a migração, abaixo). O número sai
do `git describe` (ver `cmake/versao_imagem.cmake`):

| `git describe` | versão |
| :--- | :--- |
| `v0.2.23-4-g86d7a7b` | 2.2304 |
| `v0.2.24-0-g…` (em cima da tag) | 2.2400 |

```sh
firmware/build-pico/_deps/picotool/picotool info -a firmware/build-pico/coruja_gps.uf2
```

Sai `version: 2.2304` e `hash: verified`.

⚠️ **O número é lido na configuração, não no build.** O build reconfigura
sozinho quando o `logs/HEAD` muda (commit, *amend*, *checkout*) e quando uma
tag nova aparece em `refs/tags`. Conferido em 16/11/2026 com um commit vazio e
uma tag descartável. Se o `picotool info` mostrar versão diferente da do
`git describe`, rode `cmake -S firmware -B firmware/build-pico` à mão. Na
liberação, o `scripts/libera_fw.sh` faz essa conferência sozinho e desfaz a
tag se o número não bater com ela.

Com **100 commits ou mais depois da tag**, o build para e pede tag nova: o
número colidiria com o da tag seguinte. **Sem tag nenhuma** (o zip "Source
code" da release, um clone raso), o build segue com um aviso e a imagem sai
sem versão, que o bootrom lê como 0.0; a liberação a recusa.

O hash é conferido **a cada boot**, com ou sem tabela: uma imagem corrompida
não sobe, e o aparelho cai no `BOOTSEL`. 🔬 Isso custa tempo de boot, de 0,2 s
a 1 s pela estimativa, a medir na linha `boot: pronto`.

### Migração para partições A/B (uma vez, por cabo)

Só a primeira gravação precisa disto. Ela instala a tabela de partições do
RP2350, e daí em diante o firmware pode ser atualizado pela rede (ADR 0012).

| Região | Na flash | Tamanho |
| :--- | :--- | ---: |
| tabela de partições | `0x000000–0x002000` | 8 KiB |
| Coruja A | `0x002000–0x182000` | 1536 KiB |
| Coruja B | `0x182000–0x302000` | 1536 KiB |
| sem partição | `0x302000–0x400000` | 1016 KiB |

1. Pegue a tabela. Toda release, desde a v0.2.24, anexa a tabela como
   `coruja_gps-vX.Y.Z-particoes.uf2`, com o SHA-256 ao lado. Sem release,
   gere a partir do build:

   ```sh
   firmware/build-pico/_deps/picotool/picotool partition create \
       firmware/particoes.json firmware/build-pico/pt.uf2
   ```

2. Aparelho em `BOOTSEL`, arraste a tabela (`pt.uf2` ou o
   `…-particoes.uf2` da release) para o volume `RP2350`. O volume
   some sozinho quando a gravação termina.
3. ⚠️ **A partir daqui o aparelho fica sem firmware que suba.** A tabela foi
   gravada no começo da flash, onde ficava o início da imagem. É esperado.
   Sem imagem válida, o bootrom volta sozinho ao modo de gravação (pelo
   código dele; 🔬 não conferido no aparelho). Se o volume `RP2350` não
   reaparecer, entre em `BOOTSEL` de novo.
4. Arraste `coruja_gps.uf2` (ou o `coruja_gps-vX.Y.Z.uf2` da release). O
   bootrom grava a imagem na partição **B** (`0x182000`) e reinicia nela.
   É a B, e não a A, porque com as duas vazias o bootrom escolheria a A para
   subir, e um UF2 arrastado vai sempre para a que ele **não** subiria.
5. Confira no `coruja.log`: a linha `boot:` traz o `git describe` gravado, a
   base carregou e o GPS está lendo.

**Depois da migração, o que acontece a cada UF2 arrastado:**

- Ele vai para a partição que não está rodando. As gravações alternam: B, A,
  B…
- Ele **sobe logo em seguida, qualquer que seja a versão**: o bootrom faz um
  boot "de atualização" apontado para a partição recém-gravada.
- ⚠️ **Se a versão dele for menor ou igual à da outra partição, o bootrom
  apaga o começo da outra.** A cópia anterior se perde. Arrastar uma bancada,
  ou a v0.2.23, que não tem versão e conta como 0.0, apaga o produto. Para
  voltar, arraste o produto de novo. A/B não protege contra gravação de versão
  mais velha por cabo.
- Só quando a versão sobe, a outra cópia fica guardada. No boot normal sobe a
  de maior versão; em empate, a A.

Para desfazer, grave o `flash_nuke.uf2` oficial da Raspberry Pi, **o build
para RP2350** (um de RP2040 é ignorado sem aviso). Ele apaga a flash inteira,
inclusive a tabela, e a gravação volta a ser a de antes.

🔬 Tudo isto saiu da leitura do código do bootrom (`raspberrypi/pico-bootrom-rp2350`:
`varm_nsboot.c`, `varm_flash_boot.c`, `usb_virtual_disk.c`), em 16/11/2026.
Nada foi conferido no aparelho ainda.

### OTA de firmware — medidas

Linha de base para o ADR 0012, tirada em 16/11/2026 do `coruja.log` que o
aparelho mandou ao servidor (histórico até 11/11/2026). Só entraram as linhas
`boot:`, `gps: receptor configurado`, `ota:`, `base: recebido` e `mem:`.

**1. Do boot ao GPS respondendo.** O log não tem linha para a primeira
sentença NMEA. A mais próxima é `gps: receptor configurado`, que só sai depois
que o receptor respondeu no baud achado e confirmou a configuração.

| Linha | Partidas | Mínimo | Mediana | Máximo |
| :--- | ---: | ---: | ---: | ---: |
| `gps: receptor configurado` | 26 | 2,2 s | 2,5 s | 4,0 s |
| `boot: pronto` (últimas 30, v0.2.21 e v0.2.23) | 30 | 2,5 s | 2,8 s | 4,4 s |

Fica longe dos 8 s em que o plano mandava parar, e o prazo de 10 s da decisão
4 se mantém. No histórico inteiro há 27 partidas com `boot: pronto` acima de
6 s (a pior, 8,7 s), todas de v0.2.13 a v0.2.15.

**2. Vazão do download da base.** Intervalo entre a linha `ota: versao` e a
`ota: base gravada`, com carimbo de 1 s, então cada intervalo erra ±1 s. Inclui
a gravação no cartão.

| Data | Bytes | Intervalo | Vazão |
| :--- | ---: | ---: | ---: |
| 11/10/2026 | 220 076 | 8 s | 27,5 KB/s |
| 15/10/2026 | 220 256 | 40 s | 5,5 KB/s |
| 20/10/2026 | 220 388 | 15 s | 14,7 KB/s |
| 26/10/2026 | 220 424 | 8 s | 27,6 KB/s |
| 28/10/2026 | 220 688 | 10 s | 22,1 KB/s |
| 02/11/2026 | 220 700 | 13 s | 17,0 KB/s |

Mediana de 19,5 KB/s. O OTA baixa o UF2, de 1 432 064 B: **52 s** na melhor
vazão vista, **73 s** na mediana e **4 min 20 s** na pior. É estimativa: o OTA
grava na flash, não no cartão, e 🔬 o custo de apagar e gravar a flash não
está aí.

**3. Pico de memória.** A linha `mem:` sai ao fim de cada sessão de rede, com
o pico acumulado desde o boot (17 sessões, em 14 execuções).

| | Pico | De | Sobra |
| :--- | ---: | ---: | ---: |
| heap do lwip | 14 924 B | 15 776 B | 852 B |
| heap do sistema | 6 244 B | — | — |
| pilha | 21 172 B | 24 576 B | 3 404 B |

- ⚠️ **A pilha vem subindo:** 18 588 B em 08/10 e 21 172 B em 28/10, junto
  com o firmware. O caminho de gravação do OTA soma pilha em cima disso, e a
  sobra é de 3,4 KB. Medir na bancada antes de confiar.
- O heap do lwip chegou aos ~14,9 KB sempre numa sessão sem download (a
  consulta falhou ou a base já estava em dia). Nas cinco sessões com download
  em que o pico ainda não estava alto, ele ficou em 3 096 B: baixar 220 KB não
  passou disso. 🔬 Qual etapa enche o heap do lwip não está no log.

## Configuração

```sh
python3 scripts/gera_config.py --destino /Volumes/NOME_DO_CARTAO
```

Gera o `coruja.cfg` no cartão — **sem comentários**. Eles ficam no
`coruja.cfg.exemplo`, versionado, que é onde se lê o que cada chave faz.

> ⚠️ O firmware carrega o arquivo INTEIRO para a RAM, e os comentários deste
> gerador são quatro quintos dele. O arquivo enxuto tem ~200 bytes; com
> comentários, passava de 5 KB — e o leitor reservava 2 KB. O sintoma foi
> `sem configuração utilizável` no OTA e o aparelho sem nome na tela, com um
> arquivo que lia perfeitamente em qualquer outra ferramenta. Hoje o leitor e
> o gravador usam a **mesma** constante, e há teste que falha se divergirem.

**Só entra nele o que varia por instalação:**

| Chave | |
| :--- | :--- |
| `wifi_ssid_N` / `wifi_senha_N` | até 5 redes, **em ordem de prioridade** |
| `url_versao` | devolve uma linha de texto qualquer, comparada como texto |
| `url_base` | entrega o `radares.bin`, em HTTPS |
| `url_envio` | para onde subir log, viagens e infrações — **vazio desliga** |
| `token_aparelho` | o segredo que identifica esta unidade no servidor |

Brilho, fuso, tolerâncias e calibração do LED ficam **no código**. O critério está no
`docs/adr/0002`: se mudar o valor muda o comportamento de segurança, é especificação e
não configuração.

> ⚠️ O `coruja.cfg` contém a senha do Wi-Fi e o **cartão é removível e legível
> por qualquer um**. Isso não é mitigável por software: use uma rede de
> convidados ou de IoT para o OTA, nunca a rede principal.

#### Enviar dados ao servidor

O item **enviar dados** do menu sobe `coruja.log`, `infracoes.log` e os
registros de viagem, e **apaga do cartão só o que o servidor confirmar ter
guardado** — manda, pergunta o CRC-32, compara, e só então apaga. Confirmar
antes de destruir dispensa manifesto no cartão e nunca perde dado por falha de
rede.

Precisa das **duas** chaves. Só a URL significaria mandar para um servidor que
aceita de qualquer um, e o firmware recusa a configuração pela metade.

O `token_aparelho` vai em **todas** as requisições, não só nos envios: é por
ele que o servidor sabe qual unidade está falando, e é o que faz os registros
de dois carros não se misturarem. Se o servidor tiver lista de aparelhos, ele
recusa até o download da base sem token.

Ele vai no cabeçalho `X-Coruja-Token`, em **todas** as rotas.

Houve uma versão em que ia como `?t=` na URL dos GET, porque o `http_client`
do lwIP montava o pedido internamente e não tinha onde encaixar cabeçalho
próprio. Com TLS o transporte foi reescrito sobre `altcp`, o gancho passou a
existir, e o segredo saiu da URL — onde entrava no log de acesso do servidor
e de qualquer proxy no caminho.

> ✅ **O envio fala TLS** desde 2026-10-06, pelo mesmo `ClienteTls` do
> download. O certificado é verificado contra três raízes embutidas no
> firmware, e `MBEDTLS_SSL_VERIFY_REQUIRED` faz o handshake abortar quando
> não bate — o padrão do lwIP é `OPTIONAL`, que verifica e deixa passar.
>
> ⚠️ Isso exige **o relógio acertado**: sem hora, todo certificado é recusado
> por "ainda não vale". Ver `servidor_ntp`.

Um arquivo que **cresceu durante o envio** fica no cartão, mesmo entregue:
apagar depois de confirmar N bytes destruiria os que vieram depois. Ver
`servidor/README.md` para o outro lado.

Isso **deixou de ser rotina**. Enquanto o log da remessa ia para o próprio
`coruja.log`, ele crescia sempre e nunca era apagado; desde que o `main.cpp`
desvia o log para `remessa.log` durante o envio, nenhum arquivo deveria
crescer. Um arquivo retido hoje quer dizer que o cartão não soltou — e a tela
passou a dizer isso, com `ENTREGUE; NAO APAGUEI`, em vez do antigo `ENVIADO`.
A distinção importa porque arquivo retido **volta inteiro na remessa
seguinte**, e era essa volta que parecia "o envio recomeça do zero".

### Retentativa, e o teto que ela exigiu

Doze arquivos são vinte e quatro conexões TCP — um `PUT` e uma consulta de CRC
por arquivo, cada uma com resolução de nome e aperto de mão. Até 11/11/2026
qualquer uma delas que falhasse condenava o arquivo da vez, sem segunda
chance; era por isso que a remessa parecia falhar "no último arquivo", quando
o último é só o que teve mais sorteios antes dele.

| | |
| :--- | :--- |
| tentativas por arquivo | 3, com 2 s entre elas — só o que pode mudar (ver abaixo) |
| tentativas da confirmação | 3, **sem refazer o `PUT`**, com a mesma regra |
| parada | 2 arquivos seguidos falhando pelo caminho até o servidor; certificado recusado, no 1º |
| teto do conjunto | 10 min, conferido entre arquivos e entre tentativas |
| arquivos por remessa | 40 (era 12) |

A confirmação é repetida sozinha porque, quando ela falha, o `PUT` já entrou e
o servidor já guardou: refazer o arquivo inteiro reenvia o mesmo conteúdo. O
nome do servidor é resolvido **uma vez por sessão** (`CacheDeHost`) e o
endereço é descartado assim que um pedido não chega a resposta nenhuma.

> ⚠️ **O envio bloqueia o aparelho inteiro, e não há como cancelá-lo.**
> Enquanto ele roda o laço principal não volta: o GPS não é lido, não há
> alerta, e o anel da UART enche e descarta bytes. Em 11/11/2026 um envio de
> backlog (84 arquivos já no servidor) coincidiu com uma volta de 146,8 s e
> 93 KB de NMEA perdidos no `vigia`. A causa é a coincidência de horário, não
> uma medição do envio em si.
>
> O tempo máximo vinha dos limites por etapa, não de um total: varredura 12 s,
> associação 20 s e, **por arquivo**, até 180 s de envio mais 30 s de consulta
> de CRC — doze arquivos davam 42 minutos no pior caso, e a retentativa
> multiplicaria isso por três. **Desde 11/11/2026 há um teto do conjunto de
> 10 minutos** (`kTetoDaRemessaMs`), conferido entre arquivos e entre
> tentativas. Ele estoura para cima em até uma tentativa, porque abortar um
> `PUT` pela metade deixaria o servidor com meio arquivo; o que não foi
> tentado fica para a próxima remessa e a tela diz `ENVIADO; FALTA MAIS`.
>
> Continua **não havendo cancelamento pelo encoder**: o teto limita o dano,
> não devolve o controle.
>
> **O envio só começa por clique em "enviar dados"** — nada o dispara sozinho.
> Mas o menu abre com o carro parado, e **sem nenhum fix desde o boot o
> `DetectorParado` conta como parado após 3 s** (para os ajustes ficarem
> acessíveis em garagem coberta). Ou seja, em casa, antes do fix, ele abre.
>
> Por isso: **envie o backlog em casa, com tempo, e nunca com pressa para
> sair.** Com 40 arquivos por remessa em vez de 12, um backlog de 84 sai em
> três idas ao menu em vez de sete — se o enlace der conta dentro do teto. Se sair andando durante o envio, o trecho fica sem alerta até ele
> terminar. Enviar com frequência mantém cada remessa curta. O módulo GPS
> segue rodando sozinho durante o envio; só os bytes enviados à placa se
> perdem (não verificado como o firmware retoma depois).

### O erro que não muda

Até a v0.2.24 todo erro era tentado três vezes por arquivo, e a remessa só
parava no teto. Um token errado, uma `url_envio` errada ou um relógio sem hora
— que faz todo certificado ser recusado — voltam iguais dois segundos depois:
com 40 arquivos e um 401, eram 59 `PUT` e os dez minutos inteiros de aparelho
preso. Desde a v0.2.25 cada tentativa termina num desfecho, e só é repetido o
que pode mudar (`RemessaDados::classifica`):

| desfecho | repete? | na conta da parada |
| :--- | :--- | :--- |
| sem resposta: DNS, conexão, tempo esgotado, queda | sim | conta |
| erro de momento: 5xx, 408, 429 | sim | conta |
| recusa: 3xx, 4xx (token, `url_envio`) | não | conta |
| certificado recusado | não | para no 1º |
| 413 | não | zera |
| CRC diferente | sim, reenviando | zera |
| cartão que não lê, URL que não forma | cartão sim, URL não | fica de fora |

Vale o **status**, e não o erro: o 401 que chega com a conexão caindo atrás (o
servidor recusa sem ler o corpo) é recusa. **Dois arquivos seguidos** que
falham pelo caminho até o servidor encerram a remessa, e o resto fica no
cartão para a próxima. Dois, e não um, porque um arquivo sozinho não separa o
caminho do arquivo. A conta zera quando o servidor responde de um jeito que
prova o caminho — a entrega, o 413, o CRC diferente —, e o certificado
recusado para no primeiro: sem relógio, aperto de mão nenhum passa.

Uma remessa condenada custa hoje, além da associação e do NTP: um aperto de
mão (certificado), dois pedidos (recusa), ~92 s (DNS morto) ou ~98 s (servidor
que não atende). O teto de 10 min fica como última guarda, para o enlace vivo
e lento — e é ele que ainda segura um `PUT` que anda mas não termina em 180 s,
tentado três vezes por arquivo.

Sem nenhuma entrega, a tela diz o motivo. A falha do caminho vale mais que a
do arquivo, porque é ela que pede conserto; entre iguais, a mais recente.

| tela | o que houve | onde olhar |
| :--- | :--- | :--- |
| `SERVIDOR NAO RESPONDEU` | DNS, conexão ou tempo esgotado | rede, servidor fora do ar |
| `CERTIFICADO RECUSADO` | o TLS recusou o certificado | relógio: `servidor_ntp`, fix do GPS |
| `SERVIDOR NAO ACEITOU` | respondeu e recusou: 3xx, 4xx, 5xx, CRC | `token_aparelho`, `url_envio`, servidor |
| `CARTAO ILEGIVEL` | o cartão não entregou os arquivos | cartão |
| `SEM CONFIGURACAO` | `url_envio`, `token_aparelho` ou rede faltando ou inválidos | `coruja.cfg` |

### Atualizar a base

O item **atualizar base** do menu pergunta a `url_versao` e compara a linha
com o `versao.txt` do cartão. **Versão igual não basta:** o `versao.txt`
descreve o `radares.bin`, e se o boot recusou esse arquivo e caiu no
`radares.bak` — ou não achou base nenhuma —, o aparelho baixa de novo mesmo
com a versão igual. Até a revisão de 17/11/2026 ele respondia `JA ESTA EM
DIA`, e o menu não tinha como tirar o aparelho da reserva.

Quem sabe de onde a base veio é a carga do boot, e não o OTA relendo o
arquivo: reler custaria 214 KB de cartão a cada clique, e a carga confere
mais (ordem e domínio) do que a verificação do download.

### Testes do gerador

```sh
python3 -m unittest discover -s scripts/testes
```

Usa `unittest` e não pytest: todo o Python deste projeto é **stdlib-only** por escolha.

Dois testes amarram o gerador ao parser em C++, que estão em linguagens diferentes: o
C++ lê o `coruja.cfg.exemplo` versionado e falha se o formato divergir, e o Python
compara suas constantes de limite com as do `Configuracao.h`.

## Organização

```
firmware/src/
├── placa/           Pinos.h — mapa único de GPIO, com invariantes em static_assert
├── nucleo/          lógica pura — tipos, Geo, base de radares, config
├── log/             Logger (interface) + LoggerConsole
├── led/             Cor, LedRgb (interface), LedRgbAnodoComum (hardware)
├── encoder/         DecodificadorQuadratura e AntiRepique (puros),
│                    Encoder (interface), EncoderKy040 (hardware)
├── app/             ModoTesteEncoder e ModoCalibracao — lógica de bancada
├── buzzer/          a fazer
├── armazenamento/   a fazer — cartão SD
├── display/         a fazer — módulo ainda não chegou
└── gps/             a fazer — módulo ainda não chegou
firmware/test/
├── apoio/           mocks compartilhados
└── <espelha src/>
```

A regra de pertencimento está no `docs/adr/0001`: **se precisa de um cabeçalho
do Pico SDK, vai para `coruja_portes`; caso contrário, fica no alvo portável.**

### Mapa de pinos

`src/placa/Pinos.h` é a **fonte única** dos 18 GPIO que o firmware usa. Nenhuma classe
declara pino próprio: `LedRgbAnodoComum` e `EncoderKy040` consomem as constantes de lá.

Três invariantes são garantidas pelo **compilador**, não por revisão de código:

| Invariante | Mensagem se violada |
| :--- | :--- |
| Nenhum GPIO usado duas vezes | `dois periféricos foram atribuídos ao mesmo GPIO` |
| Todo GPIO existe no cabeçalho e não é do CYW43 (23, 24, 25, 29) | `algum GPIO não existe no cabeçalho do Pico...` |
| A contagem de 18 não mudou sem revisão | `a contagem de GPIO mudou: confira o bom_schematic.md...` |

As três foram **verificadas quebrando o arquivo de propósito** e conferindo que o build
para com a mensagem certa. Os testes de `placa/PinosTest.cpp` acrescentam o que o
`static_assert` não alcança: eles **fixam os valores** contra a fiação documentada, para
que trocar o GPIO 2 pelo 3 falhe dizendo qual periférico mudou de pino.

## Estado

| Componente | Peça em mãos | Interface | Implementação | Testes |
| :--- | :---: | :---: | :---: | :---: |
| `placa` — mapa de pinos | — | — | ✅ | ✅ |
| `nucleo` — tipos, Geo, LimiarInfracao, BaseRadares, LeitorConfig | — | — | ✅ | ✅ |
| `log` — Logger, console, mock | — | ✅ | ✅ | ✅ |
| `led` — LED RGB | ✅ | ✅ | ✅ | ✅ |
| `encoder` — KY-040 | ✅ | ✅ | ✅ | ✅ |
| `app` — modos de teste e de calibração | ✅ | — | ✅ | ✅ |
| `buzzer` — piezo ativo + BC337 | ✅ | ⬜ | ⬜ | ⬜ |
| `armazenamento` — microSD | ✅ | ⬜ | ⬜ | ⬜ |
| `display` — 2,4" 320×240 | ❌ Correios | ⬜ | ⬜ | ⬜ |
| `gps` — NEO-M8N | ❌ Correios | ⬜ | ⬜ | ⬜ |

O parsing de NMEA e a máquina de estados de zona são lógica pura e podem ser
escritos e testados **antes** dos módulos chegarem, contra mocks.

### O que está verificado e o que não está

* **Verificado:** a suíte de **143 casos** no host, com cobertura medida — 97,9%
  de linhas e **100% de ramos** no carregador, e **100% de linhas e ramos** em
  `Geo`, `LimiarInfracao`, `DecodificadorQuadratura`, `AntiRepique` e
  `ModoTesteEncoder`.
* **Verificado:** a compilação para ARM, com o toolchain oficial 14.2.Rel1 e o
  `pico-sdk` 2.3.1. Sai `coruja_gps.uf2` de 126 KB, e o orçamento de memória
  passou a ser medido no linker.
* **Não verificado:** nada rodou numa placa. O `src/main.cpp` compila e liga,
  mas o comportamento do modo de teste — inclusive se esquerda e direita saem
  na ordem certa — só se confirma na bancada.

## Modo de bancada de rede — o que o `main.cpp` roda hoje

Exercita o caminho de atualização OTA de ponta a ponta, com tudo saindo no
console USB.

| Gesto | O que acontece |
|---|---|
| girar à **direita** | agrava o estado de via: segura → âmbar → rosa → perigo |
| girar à **esquerda** | alivia, no sentido inverso |
| **clicar** | conecta no Wi-Fi, consulta a versão, baixa se mudou, verifica, desconecta |

As quatro cores são as medidas no R-05, não valores nominais. O ciclo **satura
nas pontas** em vez de dar a volta: numa tela de diagnóstico, voltar sozinho ao
verde depois do vermelho esconde justamente que se chegou ao fim.

### A configuração vem do cartão, lida no clique

Nada de credencial embutida no firmware — o RNF03 é explícito, e ler no clique
tem uma consequência prática boa: **trocar o cartão passa a valer sem
reiniciar** o aparelho, e a configuração nunca fica velha em RAM.

Ponha na raiz do cartão o `coruja.cfg` gerado por:

```bash
python3 scripts/gera_config.py --permitir-http --destino /Volumes/NOME_DO_CARTAO
```

No boot o cartão é só **inspecionado** pelo pino `DET` (GPIO 14), e o estado
sai no console — presente ou ausente. Era o que faltava: sem essa linha, os
dois casos davam exatamente o mesmo console, e a diferença só aparecia depois,
disfarçada de outro erro.

#### Qual partição

A que **contém o arquivo**, não a primeira que for FAT. Num cartão de duas
partições com a configuração na segunda, o padrão do FatFs diria "sem
configuração" com o arquivo no cartão. Ver ADR 0007 e R-38.

Partição lógica dentro de estendida continua fora do alcance, e o log diz
quantas partições FAT foram montadas — é essa a pista quando o arquivo está no
lugar errado.

### O que sai no console

Na varredura, cada rede vista com canal e RSSI, e quais são da lista. Depois o
IP obtido, o `GET`, e — no download — os seis campos do cabeçalho do
`radares.bin` ao lado do que de fato chegou:

```
[base] ---- cabecalho do radares.bin recebido ----
[base]   magic        : 'RDR1' (0x31524452)
[base]   versao       : 1
[base]   exp_escala   : 5  (coordenadas x10^5)
[base]   tam_registro : 12 B
[base]   n_pontos     : 18294  (declarados)
[base]   crc32        : 0xD290D536  (do cabecalho)
[base] ---- e o que de fato chegou ----
[base]   bytes        : 219544  (219528 de dados)
[base]   pontos       : 18294  (confere)
[base]   crc32        : 0xD290D536  (confere)
```

### O que ainda não acontece

**Nada é gravado.** O destino do `radares.bin` é o cartão, e o leitor não
existe — o arquivo é verificado *em fluxo* e descartado. A versão confirmada
vive em RAM, então reiniciar faz o próximo clique baixar de novo.

A verificação em fluxo é obrigatória, não uma escolha: o arquivo tem 214 KB e
a base já reserva 281 KB dos 520 KB da placa. Guardar o download inteiro para
só então validá-lo não cabe — e não vai caber nem com o cartão, porque aí o
destino é o cartão.

O `VerificadorDownload` confere cabeçalho, contagem e CRC-32, **na mesma ordem**
do `carrega_base`. Ordenação por latitude e domínio de cada registro exigem os
bytes na mão e continuam com a carga. Um arquivo aprovado no download ainda
pode ser recusado na carga, e isso é correto: são perguntas diferentes.

### Limites conhecidos

- **Sem TLS.** Uma URL `https` é recusada com mensagem própria, não baixada em
  claro: baixar em claro o que diz `https` seria mentir sobre o RF05.2 no ponto
  exato em que ele importa. Ver o README do `servidor/`.
- **Sem IPv6.** `http://[::1]:8080/x` é recusado com motivo próprio, em vez de
  ir falhar no DNS com uma mensagem que não explica nada.
- **Conexão episódica.** O rádio sobe no clique e cai ao fim, sempre — inclusive
  nos caminhos de erro.

## Modo de calibração

O firmware atual roda o **modo de calibração**, que fecha a metade pendente do
**R-05**: as razões de PWM do âmbar e do rosa. Não precisa de GPS, cartão nem
display.

| Ação | Efeito |
| :--- | :--- |
| **Girar** | ajusta o canal variável do item atual, 5 de duty por detente |
| **Clicar** | avança: vermelho → verde → azul → âmbar → rosa → volta |

Nas compostas o **vermelho fica fixo em 100%** e o encoder move o outro canal.
Não é simplificação: é a forma como `led/Calibracao.h` guarda as cores, com o
canal vermelho em 255 e só o secundário variando.

Ao dar a volta, o log imprime o bloco pronto para transcrever:

```
[INFO ] calib: ---- calibracao do R-05, para o gera_config.py ----
[INFO ] calib:   duty do verde no ambar : 0.45
[INFO ] calib:   duty do azul no rosa   : 0.60
[INFO ] calib:   (o vermelho das duas fica em 1,00 por construcao)
[INFO ] calib:   canais isolados: R=1.00  G=1.00  B=1.00
```

> ⚠️ **Julgue o rosa ao lado do vermelho, e sob sol direto.** É o item crítico:
> se os dois se confundirem, a faixa de margem parece Zona de Perigo ao
> motorista, que é o oposto da intenção do RF03.4.

De passagem ele valida a fiação inteira: os três canais acendem isolados —
inclusive o **verde**, que o modo de teste anterior nunca acendia — e o giro e
o clique exercitam o encoder. Se o giro à direita **diminuir** o brilho em vez
de aumentar, `CLK` e `DT` estão invertidos: construa com
`EncoderKy040(true, /*invertido=*/true)` em vez de mexer na fiação.

### O modo de teste anterior continua disponível

Esquerda vermelho, direita azul, clique apaga. Troque `ModoCalibracao` por
`ModoTesteEncoder` no `src/main.cpp` — a classe continua no projeto e testada.
