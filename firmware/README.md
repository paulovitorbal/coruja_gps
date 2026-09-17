# Firmware — instalação, build e testes

Regra 6 do `coding_rules.md`. Este documento cobre o firmware; a montagem do
hardware está no `bom_schematic.md` e os requisitos no `requirements.md`.

## Dependências

| Ferramenta | Para quê | Instalação no macOS |
| :--- | :--- | :--- |
| CMake ≥ 3.13 | build dos dois alvos | `brew install cmake` |
| Ninja | gerador | `brew install ninja` |
| GoogleTest | suíte de testes | `brew install googletest` |
| Arm GNU Toolchain | compilar para o RP2350 | `brew install --cask gcc-arm-embedded` |
| Pico SDK ≥ 2.0 | biblioteca do alvo | `git clone --depth 1 https://github.com/raspberrypi/pico-sdk` |

> ⚠️ **Não use a formula `brew install arm-none-eabi-gcc`.** Ela instala um
> toolchain **só de C**: não vem com a biblioteca padrão de C++, e qualquer
> `#include <cmath>` ou `<cstdio>` falha com *"No such file or directory"*.
> Verificado nesta máquina com a 16.2.0 — não há um único `cmath` no Cellar.
> O que serve é o **cask** `gcc-arm-embedded`, que é o toolchain oficial da Arm
> e traz a libstdc++. Ele instala via `.pkg` e **pede senha de administrador**,
> então precisa rodar num terminal interativo.

O Pico SDK precisa do `PICO_SDK_PATH` apontando para o clone, e do submódulo
`lib/tinyusb` inicializado:

```sh
export PICO_SDK_PATH="$HOME/pico-sdk"
git -C "$PICO_SDK_PATH" submodule update --init --depth 1 lib/tinyusb
```

## Testes no host

Não precisa de ARM nem de placa. Ver `docs/adr/0001`.

```sh
cmake -S firmware -B firmware/build-teste -G Ninja -DCORUJA_TESTES=ON
cmake --build firmware/build-teste
ctest --test-dir firmware/build-teste --output-on-failure
```

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
export PICO_SDK_PATH="$HOME/pico-sdk"
cmake -S firmware -B firmware/build -G Ninja -DPICO_BOARD=pico2_w
cmake --build firmware/build
```

Sai `firmware/build/coruja_gps.uf2`. Para gravar: segure `BOOTSEL`, conecte o
USB e copie o `.uf2` para o volume `RP2350` que aparece.

O `stdio` sai pelo **USB-CDC**, não pela UART — a UART0 fica para o GPS. Para
ler o log: `screen /dev/tty.usbmodem* 115200`.

## Configuração

```sh
python3 scripts/gera_config.py --destino /Volumes/NOME_DO_CARTAO
```

Gera o `coruja.cfg` no cartão e o `ConfigCalibracao.h` no firmware. Ver
`docs/adr/0002` para a divisão entre os dois.

> ⚠️ O `coruja.cfg` contém a senha do Wi-Fi e o **cartão é removível e legível
> por qualquer um**. Isso não é mitigável por software: use uma rede de
> convidados ou de IoT para o OTA, nunca a rede principal.

## Organização

```
firmware/src/
├── nucleo/          lógica pura — compila e é testada no host
├── log/             Logger (interface) + LoggerConsole
├── led/             LedRgb (interface) + LedRgbPwm (hardware)
├── buzzer/          a fazer
├── encoder/         a fazer
├── armazenamento/   a fazer — cartão SD
├── display/         a fazer — módulo ainda não chegou
└── gps/             a fazer — módulo ainda não chegou
firmware/test/
├── apoio/           mocks compartilhados
└── <espelha src/>
```

A regra de pertencimento está no `docs/adr/0001`: **se precisa de um cabeçalho
do Pico SDK, vai para `coruja_portes`; caso contrário, fica no alvo portável.**

## Estado

| Componente | Peça em mãos | Interface | Implementação | Testes |
| :--- | :---: | :---: | :---: | :---: |
| `nucleo` — tipos, Geo, LimiarInfracao, BaseRadares | — | — | ✅ | ✅ 58 casos |
| `log` — Logger, console, mock | — | ✅ | ✅ | ✅ |
| `led` — LED RGB | ✅ | ⬜ | ⬜ | ⬜ |
| `buzzer` — SFM-27 + BC337 | ✅ | ⬜ | ⬜ | ⬜ |
| `encoder` — KY-040 | ✅ | ⬜ | ⬜ | ⬜ |
| `armazenamento` — microSD | ✅ | ⬜ | ⬜ | ⬜ |
| `display` — 2,4" 320×240 | ❌ Correios | ⬜ | ⬜ | ⬜ |
| `gps` — NEO-M8N | ❌ Correios | ⬜ | ⬜ | ⬜ |

O parsing de NMEA e a máquina de estados de zona são lógica pura e podem ser
escritos e testados **antes** dos módulos chegarem, contra mocks.

### O que está verificado e o que não está

* **Verificado:** a suíte de 59 casos no host, com cobertura medida — 97,9% de
  linhas e **100% de ramos** no carregador, 100% em `Geo` e `LimiarInfracao`.
* **Não verificado:** a compilação para ARM. O alvo Pico **configura** sem
  erro, mas não foi possível compilá-lo nesta máquina porque o toolchain
  correto exige instalação com senha de administrador (ver o aviso acima).
  O `src/main.cpp` é um smoke test do alvo e nunca rodou numa placa.
