# ADR 0008 — Cartão em FAT32 com MBR

**Data:** 2026-09-20
**Status:** aceito

## Contexto

O cartão é o meio por onde a configuração entra no aparelho e por onde a base
de radares sai e entra. Ele é escrito **por uma pessoa, num computador
qualquer** — o autor usa macOS, mas nada garante que seja sempre assim — e
lido por um firmware embarcado sem sistema operacional.

Isso dá dois requisitos que puxam para o mesmo lado:

* O formato precisa ser gravável em Windows, Linux e macOS **sem driver,
  sem software extra e sem opção de montagem especial**. Um formato que exige
  instalar algo transforma "copie o arquivo para o cartão" num procedimento.
* O formato precisa ser legível pela biblioteca de FAT do firmware, nas
  opções de compilação que o projeto escolheu (ADR 0007).

O RNF03 já exigia FAT32. Este ADR registra **por quê**, porque a razão não
estava escrita em lugar nenhum e a pergunta voltou.

## Decisão

**FAT32, em tabela de partição MBR.**

* Rótulo do volume: `CORUJA`. Não é estético — durante os testes de bancada
  circularam cartões chamados `BOOT` e `EASYROMS`, e montar o errado sem
  perceber é fácil.
* **FAT16 é aceitável** em cartões pequenos. Alguns formatadores escolhem
  FAT16 sozinhos abaixo de ~4 GB; o FatFs lê os dois e não vale brigar com a
  ferramenta.
* Partição única é o preferido, mas **não é exigência**: a sondagem do ADR
  0007 procura o arquivo em até quatro partições primárias.

## Consequências

* Qualquer um dos três sistemas formata e escreve sem preparo nenhum.
* O limite de 4 GB por arquivo do FAT32 é irrelevante aqui: o `radares.bin`
  tem 214 KB, com teto de projeto em ~470 KB.
* **GPT fica fora.** O FatFs só entende GPT com `FF_LBA64` e exFAT
  habilitados, e nenhum dos dois está. Num cartão pequeno o padrão já é MBR
  nos três sistemas, então na prática nem aparece como escolha — mas num
  cartão grande reformatado, pode aparecer.

## Alternativas consideradas

| Formato | Por que não |
| :--- | :--- |
| **exFAT** | Os três sistemas leem, mas o firmware não: a biblioteca vem com `FF_FS_EXFAT = 0`. Habilitar custa flash e contraria o RNF03. Foi encontrado na prática — um cartão de teste tinha a partição grande em exFAT, e o firmware montava as FAT e não via nada nela. |
| **NTFS** | O macOS monta **somente leitura** por padrão. Quebra o requisito de escrita. |
| **ext4** | Windows e macOS não leem sem software de terceiros. |

## Nota sobre o que isto **não** decide

Este ADR fala do formato, não da **qualidade** do cartão. Contato ruim e
mídia ruim produzem falhas intermitentes que se disfarçam de defeito de
firmware — ver R-43, onde um contato oscilante do `DET` deu duas leituras
contraditórias na mesma inicialização e quase foi explicado por software.
