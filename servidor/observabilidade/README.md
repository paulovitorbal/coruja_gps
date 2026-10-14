# Observabilidade: ler o diário do aparelho sem `grep`

Três serviços: o **Loki** guarda, o **Alloy** coleta, o **Grafana** mostra.

**O que justifica a pilha é o diário do aparelho, não os logs de container.**
`docker logs` já mostra o segundo. O primeiro chega de outro jeito: o aparelho
acumula o log no cartão, o motorista manda pelo menu quando lembra, e o
servidor **acrescenta** o trecho ao histórico. O resultado é um arquivo que
cresce para sempre, misturando dezenas de ligações do aparelho, cada uma com
um `IDEXEC` próprio, com carimbos de dias diferentes — e metade deles em
milissegundos desde o boot, porque o RP2350 não tem bateria no relógio.

Procurar "o que o aparelho viu naquela terça" num arquivo desses é `grep` com
data na mão. Aqui vira uma linha de consulta.

```logql
{servico="coruja-aparelho", nivel=~"WARN|ERRO"}
```

## Subir

```sh
cd servidor/observabilidade
cp env.exemplo .env && $EDITOR .env      # a senha do Grafana, obrigatória
docker compose up -d
```

O painel fica em `http://<servidor>:3000`, usuário `admin`. A fonte de dados e
o painel **Coruja — diário do aparelho** entram por provisionamento: não há
sequência de cliques a lembrar.

Sobe **separado da API** de propósito. Quem clonar o repositório para
distribuir base de radares não precisa de três serviços a mais, e uma pilha de
observabilidade que derruba o que observa é pior que não ter nenhuma.

## Cinco coisas que custaram uma subida de teste para descobrir

### 1. A data do esquema é um piso, e o que vem antes dela é recusado

O `schema_config` do Loki tem um `from:`. Ele parece metadado inofensivo e
**não é**: linha com carimbo anterior a essa data volta com `500` e
`no schema config found for time`, e o coletor fica reenviando para sempre.

Com o piso na data da implantação, o primeiro teste recusou catorze linhas —
o log do próprio container da API, de três semanas antes. O diário do aparelho
é pior: ele chega dias depois do que descreve. Por isso o piso está em
**2024-01-01**, antes de existir qualquer dado deste projeto.

⚠️ Essa data **não muda** depois que há dado gravado: alterá-la move a
fronteira entre esquemas e torna ilegível o que já está no disco.

### 2. `reject_old_samples` tem de estar desligado

Mesma causa, outro mecanismo. O padrão do Loki recusa amostra com mais de uma
semana — e **descarta em silêncio**: o arquivo está no servidor, a consulta não
acha nada, e não há erro em lugar nenhum. Uma remessa feita depois de um mês
sem conectar o aparelho sumiria inteira.

### 3. A senha do Grafana só vale no primeiro boot

`GRAFANA_SENHA` cria o usuário `admin` na primeira subida. Trocá-la no `.env`
depois **não muda nada** — o usuário já está no volume, e a tentativa de
entrar responde `401`. Para trocar de verdade:

```sh
docker exec -it coruja-grafana grafana-cli admin reset-admin-password '<nova>'
```

Ou apagar o volume `grafana-dados`, o que também apaga preferências e qualquer
painel criado pela tela.

### 4. Linha atrasada é descartada, e às vezes em silêncio

A janela de aceitação de uma linha **atrasada** é metade do `max_chunk_age`, e
o padrão de 2 h dava uma hora. O diário do aparelho chega dias depois do que
descreve: com o padrão, quase nada dele é indexado.

Medido: uma linha de nove dias atrás simplesmente não chegava. Às vezes o
coletor registrava `entry too far behind`; às vezes **não registrava nada**, e
o silêncio é pior — o arquivo está no servidor, a consulta não acha, e nada
aponta para o motivo. Por isso `max_chunk_age: 2160h`, que dá **45 dias** de
janela.

Remessa mais atrasada que isso continua no arquivo original, em `recebidos/`.
Esta pilha é um índice do que já está guardado, não a cópia única.

### 5. O coletor leva todo container da máquina, se deixarem

