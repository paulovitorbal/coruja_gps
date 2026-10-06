#!/usr/bin/env python3
"""Servidor de referência da base de radares do Coruja GPS.

Expõe os dois recursos que o firmware consome (RF05.0):

    GET /radares.versao   -> uma linha de texto, comparada como TEXTO
    GET /radares.bin      -> o arquivo binário

É **agnóstico à origem**: serve o que estiver no diretório de dados, sem saber
de onde veio. É por isso que ele pode viver no repositório público — quem
clonar aponta o próprio pipeline para cá.

Usa só a biblioteca padrão, como todo o Python deste projeto. Sem framework,
a imagem Docker não precisa de `pip install` e não há dependência para
envelhecer.

    python3 servidor.py --dados ./dados --porta 8080
"""

from __future__ import annotations

import argparse
import hashlib
import http.server
import logging
import os
import socketserver
import struct
import sys
import hmac
import re
import threading
import zlib
from pathlib import Path

NOME_BASE = "radares.bin"
ROTA_VERSAO = "/radares.versao"
ROTA_BASE = "/radares.bin"

# ---------------------------------------------------------------- recepção --
#
# O aparelho envia os logs que gravou — infrações e viagens. O protocolo é
# propositalmente mínimo, porque do outro lado está um RP2350 falando lwIP sem
# TLS e sem parser de multipart:
#
#   PUT  /envio/<nome>   corpo = o arquivo cru        -> 201
#   GET  /envio/<nome>   -> o CRC32 do que esta aqui, em hexa de 8 digitos
#
# O GET existe por causa da regra de apagar: o aparelho so remove o arquivo
# local depois de perguntar o CRC e confirmar que bate. Confirmar antes de
# destruir dispensa manifesto no cartao e nunca perde dado por falha de rede.
ROTA_ENVIO = "/envio/"

#: Onde cai o que o aparelho manda, quando ninguem diz outra coisa.
#:
#: ⚠️ **E um diretorio SEPARADO do da base, e nao um subdiretorio dela por
#: acaso.** O servico serve a base com o volume montado `:ro` e o container
#: `read_only`, de proposito: comprometido, ele nao tem como alterar o
#: `radares.bin` que os aparelhos vao baixar. Receber arquivo exige escrita,
#: e abrir o volume da base para isso jogaria fora essa garantia inteira.
#: Separados, a recepcao monta um volume proprio, gravavel, e a base
#: continua intocavel.
SUBDIR_ENVIO = "recebidos"

#: Teto por arquivo. Um log de viagem longa tem dezenas de KB; 8 MB e folga de
#: sobra e impede que um corpo sem fim encha o disco.
TAMANHO_MAXIMO = 8 * 1024 * 1024

#: Nome aceito: o que o firmware gera, e nada mais.
#:
#:   coruja.log               o diario de diagnostico
#:   infracoes.log            o registro de passagens
#:   AAAAMMDD_HHMMSS.log      um trecho de viagem
#:
#: ⚠️ A lista branca e a defesa contra travessia de caminho. Rejeitar "../" e
#: jogo de gato e rato; aceitar so o que casa com o padrao nao e.
PADRAO_NOME = re.compile(r"^(coruja\.log|infracoes\.log|\d{8}_\d{6}\.log)$")

#: Cabecalho do segredo combinado. Sem ele, ou errado, a resposta e 401.
#:
#: ⚠️ Isto NAO e autenticacao forte: o segredo viaja em claro, porque o
#: aparelho nao fala TLS. Ele existe para que a URL publica nao seja um
#: deposito aberto para qualquer um que a descubra — que e um problema real e
#: diferente.
CABECALHO_TOKEN = "X-Coruja-Token"

#: Onde mora a lista de aparelhos, quando ninguem diz outra coisa.
ARQUIVO_APARELHOS = "aparelhos.cfg"

