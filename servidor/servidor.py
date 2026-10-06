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
import secrets
import threading
import time
import urllib.parse
import zlib
from pathlib import Path
from typing import NamedTuple

# `pagina` e `viagem` sao do proprio servidor. O `viagem` so importa o folium
# DENTRO da funcao que desenha, entao a ausencia da dependencia nao impede o
# servidor de subir e distribuir a base -- que e a funcao principal.
import pagina
import viagem

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

#: So os registros de viagem, para a pagina. O `coruja.log` e o
#: `infracoes.log` nao sao trajetos e nao tem o que desenhar.
PADRAO_VIAGEM = re.compile(r"^\d{8}_\d{6}\.log$")

#: Cabecalho do segredo combinado. Sem ele, ou errado, a resposta e 401.
#:
#: ⚠️ Isto NAO e autenticacao forte: o segredo viaja em claro, porque o
#: aparelho nao fala TLS. Ele existe para que a URL publica nao seja um
#: deposito aberto para qualquer um que a descubra — que e um problema real e
#: diferente.
CABECALHO_TOKEN = "X-Coruja-Token"

# ------------------------------------------------------- pagina de viagens --
#
# Uma pessoa abre no navegador, informa o token do aparelho e ve as viagens que
# ele mandou -- com mapa e reproducao.
#
#   GET  /viagens          formulario, ou a listagem se houver sessao
#   POST /viagens          recebe o token e abre a sessao
#   GET  /viagens/<nome>   o mapa de uma viagem
#   POST /viagens/sair     encerra a sessao
ROTA_VIAGENS = "/viagens"

#: Nome do cookie de sessao.
#:
#: ⚠️ **O cookie guarda um identificador aleatorio, nao o token.** Poe-lo no
#: navegador significaria que um computador compartilhado passa a poder SUBIR
#: viagem falsa -- o mesmo segredo serve para ler e para enviar. O
#: identificador so da acesso de leitura, e morre com o processo.
COOKIE_SESSAO = "coruja_sessao"

#: Quanto tempo uma sessao dura sem uso.
VALIDADE_SESSAO_S = 8 * 3600

#: Espera imposta a cada tentativa de token errado, por endereco.
#:
#: Nao e defesa seria -- quem tem banda abre varias conexoes. E para que um
#: script ingenuo apontado para a pagina nao consiga milhares de tentativas por
#: minuto num token que, afinal, viaja em claro ate a borda.
ESPERA_APOS_ERRO_S = 1.0

#: Onde mora a lista de aparelhos, quando ninguem diz outra coisa.
ARQUIVO_APARELHOS = "aparelhos.cfg"

#: Quantos digitos hexadecimais da impressao do token nomeiam a pasta.
#:
#: 16 digitos sao 64 bits. Para um punhado de aparelhos a chance de dois
#: tokens caírem na mesma pasta e desprezivel -- e, mesmo assim, o leitor
#: confere e recusa, porque "desprezivel" nao e "impossivel" e a consequencia
#: seria dois carros gravando um por cima do outro.
DIGITOS_IMPRESSAO = 16


def impressao_do_token(token: str) -> str:
    """O nome de pasta de um aparelho, derivado do segredo dele.

    **Derivado, e nao configurado**: nao ha nome para manter em lugar nenhum,
    e trocar o token troca a pasta -- que e o comportamento certo, porque um
    token novo e outro aparelho do ponto de vista de quem recebe.

    ⚠️ **O token NAO vira nome de pasta.** Ele e a credencial que tambem
    ENVIA; escrito no disco apareceria em `ls`, em qualquer backup e no log
    deste servidor, e quem o lesse poderia subir viagem falsa. A impressao
    identifica sem revelar.
    """
    return hashlib.sha256(token.encode("utf-8")).hexdigest()[:DIGITOS_IMPRESSAO]

#: Abaixo disto o token e curto demais para servir de segredo. Nao recusa:
#: avisa. Quem escolhe o segredo e o dono do servidor, e travar a subida por
#: causa disso deixaria alguem sem servidor as 23h por um palpite nosso.
TOKEN_CURTO = 16


class ErroDeAparelhos(Exception):
    """A lista de aparelhos nao pode ser usada como esta."""


#: Secoes que o `aparelhos.cfg` reconhece depois da lista de aparelhos.
#:
#: Fechada de proposito: `[mapas]` em vez de `[mapa]` seria um erro de
#: digitacao que deixaria a chave do Thunderforest cair num balde ignorado, e
#: o sintoma seria um mapa em branco sem explicacao.
SECOES_CONHECIDAS = frozenset({"mapa"})


