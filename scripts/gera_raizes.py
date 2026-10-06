#!/usr/bin/env python3
"""Gera o pacote de raízes confiáveis que o firmware embute.

    python3 scripts/gera_raizes.py

Baixa os certificados raiz, **confere a impressão digital de cada um contra
um valor fixado aqui** e escreve `firmware/src/rede/RaizesConfiaveis.h`.

⚠️ A conferência é o ponto deste script, não o download. Uma raiz de confiança
embutida no firmware é o que decide de quem o aparelho aceita uma atualização:
trocada, ela não quebra nada visível — ela passa a validar o servidor errado.
Baixar sem conferir seria confiar em quem respondeu ao `curl`.

As impressões abaixo foram conferidas em 2026-10-06 contra as publicadas pelas
próprias autoridades. Para trocar uma raiz, troque também a impressão, de
propósito: é para doer.
"""

import hashlib
import subprocess
import sys
import urllib.request
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
DESTINO = RAIZ / "firmware/src/rede/RaizesConfiaveis.h"

#: (nome, url, sha256 do DER, por que esta aqui)
RAIZES = [
    ("GTS Root R4",
     "https://i.pki.goog/r4.crt",
     "349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d",
     "assina o certificado que o Cloudflare serve hoje (ECDSA P-384)"),
    ("ISRG Root X1",
     "https://letsencrypt.org/certs/isrgrootx1.pem",
     "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6",
     "Let's Encrypt, para onde o Cloudflare pode rotacionar (RSA 4096)"),
    ("ISRG Root X2",
     "https://letsencrypt.org/certs/isrg-root-x2.pem",
     "69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470",
     "a raiz EC da Let's Encrypt, para onde ela propria esta migrando"),
]


def como_pem(bruto: bytes) -> bytes:
    """Converte para PEM se vier em DER. Já em PEM, devolve como está."""
    if bruto.lstrip().startswith(b"-----BEGIN"):
        return bruto
    return subprocess.run(
        ["openssl", "x509", "-inform", "DER", "-outform", "PEM"],
        input=bruto, capture_output=True, check=True).stdout


def como_der(pem: bytes) -> bytes:
    return subprocess.run(
        ["openssl", "x509", "-outform", "DER"],
        input=pem, capture_output=True, check=True).stdout


def campo(pem: bytes, opcao: str) -> str:
    saida = subprocess.run(["openssl", "x509", "-noout", opcao],
                           input=pem, capture_output=True, check=True).stdout
    return saida.decode().split("=", 1)[1].strip()


def confere(nome: str, pem: bytes, impressao: str) -> str:
    """Levanta se o certificado não for o esperado. Devolve a data de expiração."""
    visto = hashlib.sha256(como_der(pem)).hexdigest()
    if visto != impressao:
        raise SystemExit(
            f"RECUSADO: {nome} tem sha256 {visto},\n"
            f"          e a impressao fixada e {impressao}.\n"
            "          Ou a autoridade trocou a raiz, ou alguem respondeu no\n"
            "          lugar dela. Confira a mao antes de mexer neste script.")

    # Raiz e auto-assinada por definicao. Um intermediario que passasse aqui
    # expiraria antes e quebraria o OTA sem aviso.
    sujeito = campo(pem, "-subject")
    emissor = campo(pem, "-issuer")
    if sujeito != emissor:
        raise SystemExit(f"RECUSADO: {nome} nao e auto-assinado "
                         f"(emitido por {emissor})")

    texto = subprocess.run(["openssl", "x509", "-noout", "-text"],
                           input=pem, capture_output=True,
                           check=True).stdout.decode()
    if "CA:TRUE" not in texto:
        raise SystemExit(f"RECUSADO: {nome} nao e uma CA")

    return campo(pem, "-enddate")


def main() -> int:
    blocos = []
    resumo = []
    for nome, url, impressao, motivo in RAIZES:
        print(f"baixando {nome}...", file=sys.stderr)
        with urllib.request.urlopen(url, timeout=30) as r:
            pem = como_pem(r.read())
        expira = confere(nome, pem, impressao)
        print(f"  ok, expira em {expira}", file=sys.stderr)
        resumo.append((nome, motivo, expira, impressao))
        blocos.append((nome, motivo, expira, pem.decode().strip()))

    linhas = [
        "// GERADO POR scripts/gera_raizes.py -- NAO EDITE A MAO.",
        "//",
        "// As autoridades de quem este aparelho aceita um servidor. Sao TRES, e",
        "// nao uma, porque quem escolhe a CA do certificado e o Cloudflare, nao o",
        "// dono do aparelho: no plano gratuito ele alterna entre a Google Trust",
        "// Services e a Let's Encrypt a criterio dele. Com uma raiz so, uma",
        "// rotacao do lado deles mataria o OTA -- e o OTA e justamente como o",
        "// aparelho se conserta em campo.",
        "//",
        "// ⚠️ A ISRG Root X1 e RSA de 4096 bits, e e por causa dela que o RSA",
        "// continua ligado no mbedTLS. As outras duas sao ECDSA P-384.",
        "//",
        "// Prazos, do mais curto para o mais longo:",
        "//",
    ]
    # Ordenado pela data de verdade, e nao pela ordem da lista acima: um
    # resumo que diz "do mais curto para o mais longo" e nao esta ordenado
    # engana quem o le para decidir quando agir.
    import datetime
    def data_de(texto: str) -> datetime.datetime:
        return datetime.datetime.strptime(texto.replace(" GMT", "").strip(),
                                          "%b %d %H:%M:%S %Y")
    for nome, _motivo, expira, _imp in sorted(resumo, key=lambda r: data_de(r[2])):
        linhas.append(f"//   {nome:<14} {expira}")
    linhas += [
        "//",
        "// Quando a primeira vencer, o aparelho continua funcionando pelas outras",
        "// -- desde que o Cloudflare esteja servindo uma cadeia que elas assinem.",
        "",
        "#pragma once",
        "",
        "namespace coruja {",
        "",
        "/// As raizes, em PEM, uma depois da outra.",
        "///",
        "/// O mbedTLS le varios certificados de um mesmo bloco, e por isso eles",
        "/// vao concatenados em vez de num vetor: `mbedtls_x509_crt_parse` quer",
        "/// um buffer so, com o terminador incluido no tamanho.",
        "constexpr const char kRaizesConfiaveis[] =",
    ]
    for nome, motivo, expira, pem in blocos:
        linhas.append(f"    // {nome} -- {motivo}")
        linhas.append(f"    // expira em {expira}")
        for linha in pem.splitlines():
            linhas.append(f'    "{linha}\\n"')
        linhas.append("")
    linhas[-1] = "    ;"
    linhas += [
        "",
        "/// Tamanho que o `mbedtls_x509_crt_parse` espera: **com** o terminador.",
        "/// Sem ele a biblioteca recusa o PEM inteiro, e o sintoma e um handshake",
        "/// que falha sem explicar por que.",
        "constexpr unsigned kTamanhoRaizes = sizeof kRaizesConfiaveis;",
        "",
        "}  // namespace coruja",
        "",
    ]
    DESTINO.write_text("\n".join(linhas), encoding="utf-8")
    print(f"escrito: {DESTINO} ({DESTINO.stat().st_size} bytes)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