#: Nome de aparelho aceito. Ele vira **nome de diretorio** em `recebidos/`,
#: entao a lista branca aqui e o que impede um nome de configuracao de virar
#: travessia de caminho — mesma razao do `PADRAO_NOME`.
PADRAO_APARELHO = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$")

#: Abaixo disto o token e curto demais para servir de segredo. Nao recusa:
#: avisa. Quem escolhe o segredo e o dono do servidor, e travar a subida por
#: causa disso deixaria alguem sem servidor as 23h por um palpite nosso.
TOKEN_CURTO = 16


class ErroDeAparelhos(Exception):
    """A lista de aparelhos nao pode ser usada como esta."""


def le_aparelhos(caminho: Path) -> dict[str, str]:
    """Le o `aparelhos.cfg` e devolve {token: nome}.

    Formato `nome=token`, uma por linha; `#` comenta, linha em branco passa.
    A divisao e no PRIMEIRO `=`, porque um token pode conter o separador.

    Indexado por token e nao por nome porque e assim que a consulta acontece:
    chega um segredo e a pergunta e "de quem e este". O caminho inverso nunca
    e percorrido.

    Levanta `ErroDeAparelhos` em nome invalido, nome repetido ou token
    repetido. **Token repetido e o que mais importa recusar**: dois aparelhos
    com o mesmo segredo tornam a atribuicao ambigua, e o proposito da lista e
    justamente saber de quem veio o arquivo.
    """
    if not caminho.is_file():
        return {}

    por_token: dict[str, str] = {}
    nomes: set[str] = set()
    for n_linha, bruta in enumerate(
            caminho.read_text(encoding="utf-8").splitlines(), start=1):
        linha = bruta.strip()
        if not linha or linha.startswith("#"):
            continue
        if "=" not in linha:
            raise ErroDeAparelhos(
                f"{caminho}:{n_linha}: esperava 'nome=token'")
        nome, token = (parte.strip() for parte in linha.split("=", 1))
        if not PADRAO_APARELHO.match(nome):
            raise ErroDeAparelhos(
                f"{caminho}:{n_linha}: nome '{nome}' invalido "
                "(letras, digitos, hifen e sublinhado; ate 32)")
        if not token:
            raise ErroDeAparelhos(f"{caminho}:{n_linha}: '{nome}' sem token")
        if nome in nomes:
            raise ErroDeAparelhos(f"{caminho}:{n_linha}: '{nome}' repetido")
        if token in por_token:
            raise ErroDeAparelhos(
                f"{caminho}:{n_linha}: o token de '{nome}' e igual ao de "
                f"'{por_token[token]}'; nao daria para saber quem enviou")
        nomes.add(nome)
        por_token[token] = nome
    return por_token

# Cabeçalho do radares.bin, de formato_dados.md §2:
#   magic[4] | versao u16 | exp_escala u8 | tam_registro u8 | n u32 | crc32 u32
# Os 16 primeiros bytes do cabeçalho, idênticos nas duas versões do formato.
# A data da base vem DEPOIS, em quatro bytes, e só existe da versão 2 em
# diante — ver `formato_dados.md` no repositório do aparelho.
CABECALHO_BASE = struct.Struct("<4sHBBII")
DATA_BASE = struct.Struct("<HBB")
VERSAO_SEM_DATA = 1
MAGIC = b"RDR1"

log = logging.getLogger("coruja.servidor")


def sufixo_de_data(bruto: bytes, versao: int) -> str:
    """O trecho ` data:AAAA-MM-DD`, ou vazio quando não há data a anunciar.

    Vazio em dois casos, e o segundo é o que importa: a versão 1 não tem o
    campo, e uma versão 2 TRUNCADA antes dos quatro bytes também não. Montar
    uma data a partir do que chegou daria algo como `2026-09-00` — uma data
    plausível e errada é pior para quem depura do que data nenhuma, porque
    não se denuncia.
    """
    if versao == VERSAO_SEM_DATA:
        return ""
    fim = CABECALHO_BASE.size + DATA_BASE.size
    if len(bruto) < fim:
        log.warning("cabeçalho formato %d sem os bytes da data", versao)
        return ""
    ano, mes, dia = DATA_BASE.unpack_from(bruto, CABECALHO_BASE.size)
    return f" data:{ano:04d}-{mes:02d}-{dia:02d}"


