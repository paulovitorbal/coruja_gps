# Servidor de referência da base de radares

Expõe os dois recursos que o firmware consome (RF05.0):

| Rota | Devolve |
| :--- | :--- |
| `GET /radares.versao` | uma linha de texto, comparada como **texto** pelo firmware |
| `GET /radares.bin` | o arquivo binário |

E, **opcionalmente**, recebe de volta o que o aparelho registrou:

| Rota | Faz |
| :--- | :--- |
| `PUT /envio/<nome>` | grava o arquivo e devolve `201` com o **CRC-32** do que chegou |
| `GET /envio/<nome>` | devolve `200` com o CRC-32 do que está **guardado** |

Quem pode falar com o servidor sai de uma lista `nome=token` — ver *Quem pode
falar com o servidor*, logo abaixo. **Com a lista preenchida, todas as rotas
passam a exigir token, inclusive a raiz.**

**É agnóstico à origem.** Ele serve o que estiver em `dados/`, sem saber de
onde veio — e é por isso que pode viver no repositório público. Quem clonar
aponta o próprio pipeline para cá.

## Subir

```sh
docker compose up -d
curl -i http://localhost:8081/radares.versao
```

Publique a base copiando o `radares.bin` para `./dados/`. O arquivo é
derivado e não é versionado.

Sem Docker, funciona igual:

```sh
python3 servidor.py --dados ./dados --porta 8080
```

## A versão sai do próprio arquivo

Nada de incrementar número à mão. O `radares.bin` já carrega um **CRC-32 do
bloco de dados** no cabeçalho, e ele responde exatamente à pergunta "os dados
mudaram?":

```
crc32:d290d536 pontos:18322 formato:2 data:2026-09-30
```

Derivar evita a falha clássica de publicar dado novo com versão velha — o
firmware compararia, veria igual, e nunca baixaria. O firmware **não
interpreta** essa linha; os campos extras existem para quem for depurar.

O `data:` só aparece a partir do **formato 2**, que é quando a base passou a
carregar a própria data. Num arquivo do formato 1 o campo some em vez de vir
zerado: uma data plausível e errada atrapalha mais quem depura do que data
nenhuma, porque não se denuncia.

Se o arquivo não tiver cabeçalho `RDR1` válido, o servidor cai para um
`sha256:` do conteúdo e **registra aviso**. Ainda dá detecção de mudança, e o
log diz que algo está errado com o arquivo.

## 🔴 HTTPS não vem incluso

O RF05.2 exige HTTPS, e este servidor fala **HTTP puro**. Em HTTP, quem
estiver na mesma rede pode substituir a base de radares que o aparelho vai
baixar — e o aparelho confiaria nela.

Para uso fora da rede local, ponha um **proxy reverso** na frente. Com Caddy
são duas linhas:

```
radares.seudominio.com {
    reverse_proxy localhost:8081
}
```

### Teste em rede local

Para o Pico alcançar o serviço pelo Wi-Fi o compose publica em `0.0.0.0:8081`,
e o `coruja.cfg` precisa de URLs em `http://`. O gerador recusa HTTP por
padrão; a saída existe e é deliberadamente incômoda:

```bash
python3 ../scripts/gera_config.py --permitir-http --destino /Volumes/CARTAO
```

Ele exige que se digite `ENTENDI`, avisa a cada URL sem TLS e grava um bloco de
aviso dentro do próprio `coruja.cfg` — de modo que um arquivo de teste não se
disfarce de definitivo meses depois.

Ao sair da rede local: proxy reverso com TLS na frente e a porta de volta para
`127.0.0.1:8081`.

## Quem pode falar com o servidor

A lista fica no `aparelhos.cfg`, ao lado do `docker-compose.yml`:

```
# nome=token
carro-paulo=HBu2kQ...
bancada=9xT1pR...
```

```sh
cp aparelhos.cfg.exemplo aparelhos.cfg
python3 -c 'import secrets; print(secrets.token_urlsafe(32))'   # um por aparelho
docker compose up -d
```

