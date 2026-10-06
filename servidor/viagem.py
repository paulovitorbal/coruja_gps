#!/usr/bin/env python3
"""Lê um registro de viagem e desenha o mapa com reprodução.

Separado do `servidor.py` de propósito: é o **único** arquivo do projeto que
depende de biblioteca externa (folium). Mantê-lo isolado deixa claro onde a
regra de "só a biblioteca padrão" foi quebrada, e permite que o servidor
continue subindo — sem a página de viagens — se o folium faltar.

O formato do arquivo é o que o firmware grava (`FormatoLog.h`):

    # coruja_gps viagem v1
    utc;lat;lon;v_media;dist_km
    2026-10-06T12:38:00Z;-15.83075;-47.98297;0.6;0.00

**Um ponto por minuto.** Não é uma trilha densa: os pontos ficam a centenas
de metros um do outro. É o que cabe no cartão para uma viagem longa, e é o que
define como a reprodução funciona — ver `PASSO_S`.
"""

from __future__ import annotations

import datetime
import html
import json
from dataclasses import dataclass
from pathlib import Path

#: Primeira linha que o firmware grava. Serve de assinatura: um `.log` que
#: não comece assim não é viagem, e tentar desenhá-lo daria um mapa vazio em
#: vez de uma mensagem.
#:
#: **As duas versões são aceitas, e as colunas são as mesmas.** O que mudou da
#: v1 para a v2 foi a taxa: uma linha por minuto virou dez. Um aparelho que
#: ainda não foi atualizado continua mandando v1, e recusá-lo apagaria da
#: página as viagens que já estão no servidor.
ASSINATURAS = (
    "# coruja_gps viagem v1",   # 1 linha/min — até 2026-10-06
    "# coruja_gps viagem v2",   # 10 linhas/min
)

CABECALHO_COLUNAS = "utc;lat;lon;v_media;dist_km"

#: Resolução da linha do tempo da reprodução, em segundos.
#:
#: O dado tem um ponto por MINUTO. Animar de minuto em minuto deixaria o
#: marcador parado 60 s e depois saltando — o que parece travamento.
#:
#: ⚠️ **O Leaflet.TimeDimension NÃO interpola.** Eu supus que sim e escrevi
#: isto aqui como se fosse fato; o teste de navegador mostrou o marcador
#: parado. O `_getFeatureBetweenDates` da biblioteca **fatia** a lista de
#: coordenadas entre dois instantes e põe o marcador na última do pedaço — a
#: posição só muda quando se cruza um registro de verdade.
#:
#: Por isso a trilha é adensada aqui, em Python, por `_densifica()`. As
#: posições intermediárias são **interpolação linear, não medição**, e a
#: página diz isso em letras.
#:
#: Dois, e não cinco: o firmware v2 grava a cada 6 s, e com passo de 5 s a
#: divisão daria um sub-passo só — ou seja, nenhum adensamento, e o marcador
#: voltaria a andar aos pulos de 6 s. Com 2 s cada intervalo vira três
#: sub-passos. Para um arquivo v1, de 60 s, vira trinta.
PASSO_S = 2

#: As velocidades que a página oferece, e o que cada uma significa.
#:
#: `1x` é **tempo real**: um minuto de viagem leva um minuto para passar.
#: Decisão do autor em 2026-10-06, com a consequência conhecida — uma viagem
#: de 22 minutos leva 22 minutos em 1x, e 1,4 minuto em 16x.
VELOCIDADES = (1, 2, 4, 8, 16)


class ErroDeViagem(Exception):
    """O arquivo não é um registro de viagem utilizável."""


@dataclass(frozen=True)
class PontoViagem:
    quando: datetime.datetime
    lat: float
    lon: float
    v_media_kmh: float
    dist_km: float


