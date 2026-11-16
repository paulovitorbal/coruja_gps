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

O `stdio` sai pelo **USB-CDC**, não pela UART — a UART0 fica para o GPS. Para
ler o log: `screen /dev/tty.usbmodem* 115200`.

### Versão da imagem

Desde o ADR 0012 a imagem carrega no bloco de metadados uma **versão
numérica e um hash**. Com a tabela de partições A/B, o bootrom sobe a partição
de maior versão. O número sai do `git describe` (ver
`cmake/versao_imagem.cmake`):

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
`git describe`, rode `cmake -S firmware -B firmware/build-pico` à mão.

Com **100 commits ou mais depois da tag**, o build para e pede tag nova: o
número colidiria com o da tag seguinte.

### Migração para partições A/B (uma vez, por cabo)

Só a primeira gravação precisa disto. Ela instala a tabela de partições do
RP2350, e daí em diante o firmware pode ser atualizado pela rede (ADR 0012).

| Região | Na flash | Tamanho |
| :--- | :--- | ---: |
| tabela de partições | `0x000000–0x002000` | 8 KiB |
| Coruja A | `0x002000–0x182000` | 1536 KiB |
| Coruja B | `0x182000–0x302000` | 1536 KiB |
| sem partição | `0x302000–0x400000` | 1016 KiB |

1. Gere a tabela:

   ```sh
   firmware/build-pico/_deps/picotool/picotool partition create \
       firmware/particoes.json firmware/build-pico/pt.uf2
   ```

2. Aparelho em `BOOTSEL`, arraste `pt.uf2` para o volume `RP2350`. O volume
   some sozinho quando a gravação termina.
3. ⚠️ **A partir daqui o aparelho fica sem firmware que suba.** A tabela foi
   gravada no começo da flash, onde ficava o início da imagem. É esperado.
   Sem imagem válida o bootrom deve voltar sozinho ao modo de gravação
   (🔬 não conferido). Se o volume `RP2350` não reaparecer, entre em
   `BOOTSEL` de novo.
4. Arraste `coruja_gps.uf2`. O bootrom grava a imagem numa das partições
   e reinicia nela.
5. Confira no `coruja.log`: a linha `boot:` traz o `git describe` gravado, a
   base carregou e o GPS está lendo.

Depois da migração, **todo UF2 arrastado vai para a partição que não está
rodando**, inclusive o das bancadas. 🔬 Qual partição sobe no boot seguinte
depende da versão das duas; a fase 2 do OTA confere isso.

Para desfazer, grave o `flash_nuke.uf2` oficial da Raspberry Pi, que apaga
a flash inteira, inclusive a tabela. Depois disso a gravação volta a ser a de
antes. 🔬 Não testado.

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
| tentativas por arquivo | 3, com 2 s entre elas |
| tentativas da confirmação | 3, **sem refazer o `PUT`** |
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