O `aparelhos.cfg` **não é versionado**; o `.exemplo` é.

No `coruja.cfg` do cartão, do outro lado:

```
token_aparelho=HBu2kQ...
url_envio=http://servidor.da.rede:8081/envio/
```

### O que a lista liga, e o que ela fecha

| `aparelhos.cfg` | rotas da base | recepção |
| :--- | :--- | :--- |
| ausente ou sem entradas | **abertas** | **fechada** (401) |
| com ao menos um aparelho | exigem token | ligada |

Os dois efeitos são opostos **de propósito**. Quem só distribui a base não
precisa configurar nada e `curl` continua sendo uma linha. Mas receber sem
lista significaria gravar sem saber de quem veio o arquivo — que derrota o
propósito da funcionalidade, além de ser um depósito aberto.

> ⚠️ **Ligar a lista derruba quem não tem token.** Um aparelho com o
> `token_aparelho` errado, ou sem ele, para de conseguir **até baixar a
> base** — responde `401`. É o preço de validar todas as rotas, e é
> deliberado.

### `401`, e não `403`

`403` significa "sei quem é você e não pode". Aqui não se sabe quem é. A
distinção é o que diz ao cliente se vale tentar outra credencial ou desistir.

A raiz e qualquer rota desconhecida também respondem `401` sem token: dizer
que uma rota não existe já é informação sobre o servidor.

### Dois aparelhos não podem dividir o mesmo token

O servidor **recusa subir** nesse caso. Com segredos iguais não dá para saber
quem enviou — que é a pergunta inteira que a lista existe para responder. Nome
repetido e nome que viraria caminho (`..`, `a/b`) também derrubam a subida, em
vez de serem corrigidos por adivinhação.

### Cada aparelho tem a própria pasta

```
recebidos/
├── carro-paulo/
│   ├── infracoes.log
│   └── 20261006_143000.log
└── bancada/
    └── coruja.log
```

Não é organização: é correção. Dois carros gravam `infracoes.log` com o mesmo
nome. Numa pasta só, o envio do segundo sobrescreveria o do primeiro — e aí o
**primeiro apagaria o arquivo do próprio cartão**, porque perguntaria o CRC e
receberia o de um arquivo que nunca mandou. Pela mesma razão, a consulta de
CRC olha só a pasta de quem perguntou.

### O token também vai na URL

O servidor aceita o segredo no cabeçalho `X-Coruja-Token` **ou** como `?t=`:

```sh
curl -i -H 'X-Coruja-Token: HBu2kQ...' http://localhost:8081/radares.versao
curl -i 'http://localhost:8081/radares.versao?t=HBu2kQ...'
```

O cabeçalho tem precedência quando os dois vêm.

⚠️ **Isso existe por limitação do firmware, não por gosto.** O cliente de
download do aparelho usa o `http_client` do lwIP, que monta a requisição
internamente: a `httpc_connection_t` não tem campo para cabeçalho próprio e a
biblioteca não oferece gancho. A alternativa seria reescrever o transporte do
OTA sobre TCP cru — o código mais arriscado e menos testável do projeto — para
mudar o lugar de uma string. O envio, que já é TCP cru, usa o cabeçalho.

⚠️ **Token em URL entra no log de acesso** — do servidor e de qualquer proxy
no caminho —, ao contrário de token em cabeçalho. Aqui pesa menos do que
pareceria, porque tudo trafega em claro de qualquer forma; mas quem puser um
proxy reverso na frente deve saber disso. O firmware mascara a consulta no
próprio log.

## Receber dados do aparelho

O item **enviar dados** do menu sobe `coruja.log`, `infracoes.log` e os
registros de viagem. O aparelho **apaga do cartão só o que o servidor
confirmar ter guardado**: manda, pergunta o CRC-32, compara, e só então
apaga.

### 🔴 O token NÃO protege o conteúdo