def versao_de(caminho: Path) -> str | None:
    """Deriva a linha de versão do PRÓPRIO arquivo.

    Nada de bumping manual: o `radares.bin` já carrega um CRC-32 do bloco de
    dados no cabeçalho, e ele responde exatamente à pergunta "os dados
    mudaram?". Derivar evita a falha clássica de publicar dado novo com versão
    velha, que o firmware então ignora.

    O firmware compara como texto e não interpreta nada disto — o formato é
    livre, e os campos extras existem para quem for depurar.
    """
    try:
        bruto = caminho.read_bytes()
    except OSError as e:
        log.error("não consegui ler %s: %s", caminho, e)
        return None

    if len(bruto) >= CABECALHO_BASE.size:
        magic, versao, _exp, _tam, n, crc = CABECALHO_BASE.unpack_from(bruto)
        if magic == MAGIC:
            linha = f"crc32:{crc:08x} pontos:{n} formato:{versao}"
            return linha + sufixo_de_data(bruto, versao)

    # Não é um radares.bin válido. Ainda assim serve uma versão estável, para
    # que o firmware ao menos detecte mudança — e o log diz que algo está
    # errado com o arquivo.
    log.warning("%s não tem cabeçalho RDR1; usando sha256 do conteúdo",
                caminho)
    return f"sha256:{hashlib.sha256(bruto).hexdigest()[:16]}"


