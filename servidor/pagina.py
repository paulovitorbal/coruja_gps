#!/usr/bin/env python3
"""As páginas HTML da consulta de viagens.

Só a biblioteca padrão: o folium entra apenas no `viagem.py`, para desenhar o
mapa. Aqui é texto.

**Tudo que vem de fora passa por `html.escape`.** O nome do aparelho e o nome
do arquivo vêm, em última instância, de um cartão que alguém pode ter mexido —
e a página mostra os dois.
"""

from __future__ import annotations

import html

import horario

ESTILO = """
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 32px 20px;
    font: 15px/1.6 system-ui, -apple-system, sans-serif;
    background: #f6f6f4; color: #1a1a1a;
    display: flex; flex-direction: column; align-items: center;
  }
  @media (prefers-color-scheme: dark) {
    body { background: #16171a; color: #e8e8e8; }
    .cartao, .viagem { background: #1f2024; border-color: #2e3036; }
    input { background: #16171a; color: #e8e8e8; border-color: #3a3d44; }
    .viagem:hover { border-color: #d7263d; }
  }
  main { width: 100%; max-width: 720px; }
  h1 { font-size: 20px; margin: 0 0 4px; letter-spacing: -.01em; }
  .sub { color: #767676; margin: 0 0 24px; font-size: 14px; }
  .cartao {
    background: #fff; border: 1px solid #e3e3e0; border-radius: 12px;
    padding: 24px;
  }
  label { display: block; font-size: 13px; color: #767676; margin-bottom: 6px; }
  input {
    width: 100%; padding: 11px 13px; font: inherit;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    border: 1px solid #d5d5d0; border-radius: 8px;
  }
  input:focus { outline: 2px solid #d7263d; outline-offset: 1px;
                border-color: transparent; }
  button {
    margin-top: 14px; padding: 11px 20px; font: inherit; font-weight: 600;
    background: #d7263d; color: #fff; border: 0; border-radius: 8px;
    cursor: pointer;
  }
  button:hover { background: #b61f33; }
  .erro {
    background: #fdecee; border: 1px solid #f3c2c9; color: #8c1c2b;
    border-radius: 8px; padding: 10px 13px; margin-bottom: 18px; font-size: 14px;
  }
  .viagem {
    display: block; background: #fff; border: 1px solid #e3e3e0;
    border-radius: 12px; padding: 16px 18px; margin-bottom: 10px;
    text-decoration: none; color: inherit; transition: border-color .12s;
  }
  .viagem:hover { border-color: #d7263d; }
  /* Era monoespacado porque o titulo era o NOME DO ARQUIVO, e maiusculizar
     ou espacar um identificador o faz deixar de bater com o que esta no
     cartao. Agora o titulo e a data da viagem, que se le melhor na fonte do
     resto da pagina. O nome do arquivo continua no `title` do link, para
     quem precisar casar com o cartao. */
  .viagem h2 { margin: 0 0 8px; font-size: 16px; font-weight: 600;
               letter-spacing: -.01em; }
  .viagem h2 small { font-size: 12px; font-weight: 500; color: #767676;
                     margin-left: 4px; }
  .nums { display: flex; flex-wrap: wrap; gap: 18px; font-size: 13px;
          color: #767676; }
  .nums b { color: inherit; font-weight: 600; }
  .rodape { margin-top: 22px; font-size: 13px; color: #767676;
            display: flex; justify-content: space-between; align-items: center; }
  .rodape form { margin: 0; }
  .rodape button { margin: 0; background: transparent; color: #767676;
                   border: 1px solid #d5d5d0; padding: 6px 12px; }
  .rodape button:hover { background: transparent; color: #d7263d;
                         border-color: #d7263d; }
  .vazio { color: #767676; }
"""


