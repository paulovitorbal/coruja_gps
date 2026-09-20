# Só a biblioteca padrão, então não há `pip install` nem camada de
# dependências para envelhecer. A imagem é o Python e um arquivo.
FROM python:3.12-slim

# Usuário sem privilégio: o processo só precisa LER o diretório de dados, e
# rodar como root num container que fala com a rede não se justifica.
RUN useradd --system --uid 10001 --no-create-home coruja

WORKDIR /app
COPY servidor.py /app/servidor.py

USER coruja

ENV CORUJA_DADOS=/dados \
    CORUJA_PORTA=8080 \
    CORUJA_ENDERECO=0.0.0.0 \
    PYTHONUNBUFFERED=1

EXPOSE 8080

# O healthcheck bate no HEAD da versão: barato, não baixa os 214 KB, e falha
# enquanto a base não tiver sido publicada — que é exatamente o estado em que
# o serviço não serve para nada.
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD python3 -c "import urllib.request as u; \
        r=u.urlopen(u.Request('http://127.0.0.1:8080/radares.versao', \
        method='HEAD'), timeout=4); exit(0 if r.status==200 else 1)"

ENTRYPOINT ["python3", "/app/servidor.py"]
