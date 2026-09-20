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
import threading
from pathlib import Path

NOME_BASE = "radares.bin"
ROTA_VERSAO = "/radares.versao"
ROTA_BASE = "/radares.bin"

# Cabeçalho do radares.bin, de formato_dados.md §2:
#   magic[4] | versao u16 | exp_escala u8 | tam_registro u8 | n u32 | crc32 u32
CABECALHO = struct.Struct("<4sHBBII")
MAGIC = b"RDR1"

log = logging.getLogger("coruja.servidor")


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

    if len(bruto) >= CABECALHO.size:
        magic, versao, _exp, _tam, n, crc = CABECALHO.unpack_from(bruto)
        if magic == MAGIC:
            return f"crc32:{crc:08x} pontos:{n} formato:{versao}"

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

    def do_GET(self) -> None:  # noqa: N802
        self._serve()

    def do_HEAD(self) -> None:  # noqa: N802
        # Permite conferir tamanho e disponibilidade sem baixar 214 KB.
        self._serve(so_cabecalho=True)


class Servidor(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def cria_servidor(dados: Path, porta: int, endereco: str = "") -> Servidor:
    manipulador = type("ManipuladorLigado", (Manipulador,),
                       {"dados": dados.resolve()})
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
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(
        level=logging.WARNING if args.quiet else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s")

    if not args.dados.is_dir():
        print(f"erro: {args.dados} não é um diretório", file=sys.stderr)
        return 1

    arquivo = args.dados / NOME_BASE
    if arquivo.is_file():
        log.info("servindo %s — %s", arquivo, versao_de(arquivo))
    else:
        log.warning("%s ainda não existe; as rotas respondem 503 até publicar",
                    arquivo)

    servidor = cria_servidor(args.dados, args.porta, args.endereco)
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