def _moldura(titulo: str, miolo: str) -> str:
    return (
        "<!doctype html>\n"
        '<html lang="pt-BR"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        # A pagina de consulta nao deve ser indexada: ela so existe para quem
        # tem o token, e um buscador a listando so convidaria tentativa.
        '<meta name="robots" content="noindex, nofollow">'
        f"<title>{html.escape(titulo)}</title>"
        f"<style>{ESTILO}</style></head><body><main>{miolo}</main>"
        # No fim do corpo: o conversor precisa dos `<time>` já no DOM, e
        # `defer` não vale para script embutido.
        f"{horario.SCRIPT}</body></html>"
    )


def formulario(erro: str = "") -> str:
    aviso = f'<div class="erro">{html.escape(erro)}</div>' if erro else ""
    return _moldura("coruja gps — viagens", f"""
      <h1>Viagens</h1>
      <p class="sub">Informe o token do aparelho para ver os trajetos que ele
      enviou.</p>
      <div class="cartao">
        {aviso}
        <form method="post" action="/viagens">
          <label for="token">token do aparelho</label>
          <input id="token" name="token" type="password" autocomplete="off"
                 autofocus spellcheck="false" required>
          <button type="submit">entrar</button>
        </form>
      </div>
    """)


def _trechos(v: dict) -> str:
    """"em N trechos", só quando a viagem foi partida por pausas.

    Sem isto a duração fica sem explicação: uma viagem que começou 12:40 e
    terminou 15:31 mostrando "35 min" parece erro, quando o que houve foram
    duas paradas longas — e o tempo contado é o de viagem, não o de relógio.
    """
    n = v.get("trechos", 1)
    if n <= 1:
        return ""
    return f'<span class="trechos">em <b>{n}</b> trechos</span>'


def listagem(aparelho: str, viagens: list[dict]) -> str:
    if viagens:
        itens = "".join(f"""
          <a class="viagem" href="/viagens/{html.escape(v['nome'])}"
             title="{html.escape(v['nome'])}">
            <h2>{v.get('quando_html', html.escape(v['inicio']))}</h2>
            <div class="nums">
              <span><b>{v['distancia_km']}</b> km</span>
              <span><b>{v['duracao_min']}</b> min</span>
              <span>média <b>{v['v_media_kmh']}</b> km/h</span>
              <span>máx <b>{v['v_maxima_kmh']}</b> km/h</span>
              {_trechos(v)}
            </div>
          </a>""" for v in viagens)
        # "viagem" + "ns" dava **viagemns**. O plural de "viagem" troca o
        # -m por -ns; não acrescenta.
        contagem = (f"{len(viagens)} viagens" if len(viagens) > 1
                    else "1 viagem")
    else:
        itens = ('<div class="cartao vazio">Nenhuma viagem recebida deste '
                 'aparelho ainda. Use <b>enviar dados</b> no menu do '
                 'coruja.</div>')
        contagem = "nenhuma viagem"

    return _moldura(f"viagens — {aparelho}", f"""
      <h1>{html.escape(aparelho)}</h1>
      <p class="sub">{contagem}</p>
      {itens}
      <div class="rodape">
        <span>horários no fuso deste computador<noscript> — sem
              JavaScript eles ficam em UTC, como o aparelho grava</noscript>
        </span>
        <form method="post" action="/viagens/sair"><button>sair</button></form>
      </div>
    """)


def aviso(texto: str) -> str:
    return _moldura("coruja gps", f"""
      <h1>Viagens</h1>
      <div class="cartao"><p>{html.escape(texto)}</p>
      <p><a href="/viagens">voltar</a></p></div>
    """)


def redireciona(para: str) -> str:
    """Página de redirecionamento, em vez de um 302.

    O `Set-Cookie` precisa chegar junto, e um 302 com cookie funciona — mas
    alguns clientes de teste seguem o redirecionamento antes de guardar o
    cookie. Uma página explícita torna o passo visível e testável.
    """
    destino = html.escape(para, quote=True)
    return (
        "<!doctype html>\n"
        '<html lang="pt-BR"><head><meta charset="utf-8">'
        f'<meta http-equiv="refresh" content="0; url={destino}">'
        f'<title>redirecionando</title></head>'
        f'<body><a href="{destino}">continuar</a></body></html>'
    )