Ele viaja **em claro**, porque o firmware não fala TLS. Qualquer um na mesma
rede lê o segredo e os arquivos — que dizem **onde o carro esteve e quando**.

O token existe para que a URL não seja um depósito aberto a quem a descobrir,
e para separar um aparelho do outro. São problemas reais e diferentes, e
resolver esses não resolve o da escuta. Fora da rede local: proxy reverso com
TLS na frente, como na base.

### Por que `recebidos/` é um volume separado

O volume da base é montado `:ro` e o container é `read_only` — comprometido,
o serviço não consegue trocar o `radares.bin` que os aparelhos vão baixar.

Receber arquivo exige escrita. Gravar dentro do volume da base jogaria fora
essa garantia inteira, em troca de uma linha de configuração a menos. Daí o
`CORUJA_RECEBIDOS` apontando para outro lugar.

### O que o servidor aceita, e nada mais

| Nome | O que é |
| :--- | :--- |
| `coruja.log` | o diário de diagnóstico |
| `infracoes.log` | o registro de passagens |
| `AAAAMMDD_HHMMSS.log` | um trecho de viagem |

⚠️ **A lista branca é a defesa contra travessia de caminho.** Rejeitar `../` é
jogo de gato e rato; aceitar só o que casa com o padrão não é. A mesma lista
existe no firmware, em `nome_enviavel()` — são dois lados do mesmo contrato, e
um nome aceito lá e recusado aqui vira um arquivo que o aparelho tenta mandar
para sempre.

### O arquivo só existe quando chega inteiro

A gravação é em `<nome>.parcial` e só então renomeada. Um envio que cai no
meio não pode deixar meio arquivo com o nome definitivo — e, pior, um reenvio
que falha não pode destruir a cópia boa que já estava lá, porque o aparelho
pode já ter apagado a dele.

## Decisões que valem conhecer

**Duas rotas fixas, não um servidor de arquivos.** Herdar de
`SimpleHTTPRequestHandler` seria menos código e exporia o diretório inteiro,
com listagem. Com duas rotas fixas não existe travessia de caminho possível —
e há teste que tenta `/../servidor.py` e variantes.

**`503` quando a base não foi publicada, não `404`.** A rota existe; o dado é
que não está lá. A distinção poupa tempo de quem está depurando.

**`HEAD` funciona.** Permite conferir disponibilidade e tamanho sem baixar
214 KB.

**`Content-Length` sempre presente.** O RF05.2 aborta o download sem ele.

**`Cache-Control: no-store`.** Um proxy guardando a base velha faria o
aparelho nunca atualizar.

**Só biblioteca padrão.** Como todo o Python deste projeto. A imagem Docker
não precisa de `pip install` e não há dependência para envelhecer.

**O volume é `:ro` e o container roda sem privilégio**, com `read_only`,
`cap_drop: ALL` e `no-new-privileges`. O serviço nunca escreve: se for
comprometido, não há como alterar a base que os aparelhos vão baixar.

## Testes

```sh
python3 -m unittest discover -s servidor/testes
```

79 casos. Sobem o servidor de verdade numa porta efêmera e falam HTTP com ele
— testar o manipulador isolado deixaria de fora justamente o que o firmware
consome: os códigos de status, os cabeçalhos e o `HEAD`.

Os da recepção e os da autenticação passaram por campanhas de **mutação**:
defeitos plausíveis introduzidos de propósito — apagar a conferência de token, tirar as
âncoras do padrão de nomes, gravar direto no nome final, deixar de encadear o
CRC entre os pedaços. Os onze são detectados.

Um deles sobreviveu à primeira rodada e vale registrar: gravar direto no nome
final passava, porque a limpeza do envio truncado apagava o arquivo nos dois
casos. O defeito que ninguém via era outro — **um reenvio que falha destrói a
cópia boa anterior**, porque abrir em `wb` trunca antes de saber se o novo vai
chegar. O teste que faltava não era sobre o envio truncado; era sobre o que
havia antes dele.
