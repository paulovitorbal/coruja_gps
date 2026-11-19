# Simulador de GPS

Faz o papel do NEO-M8N numa porta serial, para que o firmware possa ser
exercitado sem o módulo — e sem sair de casa.

```bash
python3 simula_gps.py
```

Ele cria um terminal virtual e imprime o caminho do dispositivo. Qualquer
programa abre esse caminho como abriria `/dev/cu.usbserial…`:

```
  rota  : rota_eixao.csv, 16.53 km por sentido
  volta : 33.05 km (ida e volta)
  serial: /dev/ttys012
  taxa  : 4 Hz
  teclas: a acelera · d desacelera · 0 para · q sai

  volta   1 |  12.4% | 60.0 km/h | norte | -15.82931,-47.92104
```

## A rota não foi inventada

Ela é a linha de centro do **Eixão (DF-002)**, derivada de 450 nós do
OpenStreetMap — `ref=DF-002` na caixa do Plano Piloto. O `gera_rota.py`
consulta a Overpass API e escreve o `rota_eixao.csv`, que é versionado para o
simulador rodar sem rede.

Coordenadas inventadas passariam por reais e ninguém saberia. Um simulador que
anda por uma via imaginária valida o firmware contra ficção — e o pior é que
ele *pareceria* funcionar.

**Por que isso importa na prática:** ao longo de uma volta o veículo passa a
menos de 300 m de **50 radares reais** da base, sendo 35 a menos de 100 m. A
maioria é de 60 km/h, com alguns de 80 — e há um de limite `0`, que é o caso
que o R-19 levantou. Dá para exercitar as quatro zonas só variando a
velocidade.

## Duas decisões que o simulador copia do módulo real

**Rumo em branco com o veículo parado.** O NEO-M8N não informa direção sem
deslocamento. Reproduzir isso é metade do valor do simulador: é o caso que
quebra parser desatento, que trataria o campo vazio como zero e apontaria o
veículo para o norte.

**4 Hz**, que é a taxa do RF01.4. Emitir mais rápido esconderia problemas de
acompanhamento que o aparelho teria no carro.

## Limitações honestas

**A linha de centro é média das duas pistas.** Elas ficam a ~40 m uma da
outra, e a suavização deixa uma irregularidade residual de ~5° por trecho. Para
alertas com raio de 300 m isso é ruído; para julgar filtro de rumo por sentido,
não confie.

**Não há ruído, nem perda de fix, nem cold start.** Todo fix é válido. Testar
o RF07 e o comportamento sem sinal exige acrescentar isso — hoje não está.

**A hora é a do computador**, em UTC, não a do relógio do satélite. O carimbo
anda em passos de 250 ms (`.00`, `.25`, `.50`, `.75`) a partir do instante em
que o simulador subiu, e não é lido do relógio a cada sentença.

**Ele não responde ao ACK do UBX.** O firmware configura o módulo por quadros
UBX binários (`CFG-RATE`, `CFG-MSG`, `CFG-PRT`…) e espera o `ACK-ACK` de cada
um; o simulador ignora o que chega e só emite RMC. Para o `ConfiguradorGps`
isso é um módulo que **não responde**: ele repete cada comando até
`kTentativasPorComando` e segue sem a configuração, no caminho de "sem
resposta". Dá para exercitar esse caminho de propósito — ele é o que acontece
com o módulo mudo —, mas **não dá para testar a configuração em si** por
aqui.

## Capturar as sentenças

⚠️ **`simula_gps.py | tee captura.nmea` NÃO captura a NMEA.** O que sai pela
saída padrão é o console — a volta, o percentual, a velocidade. As sentenças
saem pela **pty**, que é justamente o ponto dela. O arquivo fica com o status
e nenhuma `$GNRMC`.

Para capturar o que o aparelho leria, leia o outro lado:

```bash
python3 simula_gps.py --sem-pausa &   # cria o atalho ./serial
cat serial > captura.nmea             # ou: cat "$(readlink serial)"
```

## Regerar a rota

```bash
python3 gera_rota.py                      # consulta a Overpass
python3 gera_rota.py --de-arquivo x.json  # de uma resposta ja baixada
```

O gerador tenta três espelhos: o principal devolve `504` sob carga com alguma
frequência, e isso não é motivo para desistir da rota.
