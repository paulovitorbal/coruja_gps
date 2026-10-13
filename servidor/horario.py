"""Horários que seguem o relógio de quem está olhando.

O aparelho grava **UTC**, e é assim que o dado fica no cartão, no nome do
arquivo e no servidor: um instante sem ambiguidade, que não depende de onde
o arquivo for parar. Trocar isso quebraria a correspondência entre o que está
gravado e o que se lê.

Mas quem abre a página não quer saber que horas eram em Greenwich — quer
saber a que horas **saiu de casa**. E o servidor não tem como saber o fuso de
quem pediu a página: isso só existe no navegador.

Daí a divisão: o HTML sai com o instante em UTC dentro de um `<time
datetime>`, que é o que a norma manda, e um script o reescreve no fuso local.
Sem JavaScript, o que fica na tela é o UTC rotulado — correto, só menos
cômodo.
"""

from __future__ import annotations

import datetime
import html

#: Formato do texto que vai no HTML, usado quando o script não roda.
_FALLBACK = "%d/%m/%Y %H:%M"


def marca(quando: datetime.datetime, fmt: str = "completo") -> str:
    """Um instante como `<time>`, pronto para o navegador converter.

    `fmt` escolhe o que o script escreve depois: `completo` traz data e hora,
    `hora` só as horas e minutos — que é o que cabe num aviso de pausa, onde
    a data já está no título da página.
    """
    iso = quando.strftime("%Y-%m-%dT%H:%M:%SZ")
    visivel = quando.strftime("%H:%M" if fmt == "hora" else _FALLBACK)
    return (f'<time datetime="{html.escape(iso)}" data-fmt="{html.escape(fmt)}">'
            f'{html.escape(visivel)} UTC</time>')


#: O script que converte todo `<time datetime>` da página para o fuso local.
#:
#: Observa o DOM porque nem tudo existe no carregamento: o aviso de pausa de
#: um marcador do Leaflet só entra na página quando o ponteiro passa por cima
#: dele, e um conversor que rodasse uma vez só o deixaria em UTC.
SCRIPT = """
<script>
(function () {
  function ajusta(el) {
    if (el.dataset.ajustado) { return; }
    var iso = el.getAttribute('datetime');
    if (!iso) { return; }
    var d = new Date(iso);
    if (isNaN(d.getTime())) { return; }   // carimbo ilegivel: deixa o UTC
    // O FUSO vem do navegador; o FORMATO fica em pt-BR, como o resto da
    // pagina. Deixar o locale do navegador decidir devolvia `10/11/2026,
    // 04:28 PM` num navegador em ingles -- que, numa pagina em portugues,
    // le-se como 10 de novembro. A data fica ambigua justo no campo que
    // identifica a viagem.
    el.textContent = (el.dataset.fmt === 'hora')
      ? d.toLocaleTimeString('pt-BR', {hour: '2-digit', minute: '2-digit',
                                       hour12: false})
      : d.toLocaleString('pt-BR', {day: '2-digit', month: '2-digit',
                                   year: 'numeric', hour: '2-digit',
                                   minute: '2-digit', hour12: false});
    el.dataset.ajustado = '1';
  }
  function varre(raiz) {
    if (raiz.nodeType !== 1 && raiz.nodeType !== 9) { return; }
    if (raiz.matches && raiz.matches('time[datetime]')) { ajusta(raiz); }
    if (raiz.querySelectorAll) {
      Array.prototype.forEach.call(
        raiz.querySelectorAll('time[datetime]'), ajusta);
    }
  }
  varre(document);
  if (window.MutationObserver && document.body) {
    new MutationObserver(function (mudancas) {
      mudancas.forEach(function (m) {
        Array.prototype.forEach.call(m.addedNodes, varre);
      });
    }).observe(document.body, {childList: true, subtree: true});
  }
})();
</script>
"""
