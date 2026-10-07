# ⚠️ A regra "só a biblioteca padrão" foi quebrada em 2026-10-06, por decisão
# do autor: a página de viagens desenha o mapa com **folium**.
#
# O que se perdeu: a imagem passa a ter camada de dependência, e `folium`
# arrasta `jinja2`, `branca`, `numpy` e `requests` junto. O que se ganhou: o
# mapa com reprodução, que fazer à mão custaria JavaScript próprio para
# manter.
#
# A quebra é **contida**: só `viagem.py` importa folium, e o import acontece
# DENTRO da função que desenha. Sem a dependência o servidor ainda sobe e
# distribui a base — que é a função principal —, e quem abrir a página de
# viagens recebe uma mensagem em vez de um 500.
FROM python:3.12-slim

# Usuário sem privilégio: o processo só precisa LER o diretório de dados e
# ESCREVER em `recebidos/`, e rodar como root num container que fala com a
# rede não se justifica.
#
# ⚠️ O **gid é fixado** junto com o uid. Sem `groupadd`, o `useradd` escolhe o
# próximo gid livre da imagem base — foi 999 numa construção aqui — e esse
# número muda quando a imagem base muda. Quem precisar casar permissões no
# host estaria mirando num alvo móvel.
RUN groupadd --system --gid 10001 coruja \
    && useradd --system --uid 10001 --gid 10001 --no-create-home coruja

# `--no-cache-dir` para a camada não carregar o cache do pip junto.
RUN pip install --no-cache-dir folium==0.20.0

WORKDIR /app
COPY servidor.py viagem.py pagina.py /app/

USER coruja

ENV CORUJA_DADOS=/dados \
    CORUJA_PORTA=8080 \
    CORUJA_ENDERECO=0.0.0.0 \
    PYTHONUNBUFFERED=1

EXPOSE 8080

# O healthcheck bate na rota de SAÚDE, que não exige token — e falha enquanto
# a base não tiver sido publicada, que é exatamente o estado em que o serviço
# não serve para nada.
#
# ⚠️ Ele batia no `HEAD /radares.versao`. Quando todas as rotas passaram a
# exigir token, essa verificação começou a levar `401` e o container ficou
# `unhealthy` por 973 vezes seguidas — servindo normalmente o tempo todo. Um
# serviço mentindo sobre o próprio estado é pior que um sem healthcheck.
#
# Dar um token ao healthcheck exigiria pôr um segredo na imagem, que vai para
# o registro e para o `docker history`.
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD python3 -c "import urllib.request as u; \
        r=u.urlopen(u.Request('http://127.0.0.1:8080/saude', \
        method='HEAD'), timeout=4); exit(0 if r.status==200 else 1)"

ENTRYPOINT ["python3", "/app/servidor.py"]