class Manipulador(http.server.BaseHTTPRequestHandler):
    """Só duas rotas. Qualquer outra coisa é 404.

    Deliberadamente **não** herda de `SimpleHTTPRequestHandler`: aquele serve
    o diretório inteiro e faz listagem, o que aqui seria expor arquivos que
    não são da API. Duas rotas fixas não têm travessia de caminho possível.
    """

    server_version = "CorujaGPS/1.0"
    protocol_version = "HTTP/1.1"

    # Injetado pelo `cria_servidor`.
    dados: Path = Path(".")
    #: {token: nome do aparelho}, do `aparelhos.cfg`.
    #:
    #: **Vazio tem dois efeitos, e eles são opostos de propósito:**
    #:
    #: - as rotas da base ficam **abertas**, como sempre foram. Subir o
    #:   servidor e conferir com `curl` continua sendo uma linha, e quem só
    #:   distribui a base não precisa configurar nada.
    #: - a recepção fica **fechada**. Sem lista não há a quem atribuir o
    #:   arquivo, e gravar sem saber de quem veio derrota o propósito da
    #:   funcionalidade — além de ser um depósito aberto.
    #:
    #: Com a lista preenchida, **toda** rota passa a exigir token conhecido,
    #: inclusive a raiz.
    aparelhos: dict[str, str] = {}
    recebidos: Path = Path()

    def log_message(self, formato: str, *args) -> None:
        log.info("%s %s", self.address_string(), formato % args)

    def _responde(self, codigo: int, corpo: bytes, tipo: str,
                  so_cabecalho: bool = False) -> None:
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        # O RF05.2 aborta se `Content-Length` faltar.
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if not so_cabecalho:
            self.wfile.write(corpo)

    def _erro(self, codigo: int, msg: str, so_cabecalho: bool) -> None:
        self._responde(codigo, (msg + "\n").encode(), "text/plain; charset=utf-8",
                       so_cabecalho)

    def _serve(self, so_cabecalho: bool = False) -> None:
        caminho = self.path.split("?", 1)[0]
        arquivo = self.dados / NOME_BASE

        if caminho not in (ROTA_VERSAO, ROTA_BASE):
            self._erro(404, f"rotas: {ROTA_VERSAO} e {ROTA_BASE}", so_cabecalho)
            return

        if not arquivo.is_file():
            # 503 e não 404: a rota existe, o dado é que não foi publicado.
            # A distinção poupa tempo de quem está depurando.
            log.warning("pedido de %s mas %s não existe", caminho, arquivo)
            self._erro(503, "base ainda nao publicada", so_cabecalho)
            return

        if caminho == ROTA_VERSAO:
            versao = versao_de(arquivo)
            if versao is None:
                self._erro(503, "base ilegivel", so_cabecalho)
                return
            self._responde(200, (versao + "\n").encode(),
                           "text/plain; charset=utf-8", so_cabecalho)
            return

        try:
            corpo = arquivo.read_bytes()
        except OSError as e:
            log.error("falha ao ler %s: %s", arquivo, e)
            self._erro(503, "base ilegivel", so_cabecalho)
            return
        self._responde(200, corpo, "application/octet-stream", so_cabecalho)

    # ------------------------------------------------------------ recepção

    def _nome_do_envio(self) -> str | None:
        """Nome pedido na rota, se for um dos que o firmware gera."""
        caminho = self.path.split("?", 1)[0]
        if not caminho.startswith(ROTA_ENVIO):
            return None
        nome = caminho[len(ROTA_ENVIO):]
        return nome if PADRAO_NOME.match(nome) else None

    def _token_enviado(self) -> str:
        """O segredo da requisição. **Só o cabeçalho.**

        Houve uma versão que também o aceitava como `?t=` na URL, porque o
        cliente de download do aparelho usava o `http_client` do lwIP, que
        monta a requisição internamente e não tem campo para cabeçalho
        próprio. Com TLS esse transporte foi reescrito sobre TCP, o gancho
        passou a existir, e o `?t=` saiu: token em URL entra no log de acesso
        do servidor e de qualquer proxy no caminho, ao contrário de token em
        cabeçalho.
        """
        return self.headers.get(CABECALHO_TOKEN, "")

    def _aparelho(self) -> str | None:
        """Qual aparelho mandou esta requisição, ou `None` se não se sabe.

        Sem lista o laço não tem o que percorrer e a resposta já é `None` —
        não há atalho antes dele. Houve um, e uma campanha de mutação mostrou
        que nenhum teste conseguia distingui-lo: era peso, não guarda.
        """
        enviado = self._token_enviado()
        if not enviado:
            # Requisição sem segredo nenhum para de imediato, e isto não é
            # só atalho: uma entrada de token vazio na lista casaria com
            # `compare_digest("", "")` e autenticaria TODO MUNDO como aquele
            # aparelho. O `le_aparelhos` recusa token vazio no arquivo, mas a
            # lista também chega por código -- é assim que os testes montam
            # servidores -- e aí ninguém a validou.
            return None
        # Percorre a lista INTEIRA, sempre, e compara cada token em tempo
        # constante. Sair no primeiro acerto -- ou usar `dict.get`, que é o
        # mesmo -- faria o tempo de resposta depender de quantos tokens há
        # antes do certo, e `==` vazaria o prefixo correto caractere a
        # caractere para quem tivesse muitas tentativas.
        achado = None
        for token, nome in self.aparelhos.items():
            if hmac.compare_digest(enviado, token):
                achado = nome
        return achado

    def _autorizado(self) -> bool:
        return self._aparelho() is not None

    def _barra_desconhecido(self, so_cabecalho: bool = False) -> bool:
        """Recusa quem não está na lista. `True` significa "já respondi".

        **Vale para TODAS as rotas, inclusive a raiz.** Com a lista
        preenchida, o servidor deixa de responder qualquer coisa a quem não
        se identifica — e isso inclui dizer que uma rota não existe, que já
        seria informação.
        """
        if not self.aparelhos:
            return False
        if self._autorizado():
            return False
        # 401 e não 403: 403 significa "sei quem é você e não pode"; aqui não
        # se sabe quem é. É a distinção que o cliente usa para decidir se
        # tenta outra credencial ou desiste.
        self._erro(401, "token ausente ou desconhecido", so_cabecalho)
        return True

    def _recebe(self) -> None:
        aparelho = self._aparelho()
        if aparelho is None:
            self._erro(401, "token ausente ou desconhecido", False)
            return
        nome = self._nome_do_envio()
        if nome is None:
            self._erro(404, "nome de arquivo nao aceito", False)
            return

        try:
            tamanho = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._erro(400, "Content-Length invalido", False)
            return
        if tamanho <= 0:
            self._erro(400, "corpo vazio", False)
            return
        if tamanho > TAMANHO_MAXIMO:
            self._erro(413, f"maior que {TAMANHO_MAXIMO} bytes", False)
            return

        # Uma pasta por aparelho. Sem isto, dois carros mandando
        # `infracoes.log` sobrescreveriam um ao outro -- e o segundo envio
        # apagaria o arquivo do primeiro no cartao dele, porque o CRC
        # bateria com o que o servidor tem.
        destino = self.recebidos / aparelho
        destino.mkdir(parents=True, exist_ok=True)

        # Grava em temporário e renomeia: um envio interrompido no meio não
        # pode deixar meio arquivo com o nome definitivo, porque o aparelho
        # perguntaria o CRC, receberia o de um pedaço, e... não apagaria o
        # local — que é o comportamento certo, mas o lixo ficaria aqui.
        temporario = destino / (nome + ".parcial")
        recebido = 0
        crc = 0
        try:
            with temporario.open("wb") as f:
                while recebido < tamanho:
                    pedaco = self.rfile.read(min(64 * 1024, tamanho - recebido))
                    if not pedaco:
                        break
                    f.write(pedaco)
                    crc = zlib.crc32(pedaco, crc)
                    recebido += len(pedaco)
        except OSError as e:
            temporario.unlink(missing_ok=True)
            log.error("falha gravando %s: %s", nome, e)
            self._erro(500, "falha ao gravar", False)
            return

        if recebido != tamanho:
            temporario.unlink(missing_ok=True)
            self._erro(400, "corpo menor que o Content-Length", False)
            return

        temporario.replace(destino / nome)
        log.info("recebido %s/%s (%d bytes, crc %08x)", aparelho, nome,
                 recebido, crc)
        self._responde(201, f"{crc:08x}\n".encode(),
                       "text/plain; charset=utf-8")

    def _crc_do_recebido(self, nome: str) -> None:
        """O CRC32 do que está guardado, para o aparelho decidir se apaga.

        Olha **só a pasta de quem perguntou**. Responder o CRC do arquivo de
        outro aparelho faria o primeiro apagar um registro que nunca chegou.
        """
        aparelho = self._aparelho()
        if aparelho is None:
            self._erro(401, "token ausente ou desconhecido", False)
            return
        arquivo = self.recebidos / aparelho / nome
        if not arquivo.is_file():
            self._erro(404, "nao recebido", False)
            return
        crc = 0
        try:
            with arquivo.open("rb") as f:
                while True:
                    pedaco = f.read(64 * 1024)
                    if not pedaco:
                        break
                    crc = zlib.crc32(pedaco, crc)
        except OSError:
            self._erro(503, "ilegivel", False)
            return
        self._responde(200, f"{crc:08x}\n".encode(),
                       "text/plain; charset=utf-8")

    def do_PUT(self) -> None:  # noqa: N802
        self._recebe()

    def do_GET(self) -> None:  # noqa: N802
        nome = self._nome_do_envio()
        if nome is not None:
            self._crc_do_recebido(nome)
            return
        if self._barra_desconhecido():
            return
        self._serve()

    def do_HEAD(self) -> None:  # noqa: N802
        # Permite conferir tamanho e disponibilidade sem baixar 214 KB.
        if self._barra_desconhecido(so_cabecalho=True):
            return
        self._serve(so_cabecalho=True)