@dataclass(frozen=True)
class Viagem:
    nome: str
    pontos: list[PontoViagem]

    @property
    def inicio(self) -> datetime.datetime:
        return self.pontos[0].quando

    @property
    def fim(self) -> datetime.datetime:
        return self.pontos[-1].quando

    @property
    def duracao(self) -> datetime.timedelta:
        return self.fim - self.inicio

    @property
    def distancia_km(self) -> float:
        return self.pontos[-1].dist_km

    @property
    def v_maxima_kmh(self) -> float:
        return max(p.v_media_kmh for p in self.pontos)

    @property
    def v_media_kmh(self) -> float:
        """Média do percurso, e não média das médias.

        Distância sobre tempo. A média das `v_media` de cada minuto daria
        outro número — e o errado, porque cada minuto pesaria igual mesmo
        tendo percorrido distâncias diferentes.
        """
        horas = self.duracao.total_seconds() / 3600
        return self.distancia_km / horas if horas > 0 else 0.0


def le_viagem(caminho: Path) -> Viagem:
    """Lê o arquivo e devolve a viagem. Levanta `ErroDeViagem`."""
    try:
        linhas = caminho.read_text(encoding="utf-8").splitlines()
    except OSError as e:
        raise ErroDeViagem(f"nao consegui ler: {e}") from e

    if not linhas or linhas[0].strip() not in ASSINATURAS:
        raise ErroDeViagem("primeira linha nao e assinatura de viagem "
                           f"conhecida ({' ou '.join(ASSINATURAS)})")

    pontos: list[PontoViagem] = []
    for n, bruta in enumerate(linhas[1:], start=2):
        linha = bruta.strip()
        if not linha or linha.startswith("#") or linha == CABECALHO_COLUNAS:
            continue
        campos = linha.split(";")
        if len(campos) != 5:
            # Linha torta nao derruba a viagem inteira: o cartao pode ter
            # sido puxado no meio de uma gravacao, e as linhas anteriores
            # continuam valendo.
            continue
        try:
            quando = datetime.datetime.strptime(
                campos[0], "%Y-%m-%dT%H:%M:%SZ").replace(
                    tzinfo=datetime.timezone.utc)
            pontos.append(PontoViagem(
                quando=quando,
                lat=float(campos[1]),
                lon=float(campos[2]),
                v_media_kmh=float(campos[3]),
                dist_km=float(campos[4]),
            ))
        except ValueError:
            continue

    if len(pontos) < 2:
        raise ErroDeViagem(
            f"so {len(pontos)} ponto(s) utilizavel(eis): nao da para desenhar "
            "um trajeto")

    # Ordena pelo carimbo. O firmware grava em ordem, mas um arquivo
    # concatenado a mao -- ou uma retomada de viagem -- pode nao estar.
    return Viagem(nome=caminho.name, pontos=sorted(pontos, key=lambda p: p.quando))


def resumo(caminho: Path) -> dict | None:
    """Os números da viagem, para a listagem. `None` se não der para ler.

    Lê o arquivo inteiro, e isso é aceitável: um registro de viagem de duas
    horas tem 120 linhas.
    """
    try:
        v = le_viagem(caminho)
    except ErroDeViagem:
        return None
    return {
        "nome": v.nome,
        "inicio": v.inicio.strftime("%Y-%m-%d %H:%M"),
        "duracao_min": round(v.duracao.total_seconds() / 60),
        "distancia_km": round(v.distancia_km, 2),
        "v_media_kmh": round(v.v_media_kmh, 1),
        "v_maxima_kmh": round(v.v_maxima_kmh, 1),
        "pontos": len(v.pontos),
    }


# ---------------------------------------------------------------------------
# O mapa
# ---------------------------------------------------------------------------

#: Thunderforest Atlas. A chave vai na URL, como o provedor exige.
#:
#: ⚠️ Ela aparece nas requisições de tile que o NAVEGADOR faz, então é visível
#: a quem abrir o inspetor. É assim com todo provedor de tiles com chave; a
#: proteção real é a cota e o limite de domínio no painel do Thunderforest,
#: não o sigilo da string.
URL_ATLAS = ("https://{s}.tile.thunderforest.com/atlas/{z}/{x}/{y}.png"
             "?apikey={chave}")
