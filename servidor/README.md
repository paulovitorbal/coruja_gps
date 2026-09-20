# Servidor de referência da base de radares

Expõe os dois recursos que o firmware consome (RF05.0):

| Rota | Devolve |
| :--- | :--- |
| `GET /radares.versao` | uma linha de texto, comparada como **texto** pelo firmware |
| `GET /radares.bin` | o arquivo binário |

**É agnóstico à origem.** Ele serve o que estiver em `dados/`, sem saber de
onde veio — e é por isso que pode viver no repositório público. Quem clonar
aponta o próprio pipeline para cá.

## Subir

```sh
docker compose up -d
curl -i http://localhost:8080/radares.versao
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
crc32:d290d536 pontos:18294 formato:1
```

Derivar evita a falha clássica de publicar dado novo com versão velha — o
firmware compararia, veria igual, e nunca baixaria. O firmware **não
interpreta** essa linha; os campos extras existem para quem for depurar.

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
    reverse_proxy localhost:8080
}
```

### Teste em rede local

Para o Pico alcançar o serviço pelo Wi-Fi o compose publica em `0.0.0.0:8080`,
e o `coruja.cfg` precisa de URLs em `http://`. O gerador recusa HTTP por
padrão; a saída existe e é deliberadamente incômoda:

```bash
python3 ../scripts/gera_config.py --permitir-http --destino /Volumes/CARTAO
```

Ele exige que se digite `ENTENDI`, avisa a cada URL sem TLS e grava um bloco de
aviso dentro do próprio `coruja.cfg` — de modo que um arquivo de teste não se
disfarce de definitivo meses depois.

Ao sair da rede local: proxy reverso com TLS na frente e a porta de volta para
`127.0.0.1:8080`.

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

17 casos. Sobem o servidor de verdade numa porta efêmera e falam HTTP com ele
— testar o manipulador isolado deixaria de fora justamente o que o firmware
consome: os códigos de status, os cabeçalhos e o `HEAD`.