class Servidor(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def cria_servidor(dados: Path, porta: int, endereco: str = "",
                  aparelhos: dict[str, str] | None = None,
                  recebidos: Path | None = None) -> Servidor:
    raiz = dados.resolve()
    manipulador = type("ManipuladorLigado", (Manipulador,),
                       {"dados": raiz,
                        "aparelhos": dict(aparelhos or {}),
                        # Sem `recebidos` explicito cai ao lado da base, que e
                        # o que serve para rodar `python3 servidor.py` a mao.
                        # O compose passa um volume proprio -- ver o README.
                        "recebidos": (recebidos.resolve() if recebidos
                                      else raiz / SUBDIR_ENVIO)})
    return Servidor((endereco, porta), manipulador)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dados", type=Path,
                   default=Path(os.environ.get("CORUJA_DADOS", "./dados")),
                   help="diretório que contém o radares.bin")
    p.add_argument("--porta", type=int,
                   default=int(os.environ.get("CORUJA_PORTA", "8080")))
    p.add_argument("--endereco", default=os.environ.get("CORUJA_ENDERECO", ""))
    p.add_argument("--aparelhos", type=Path,
                   default=Path(os.environ.get("CORUJA_APARELHOS",
                                               ARQUIVO_APARELHOS)),
                   help=f"lista 'nome=token', uma por linha "
                        f"(padrao: {ARQUIVO_APARELHOS}). Ausente ou vazia: "
                        "base aberta e recepcao desligada")
    p.add_argument("--recebidos", type=Path,
                   default=(Path(os.environ["CORUJA_RECEBIDOS"])
                            if os.environ.get("CORUJA_RECEBIDOS") else None),
                   help="onde gravar o que o aparelho mandar "
                        "(padrao: <dados>/recebidos)")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(
        level=logging.WARNING if args.quiet else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s")

    try:
        aparelhos = le_aparelhos(args.aparelhos)
    except ErroDeAparelhos as e:
        # Recusa subir. Um servidor que ignora a lista com defeito ficaria com
        # as rotas abertas e a recepcao desligada, parecendo funcionar.
        print(f"erro: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"erro: nao consegui ler {args.aparelhos}: {e}", file=sys.stderr)
        return 1

    if aparelhos:
        destino = args.recebidos or (args.dados / SUBDIR_ENVIO)
        log.info("%d aparelho(s) em %s; TODAS as rotas exigem token",
                 len(aparelhos), args.aparelhos)
        log.info("recepcao LIGADA em %s* -> %s/<aparelho>/", ROTA_ENVIO,
                 destino)
        for nome in sorted(aparelhos.values()):
            log.info("  aparelho: %s", nome)
        curtos = sum(1 for tk in aparelhos if len(tk) < TOKEN_CURTO)
        if curtos:
            log.warning("%d token(s) com menos de %d caracteres: curto demais "
                        "para servir de segredo", curtos, TOKEN_CURTO)
    else:
        log.info("sem %s: rotas da base ABERTAS, recepcao desligada",
                 args.aparelhos)

    if not args.dados.is_dir():
        print(f"erro: {args.dados} não é um diretório", file=sys.stderr)
        return 1

    arquivo = args.dados / NOME_BASE
    if arquivo.is_file():
        log.info("servindo %s — %s", arquivo, versao_de(arquivo))
    else:
        log.warning("%s ainda não existe; as rotas respondem 503 até publicar",
                    arquivo)

    servidor = cria_servidor(args.dados, args.porta, args.endereco,
                             aparelhos, args.recebidos)
    log.info("escutando em %s:%d", args.endereco or "0.0.0.0", args.porta)
    log.warning("HTTP puro. O RF05.2 exige HTTPS: ponha um proxy reverso na "
                "frente antes de usar fora da rede local.")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        log.info("encerrando")
    finally:
        servidor.shutdown()
        servidor.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