ATRIBUICAO_ATLAS = (
    '&copy; <a href="https://www.thunderforest.com/">Thunderforest</a>, '
    '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
)

#: Sem chave do Thunderforest a página ainda funciona, com o tile padrão.
#: Degradar é melhor que mostrar mapa em branco: o trajeto é o conteúdo, e o
#: tema é a aparência.
URL_OSM = "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
ATRIBUICAO_OSM = (
    '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
)


def _densifica(v: Viagem, passo_s: int = PASSO_S) -> list[PontoViagem]:
    """Um ponto a cada `passo_s` segundos, por interpolação linear.

    ⚠️ **Inventa posição.** Entre dois registros de minuto o carro não andou
    em linha reta a velocidade constante — ele fez curvas, parou em semáforo,
    acelerou. O que isto produz é uma animação legível, não um trajeto
    medido, e a página precisa dizer isso a quem olha.
    #
    Existe porque a biblioteca de animação não interpola: ela fatia a lista
    de coordenadas, e com um ponto por minuto o marcador ficaria 60 s parado.

    Interpolação linear em lat/lon direto, sem geodésica: entre dois pontos a
    algumas centenas de metros a diferença é de centímetros, bem abaixo do
    erro do próprio GPS.
    """
    denso: list[PontoViagem] = []
    for atual, proximo in zip(v.pontos, v.pontos[1:]):
        intervalo = (proximo.quando - atual.quando).total_seconds()
        if intervalo <= 0:
            continue
        passos = max(1, int(intervalo // passo_s))
        for k in range(passos):
            f = k / passos
            denso.append(PontoViagem(
                quando=atual.quando + datetime.timedelta(
                    seconds=intervalo * f),
                lat=atual.lat + (proximo.lat - atual.lat) * f,
                lon=atual.lon + (proximo.lon - atual.lon) * f,
                # A velocidade e a distancia NAO sao interpoladas: elas valem
                # para o minuto inteiro, e inventar valores intermediarios
                # daria numeros que ninguem mediu.
                v_media_kmh=atual.v_media_kmh,
                dist_km=atual.dist_km,
            ))
    denso.append(v.pontos[-1])
    return denso


def _trilha_geojson(v: Viagem) -> dict:
    """A viagem como uma LineString com carimbo por vértice, já adensada."""
    pontos = _densifica(v)
    return {
        "type": "FeatureCollection",
        "features": [{
            "type": "Feature",
            "geometry": {
                "type": "LineString",
                "coordinates": [[p.lon, p.lat] for p in pontos],
            },
            "properties": {
                "times": [p.quando.strftime("%Y-%m-%dT%H:%M:%SZ")
                          for p in pontos],
                "style": {"color": "#d7263d", "weight": 4},
                "icon": "circle",
                "iconstyle": {
                    "fillColor": "#d7263d",
                    "fillOpacity": 0.9,
                    "stroke": "true",
                    "color": "#ffffff",
                    "weight": 2,
                    "radius": 7,
                },
            },
        }],
    }


def desenha(v: Viagem, chave_thunderforest: str = "") -> str:
    """Devolve o HTML do mapa, com os controles de reprodução.

    `import folium` acontece aqui dentro, e não no topo do módulo: assim o
    servidor sobe e serve a base mesmo sem a dependência instalada, e quem
    abrir a página de viagens recebe uma mensagem clara em vez de um 500.
    """
    import folium                      # noqa: PLC0415
    from folium.plugins import TimestampedGeoJson  # noqa: PLC0415

    centro = [
        sum(p.lat for p in v.pontos) / len(v.pontos),
        sum(p.lon for p in v.pontos) / len(v.pontos),
    ]
    mapa = folium.Map(location=centro, zoom_start=14, tiles=None,
                      control_scale=True)

    if chave_thunderforest:
        folium.TileLayer(
            tiles=URL_ATLAS.replace("{chave}", chave_thunderforest),
            attr=ATRIBUICAO_ATLAS, name="Atlas", subdomains="abc",
            max_zoom=22).add_to(mapa)
    else:
        folium.TileLayer(tiles=URL_OSM, attr=ATRIBUICAO_OSM,
                         name="OpenStreetMap", subdomains="abc",
                         max_zoom=19).add_to(mapa)

    # Enquadra a viagem inteira: `zoom_start` sozinho erra em trajeto longo.
    mapa.fit_bounds([[min(p.lat for p in v.pontos),
                      min(p.lon for p in v.pontos)],
                     [max(p.lat for p in v.pontos),
                      max(p.lon for p in v.pontos)]], padding=(30, 30))

    folium.Marker(
        [v.pontos[0].lat, v.pontos[0].lon], tooltip="inicio",
        icon=folium.Icon(color="green", icon="play", prefix="fa")).add_to(mapa)
    folium.Marker(
        [v.pontos[-1].lat, v.pontos[-1].lon], tooltip="fim",
        icon=folium.Icon(color="red", icon="stop", prefix="fa")).add_to(mapa)

    # `transition_time` e `period` sao os dois numeros que governam a
    # velocidade. Ver `PASSO_S` e `VELOCIDADES`: em 1x, cada passo de 5 s de
    # dado leva 5 s de relogio -- tempo real.
    animacao = TimestampedGeoJson(
        _trilha_geojson(v),
        period=f"PT{PASSO_S}S",
        duration=None,          # o rastro fica, em vez de sumir atras
        transition_time=PASSO_S * 1000,
        auto_play=False,
        loop=False,
        # O deslizador nativo de velocidade sai de cena: ele pensa em
        # "passos por segundo", e mexer nele depois de escolher 4x
        # sobrescreveria o `transitionTime` que o botao acabou de definir --
        # dois controles disputando o mesmo numero.
        speed_slider=False,
        loop_button=False,
        date_options="DD/MM/YYYY HH:mm:ss",
        time_slider_drag_update=True,
    )
    animacao.add_to(mapa)

    mapa.get_root().html.add_child(folium.Element(_painel(v)))
    mapa.get_root().html.add_child(folium.Element(_controles_js(PASSO_S)))
    return mapa.get_root().render()


def _painel(v: Viagem) -> str:
    """Os números da viagem, fixos num canto do mapa."""
    linhas = [
        ("distancia", f"{v.distancia_km:.2f} km"),
        ("duracao", f"{round(v.duracao.total_seconds() / 60)} min"),
        ("media", f"{v.v_media_kmh:.1f} km/h"),
        ("maxima", f"{v.v_maxima_kmh:.1f} km/h"),
        ("pontos", f"{len(v.pontos)}"),
    ]
    itens = "".join(
        f'<div class="cg-item"><span>{html.escape(r)}</span>'
        f'<strong>{html.escape(d)}</strong></div>'
        for r, d in linhas)
    return f"""
<style>
  .cg-painel {{
    position: fixed; top: 12px; right: 12px; z-index: 1000;
    background: rgba(255,255,255,.94); border-radius: 10px;
    padding: 12px 14px; font: 13px/1.5 system-ui, sans-serif;
    box-shadow: 0 2px 12px rgba(0,0,0,.2); min-width: 170px;
  }}
  /* Sem `text-transform`: o titulo e o NOME DO ARQUIVO, e maiusculizar um
     identificador faz ele deixar de bater com o que esta no cartao. */
  .cg-painel h3 {{ margin: 0 0 8px; font-size: 13px; color: #333;
                   font-family: ui-monospace, monospace; }}
  .cg-item {{ display: flex; justify-content: space-between; gap: 14px; }}
  .cg-item span {{ color: #666; }}
  .cg-nota-painel {{ margin: 10px 0 0; padding-top: 8px; font-size: 11px;
                     line-height: 1.4; color: #888;
                     border-top: 1px solid #e3e3e0; }}
  .cg-vel {{
    position: fixed; bottom: 92px; left: 50%; transform: translateX(-50%);
    z-index: 1000; display: flex; gap: 6px; align-items: center;
    background: rgba(255,255,255,.94); border-radius: 10px; padding: 8px 10px;
    font: 13px/1 system-ui, sans-serif; box-shadow: 0 2px 12px rgba(0,0,0,.2);
  }}
  .cg-vel button {{
    border: 1px solid #ccc; background: #fff; border-radius: 6px;
    padding: 6px 11px; cursor: pointer; font: inherit; font-weight: 600;
  }}
  .cg-vel button[aria-pressed="true"] {{
    background: #d7263d; border-color: #d7263d; color: #fff;
  }}
  .cg-vel .cg-nota {{ color: #666; font-weight: 400; margin-left: 4px; }}
</style>
<div class="cg-painel">
  <h3>{html.escape(v.nome)}</h3>
  {itens}
  <p class="cg-nota-painel">um registro por minuto; a posicao entre eles e
  interpolada, nao medida</p>
</div>
"""


def _controles_js(passo_s: int) -> str:
    """Os botões de velocidade, ligados ao reprodutor do TimeDimension.

    Por que isto existe em vez do controle nativo: o reprodutor do
    Leaflet.TimeDimension traz um *slider* de velocidade em "passos por
    segundo", que não é a pergunta que o usuário faz. Ele quer `4x`, e `4x`
    tem de significar quatro vezes o tempo real — a conta é
    `transitionTime = passo_de_dado / velocidade`.
    """
    velocidades = json.dumps(list(VELOCIDADES))
    return f"""
<div class="cg-vel" id="cg-vel" role="group" aria-label="velocidade">
  <span class="cg-nota">velocidade</span>
</div>
<script>
(function () {{
  var PASSO_MS = {passo_s * 1000};
  var VELOCIDADES = {velocidades};

  // O reprodutor vive DENTRO do controle de linha do tempo, que o folium
  // cria como `var timeDimensionControl` no topo do proprio bloco de script
  // -- ou seja, em `window`. Conferido na saida do folium 0.20 e na API do
  // leaflet-timedimension 1.1.1.
  //
  // `_player` tem sublinhado e e privado por convencao. Nao ha caminho
  // publico para trocar a velocidade de fora, e a alternativa seria criar um
  // SEGUNDO reprodutor sobre a mesma linha do tempo -- dois donos do mesmo
  // estado, que e pior que um acesso privado anotado.
  function achaReprodutor() {{
    var c = window.timeDimensionControl;
    return (c && c._player) ? c._player : null;
  }}

  function liga(reprodutor) {{
    var caixa = document.getElementById('cg-vel');
    var botoes = [];

    function escolhe(v) {{
      // `transitionTime` e quantos MILISSEGUNDOS de relogio cada passo de
      // dado leva. Passo de 5 s de viagem em 5000 ms = tempo real = 1x.
      reprodutor.setTransitionTime(Math.round(PASSO_MS / v));
      botoes.forEach(function (b) {{
        b.setAttribute('aria-pressed', String(Number(b.dataset.v) === v));
      }});
    }}

    VELOCIDADES.forEach(function (v) {{
      var b = document.createElement('button');
      b.textContent = v + 'x';
      b.dataset.v = v;
      b.setAttribute('aria-pressed', 'false');
      b.addEventListener('click', function () {{ escolhe(v); }});
      caixa.appendChild(b);
      botoes.push(b);
    }});

    var nota = document.createElement('span');
    nota.className = 'cg-nota';
    nota.textContent = '1x = tempo real';
    caixa.appendChild(nota);

    escolhe(1);
  }}

  var tentativas = 0;
  var timer = setInterval(function () {{
    var r = achaReprodutor();
    if (r) {{ clearInterval(timer); liga(r); return; }}
    // Desiste depois de 10 s em vez de girar para sempre: se o reprodutor
    // nao apareceu, os botoes nao teriam o que controlar, e um laco eterno
    // so gastaria bateria de quem abriu a pagina.
    if (++tentativas > 100) {{
      clearInterval(timer);
      document.getElementById('cg-vel').innerHTML =
        '<span class="cg-nota">reprodutor nao carregou</span>';
    }}
  }}, 100);
}})();
</script>
"""