A máquina que hospeda a API roda uma dúzia de serviços sem relação nenhuma com
este projeto. Na primeira implantação entraram linhas **de 2022** do `mysql` e
do `portainer` — que além de ruído foram recusadas pelo piso do esquema e
reenviadas em laço.

Por isso o `discovery.relabel` tem um `keep` em `/coruja-.*`. Para incluir
outros serviços, troque a expressão — e desça o piso do `schema_config` até
antes do log mais antigo que existir na máquina.

## O soquete do Docker, e como dispensá-lo

Os logs dos containers exigem `/var/run/docker.sock` montado no Alloy. Ele vai
somente-leitura, mas **quem tem o soquete tem, na prática, raiz no
hospedeiro** — somente-leitura reduz, não elimina.

Para não correr esse risco: comente a linha do soquete no `docker-compose.yml`
e os dois blocos marcados no `alloy.alloy`. O diário do aparelho, que é o
motivo da pilha existir, continua funcionando inteiro.

Se o que você quer é **só** olhar log de container, o [Dozzle] custa ~20 MB e
não precisa de nada disto.

[Dozzle]: https://dozzle.dev

## O que vira rótulo, e o que não vira

| Rótulo | Valores | De onde vem |
| :--- | :--- | :--- |
| `servico` | `coruja-aparelho`, `docker` | fixo, no coletor |
| `aparelho` | a impressão do token | o nome da pasta em `recebidos/` |
| `nivel` | `DEBUG`, `INFO`, `WARN`, `ERRO` | a linha do diário |
| `origem` | `boot`, `gps`, `sd`, `base`, `vigia`, `temp`, `http`… | idem |
| `container` | o nome do container | só nos logs de Docker |

⚠️ **`idexec` NÃO é rótulo** — é metadado estruturado. São 8 caracteres
sorteados a cada ligação do aparelho: como rótulo, cada boot abriria fluxos
próprios no índice, que é a maneira clássica de deixar o Loki lento. Ele
aparece nos detalhes de cada linha e filtra assim:

```logql
{servico="coruja-aparelho"} | idexec="EGMHMD4E"
```

**A impressão, e não o token.** O rótulo `aparelho` carrega os 16 dígitos de
SHA-256 que o servidor usa para nomear a pasta. Identifica sem revelar o
segredo que também envia.

## Consultas que valem a pena

```logql
# Tudo de uma ligação, do boot ao fim
{servico="coruja-aparelho"} | idexec="EGMHMD4E"

# A vigília: base, fix, zona e geometria, de 5 em 5 minutos
{servico="coruja-aparelho", origem="vigia"}

# Cadência do GPS caindo COM posição presente -- o aviso que saiu da tela
{servico="coruja-aparelho", origem="gps"} |= "taxa de fixes"

# Quantos boots houve, por dia
sum(count_over_time({servico="coruja-aparelho", origem="boot"} [1d]))

# O servidor recebendo remessa
{servico="docker", container="coruja-api"} |= "recebido"
```

## Memória

Medido parado, logo depois de subir:

| Serviço | Uso | Teto |
| :--- | ---: | ---: |
| Grafana | 327 MiB | 512 MiB |
| Loki | 62 MiB | 512 MiB |
| Alloy | 53 MiB | 256 MiB |

~440 MiB no total, contra os ~3 GB livres da máquina que hospeda a API. **Todo serviço tem teto**:
sem ele, um pico de ingestão empurra para a troca justamente o que se quer
observar. O teto do Grafana é 512 e não 384 porque 327 MiB parado já era 85%
da cota — e estourar o limite, no Docker, aparece como container reiniciando
sem explicação.

O Loki guarda **90 dias**. O que sai da retenção continua no arquivo original,
em `recebidos/`: isto aqui é um índice do que já está guardado, não a cópia
única.

## Testes

`servidor/testes/test_observabilidade.py` confere o que esta pasta decide sem
subir container nenhum: a expressão que lê o diário contra as quatro formas de
linha que o firmware já produziu, o piso do esquema, a recusa de amostra
antiga, os tetos de memória e a ausência de senha no arquivo versionado.

```sh
python3 -m unittest discover -s servidor/testes -p 'test_observabilidade.py'
```