class ListaDeAparelhos(NamedTuple):
    """O que o `aparelhos.cfg` descreve."""

    #: {token: impressao do token} — a impressao e o nome da pasta
    por_token: dict[str, str]
    #: a secao `[mapa]`, com a chave do provedor de tiles
    mapa: dict[str, str]


def le_aparelhos(caminho: Path) -> ListaDeAparelhos:
    """Le o `aparelhos.cfg`.

    **Um token por linha**, e so. `#` comenta, linha em branco passa. Nao ha
    nome a configurar: a pasta de cada aparelho e a IMPRESSAO do token dele --
    ver `impressao_do_token`.

    Depois dos tokens pode vir `[mapa]`, com ajustes da pagina de viagens:

        HBu2kQ3pR8tL5nW...
        9xT1pRvM2kS7bY4...

        [mapa]
        thunderforest=abc123...

    Indexado por TOKEN porque e assim que a consulta acontece: chega um
    segredo e a pergunta e "de quem e este". O caminho inverso nunca e
    percorrido.

    Levanta `ErroDeAparelhos` em token repetido, impressao repetida ou secao
    desconhecida. **Token repetido e o que mais importa recusar**: dois
    aparelhos com o mesmo segredo tornam a atribuicao ambigua, e o proposito
    da lista e justamente saber de quem veio o arquivo.
    """
    if not caminho.is_file():
        return ListaDeAparelhos({}, {})

    por_token: dict[str, str] = {}
    mapa: dict[str, str] = {}
    secao = ""   # vazio = a lista de tokens

    for n_linha, bruta in enumerate(
            caminho.read_text(encoding="utf-8").splitlines(), start=1):
        linha = bruta.strip()
        if not linha or linha.startswith("#"):
            continue

        if linha.startswith("[") and linha.endswith("]"):
            secao = linha[1:-1].strip().lower()
            if secao not in SECOES_CONHECIDAS:
                raise ErroDeAparelhos(
                    f"{caminho}:{n_linha}: secao '[{secao}]' desconhecida "
                    f"(conhecidas: {', '.join(sorted(SECOES_CONHECIDAS))})")
            continue

        if secao == "mapa":
            if "=" not in linha:
                raise ErroDeAparelhos(
                    f"{caminho}:{n_linha}: em [mapa], esperava 'chave=valor'")
            chave, valor = (parte.strip() for parte in linha.split("=", 1))
            mapa[chave.lower()] = valor
            continue

        token = linha
        # Um `=` fora da secao [mapa] quase certamente e o formato ANTIGO,
        # `nome=token`, que existiu entre 2026-10-06 e a mesma data. Recusar
        # com a explicacao custa menos que aceitar e gravar na pasta errada.
        if "=" in token:
            raise ErroDeAparelhos(
                f"{caminho}:{n_linha}: um token por linha, sem 'nome='. "
                "A pasta de cada aparelho sai da impressao do proprio token.")
        if token in por_token:
            raise ErroDeAparelhos(
                f"{caminho}:{n_linha}: token repetido; nao daria para saber "
                "qual aparelho enviou")
        impressao = impressao_do_token(token)
        if impressao in por_token.values():
            # 64 bits nao colidem na pratica, mas "na pratica" nao e "nunca",
            # e a consequencia seria dois carros gravando um por cima do
            # outro -- em silencio.
            raise ErroDeAparelhos(
                f"{caminho}:{n_linha}: a impressao '{impressao}' colide com a "
                "de outro token. Gere outro segredo.")
        por_token[token] = impressao

    return ListaDeAparelhos(por_token, mapa)

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
    #: Ajustes da pagina de viagens, da secao `[mapa]` do `aparelhos.cfg`.
    mapa: dict[str, str] = {}
    #: {identificador: (nome do aparelho, instante de expiracao)}.
    #:
    #: Em memoria e sem limpeza periodica: reiniciar o servidor derruba as
    #: sessoes, que e comportamento aceitavel para uma pagina de consulta, e
    #: a varredura acontece a cada acesso -- com um punhado de aparelhos nao
    #: vale uma tarefa de fundo.
    sessoes: dict = {}
    #: {endereco: instante ate o qual novas tentativas esperam}
    castigo: dict = {}

    def log_message(self, formato: str, *args) -> None:
        log.info("%s %s", self.address_string(), formato % args)

    def _responde(self, codigo: int, corpo: bytes, tipo: str,
                  so_cabecalho: bool = False) -> None:
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        # O RF05.2 aborta se `Content-Length` faltar.
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("Cache-Control", "no-store")
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        if not so_cabecalho:
            self.wfile.write(corpo)

    def _descarta_corpo(self) -> None:
        """Fecha a conexao quando o corpo do pedido nao foi lido.

        ⚠️ **Isto nao e zelo; e correcao.** Recusar um PUT sem ler o corpo
        deixa os bytes dele na conexao. Com `keep-alive` -- e o Cloudflare usa
        -- o pedido SEGUINTE chega grudado nesse resto, e o servidor ve um
        metodo inventado:

            Unsupported method ('xGET')

        Apareceu assim, em producao, no deploy de 2026-10-06: um `PUT` de um
        byte recusado com 401, e o `GET /radares.versao` logo depois voltando
        501. Em teste nao aparecia, porque cada caso abria conexao nova.

        Drenar o corpo seria a alternativa, e e pior: o teto de
        `TAMANHO_MAXIMO` existe justamente para NAO ler o que nao se quer, e
        um corpo enorme recusado passaria a ser lido inteiro assim mesmo.
        """
        try:
            resta = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            resta = 1          # malformado: trate como se houvesse corpo
        if resta > 0:
            self.close_connection = True

    def _erro(self, codigo: int, msg: str, so_cabecalho: bool) -> None:
        self._descarta_corpo()
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

    # ------------------------------------------------------ pagina de viagens

    def _sessao(self) -> str | None:
        """O aparelho desta sessao, ou `None`. Renova o prazo a cada acesso."""
        bruto = self.headers.get("Cookie", "")
        sid = ""
        for parte in bruto.split(";"):
            chave, _, valor = parte.strip().partition("=")
            if chave == COOKIE_SESSAO:
                sid = valor
        if not sid:
            return None

        agora = time.monotonic()
        # Varre e descarta as vencidas na passagem. Sem isto o dicionario
        # cresceria para sempre num servidor que fica meses no ar.
        for chave in [k for k, (_n, exp) in self.sessoes.items() if exp < agora]:
            del self.sessoes[chave]

        achado = self.sessoes.get(sid)
        if achado is None:
            return None
        nome, _exp = achado
        self.sessoes[sid] = (nome, agora + VALIDADE_SESSAO_S)
        return nome

    def _pasta_do_aparelho(self, nome: str) -> Path:
        return self.recebidos / nome

    def _abre_sessao(self, token: str) -> str | None:
        """Valida o token e devolve o identificador de sessao, ou `None`."""
        aparelho = None
        # Percorre a lista inteira e compara em tempo constante, como o
        # `_aparelho()` faz: sair no primeiro acerto vazaria o prefixo certo
        # pelo tempo de resposta.
        if token:
            for esperado, nome in self.aparelhos.items():
                if hmac.compare_digest(token, esperado):
                    aparelho = nome
        if aparelho is None:
            return None
        sid = secrets.token_urlsafe(32)
        self.sessoes[sid] = (aparelho, time.monotonic() + VALIDADE_SESSAO_S)
        return sid

    def _html(self, codigo: int, corpo: str,
              cookie: tuple[str, str] | None = None) -> None:
        bytes_ = corpo.encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(bytes_)))
        self.send_header("Cache-Control", "no-store")
        # A pagina monta HTML com dados que vieram do aparelho. Os cabecalhos
        # abaixo sao a segunda linha de defesa; a primeira e o `html.escape`.
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        if cookie is not None:
            nome, valor = cookie
            # `Secure` so quando a borda diz que o cliente veio por HTTPS: o
            # Cloudflare termina o TLS e fala HTTP com este servidor, entao
            # olhar o proprio soquete diria "nao e seguro" sempre -- e o
            # cookie nunca seria enviado de volta.
            seguro = self.headers.get("X-Forwarded-Proto", "") == "https"
            pedacos = [f"{nome}={valor}", "Path=/viagens", "HttpOnly",
                       "SameSite=Strict", f"Max-Age={VALIDADE_SESSAO_S}"]
            if seguro:
                pedacos.append("Secure")
            if not valor:
                pedacos.append("Max-Age=0")
            self.send_header("Set-Cookie", "; ".join(pedacos))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(bytes_)

    def _pagina_viagens(self) -> None:
        aparelho = self._sessao()
        if aparelho is None:
            self._html(200, pagina.formulario())
            return
        viagens = []
        pasta = self._pasta_do_aparelho(aparelho)
        if pasta.is_dir():
            for arquivo in sorted(pasta.iterdir(), reverse=True):
                if not arquivo.is_file() or not PADRAO_VIAGEM.match(arquivo.name):
                    continue
                r = viagem.resumo(arquivo)
                if r is not None:
                    viagens.append(r)
        self._html(200, pagina.listagem(aparelho, viagens))

    def _pagina_mapa(self, nome: str) -> None:
        aparelho = self._sessao()
        if aparelho is None:
            self._html(401, pagina.formulario("entre para ver a viagem"))
            return
        arquivo = self._pasta_do_aparelho(aparelho) / nome
        if not arquivo.is_file():
            self._html(404, pagina.aviso("viagem nao encontrada"))
            return
        try:
            v = viagem.le_viagem(arquivo)
        except viagem.ErroDeViagem as e:
            self._html(422, pagina.aviso(f"nao da para desenhar: {e}"))
            return
        try:
            corpo = viagem.desenha(v, self.mapa.get("thunderforest", ""))
        except ImportError:
            # O servidor sobe sem o folium de proposito: faltar a dependencia
            # nao pode derrubar a distribuicao da base, que e a funcao
            # principal. Quem abre a pagina recebe a instrucao.
            log.error("folium ausente: `pip install folium` para a pagina de viagens")
            self._html(503, pagina.aviso(
                "o mapa precisa do folium: pip install folium"))
            return
        self._html(200, corpo)

    def _entra(self) -> None:
        agora = time.monotonic()
        de_onde = self.client_address[0]
        espera = self.castigo.get(de_onde, 0.0)
        if espera > agora:
            time.sleep(min(espera - agora, ESPERA_APOS_ERRO_S))

        try:
            tamanho = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            tamanho = 0
        if tamanho <= 0 or tamanho > 4096:
            self._html(400, pagina.formulario("formulario invalido"))
            return
        bruto = self.rfile.read(tamanho).decode("utf-8", errors="replace")
        campos = urllib.parse.parse_qs(bruto)
        token = (campos.get("token") or [""])[0].strip()

        sid = self._abre_sessao(token)
        if sid is None:
            self.castigo[de_onde] = time.monotonic() + ESPERA_APOS_ERRO_S
            log.warning("token recusado na pagina, de %s", de_onde)
            self._html(401, pagina.formulario("token desconhecido"))
            return
        self.castigo.pop(de_onde, None)
        self._html(200, pagina.redireciona(ROTA_VIAGENS),
                   cookie=(COOKIE_SESSAO, sid))

    def _sai(self) -> None:
        bruto = self.headers.get("Cookie", "")
        for parte in bruto.split(";"):
            chave, _, valor = parte.strip().partition("=")
            if chave == COOKIE_SESSAO:
                self.sessoes.pop(valor, None)
        self._html(200, pagina.redireciona(ROTA_VIAGENS),
                   cookie=(COOKIE_SESSAO, ""))

    def _trata_viagens(self, caminho: str) -> bool:
        """`True` se a rota era de viagens e ja foi respondida."""
        if caminho == ROTA_VIAGENS or caminho == ROTA_VIAGENS + "/":
            if self.command == "POST":
                self._entra()
            else:
                self._pagina_viagens()
            return True
        if caminho == ROTA_VIAGENS + "/sair":
            self._sai()
            return True
        if caminho.startswith(ROTA_VIAGENS + "/"):
            nome = caminho[len(ROTA_VIAGENS) + 1:]
            # Mesma lista branca do envio: o nome vira caminho em disco, e
            # recusar o que nao casa com o padrao e a defesa contra travessia.
            if not PADRAO_VIAGEM.match(nome):
                self._html(404, pagina.aviso("viagem nao encontrada"))
                return True
            self._pagina_mapa(nome)
            return True
        return False

    def do_POST(self) -> None:  # noqa: N802
        caminho = self.path.split("?", 1)[0]
        if self._trata_viagens(caminho):
            return
        self._erro(404, "rota desconhecida", False)

    def do_PUT(self) -> None:  # noqa: N802
        self._recebe()

    def do_GET(self) -> None:  # noqa: N802
        caminho = self.path.split("?", 1)[0]
        # A pagina de viagens tem autenticacao PROPRIA, por sessao: ela nao
        # passa pelo `_barra_desconhecido`, que espera o token no cabecalho e
        # devolveria 401 a um navegador que acabou de fazer login.
        if self._trata_viagens(caminho):
            return
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
                  recebidos: Path | None = None,
                  mapa: dict[str, str] | None = None) -> Servidor:
    raiz = dados.resolve()
    manipulador = type("ManipuladorLigado", (Manipulador,),
                       {"dados": raiz,
                        "aparelhos": dict(aparelhos or {}),
                        "mapa": dict(mapa or {}),
                        # Dicionarios PROPRIOS por servidor, e nao os da
                        # classe base: dois servidores no mesmo processo --
                        # o que a suite faz o tempo todo -- dividiriam
                        # sessoes, e um teste veria a sessao do outro.
                        "sessoes": {},
                        "castigo": {},
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
        lista = le_aparelhos(args.aparelhos)
        aparelhos, mapa = lista.por_token, lista.mapa
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
        log.info("pagina de viagens em %s", ROTA_VIAGENS)
        if not mapa.get("thunderforest"):
            log.info("sem chave do Thunderforest em [mapa]: o mapa usa "
                     "OpenStreetMap")
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
                             aparelhos, args.recebidos, mapa)
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
