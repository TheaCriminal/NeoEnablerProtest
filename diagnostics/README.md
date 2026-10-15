# Coleta para estudar outros firmwares

Este kit prepara evidências para portar o orçamento de8GiB. Não libera novas
versões automaticamente e não aplica patches no console.

- `neo-diagnostic.elf`: coleta metadados e imprime JSONL no stdout do sender.
- `tools/firmware_report.py`: organiza a coleta e analisa um dump fornecido,
  inteiramente offline, usando somente a biblioteca padrão do Python3.
- `compatibility.json`: tabela de pesquisa. Somente13.20 tem evidência local;
  versões ausentes são consideradas sem suporte ao ajuste de memória. Essa
  tabela não é carregada pelo manager nem altera sua whitelist.

O diagnóstico não usa offsets próprios de firmware nem lê kernel TEXT. Não
precisa do desbloqueio HV para a coleta de metadados; depende de um jailbreak,
loader e versão do SDK que suportem o firmware. Não instala/desativa NEO,
não muda orçamento, não pausa o jogo, não faz alocações nele e não executa
outros payloads. As primitivas do SDK mantêm seu próprio estado de leitura
do kernel; "somente leitura" aqui significa nenhuma alteração de configuração,
código ou memória do jogo pelo diagnóstico.

## Coleta pelo colaborador

1. Anote firmware e modelo, método de jailbreak e, se houver, desbloqueio HV.
2. Compile/envie **somente o diagnóstico**, nunca um patch13.20 adaptado por
   tentativa. Para coletar orçamento de um jogo, deixe-o no menu ou numa cena
   estável. É útil coletar uma vez sem jogo e outra com jogo, e indicar se o
   jogo tem suporte Pro nativo e quais patches estão ativos.
3. Salve o stdout completo. Exemplo, após autorizar o envio ao seu próprio console:

```sh
bash diagnostics/build.sh
/opt/ps5-payload-sdk/bin/prospero-deploy -h IP_DO_PS5 build/neo-diagnostic.elf > diagnostic.jsonl 2> sender.log
```

Não remova linhas do JSONL. Ele deve começar com `session` e terminar com
`end`, contendo `scan_complete:true`. Se o sender misturar logs no stdout,
guarde o original e separe as linhas do diagnóstico num segundo arquivo,
sem reconstruir um footer ou ignorar registros faltantes. O analisador recusa
capturas incompletas, sessões concatenadas e firmware não identificado.

A coleta registra firmware real via `machdep.upd_version` e SDK separadamente,
base DATA informada pelo SDK, PIDs/título, presença dos módulos Gnm e consultas
DMEM/FMEM. Erros aparecem como rc/errno e valor nulo, nunca tamanho zero falso.
`compat_unknown` não é confirmação de modo base ou NEO. É um snapshot sem
pausa e pode mudar durante a coleta. Nenhum dump de kernel é produzido.

## Dump para análise

Para localizar o construtor em outro firmware, precisamos também de um dump
**raw de TEXT**, iniciado exatamente na base de TEXT, obtido separadamente
por ferramenta já validada naquele console após o desbloqueio HV. Este kit
não tenta descobrir offsets, acessar TEXT protegido ou executar um exploit
num firmware desconhecido. Apenas metadados permitem documentar o ambiente,
mas não localizar o patch.

Registre a ferramenta usada, o endereço inicial, comprimento, falhas de
leitura, se o kernel estava original ou modificado e a correspondência com
o mesmo firmware/boot da coleta. Nunca declare um dump válido se páginas
falharam e foram substituídas por zeros. O scanner não detecta todos os erros
de origem do dump e não valida sozinho essa correspondência.

## Organizar um caso offline

Os comandos a seguir não acessam rede ou console:

```sh
python3 tools/firmware_report.py --diagnostic diagnostic.jsonl --out cases/fw-report
python3 tools/firmware_report.py --diagnostic diagnostic.jsonl --kernel-text kernel-text.bin --out cases/fw-with-text
```

`--out` deve ser uma pasta nova; evidências existentes nunca são sobrescritas.
`--text-base 0x...` é opcional e deve vir de uma medição independente. Sem ele,
o scanner trabalha apenas com offsets no arquivo e não inventa uma base por
subtração dos offsets13.20. ELF deve ser extraído antes; não é aceito como raw.

A pasta criada contém `diagnostic.jsonl`, `report.json` e, se fornecido,
`kernel-text.bin`. O relatório registra hashes SHA-256 e tamanho. A análise
procura a aritmética e sequências conhecidas da referência13.20, além de
ocorrências de constantes; relata somente **candidatos**. Compiladores e
firmwares podem usar outras instruções e não encontrar candidato não prova
ausência de solução. Encontrar constante ou sequência também não demonstra
que ela pertence ao orçamento NEO, que reservas são ajustadas ou que a GPU
consegue acessar a região. O relatório sempre mantém
`automatic_patch_allowed:false` e `hardware_validation_required:true`.

Para revisar um caso, comece por `report.json` e pelas informações do ambiente.
Combine a transferência do dump binário separadamente; não é necessário
incluir dumps de kernel num PR público do projeto.

## Critério para adicionar suporte

1. Conferir origem/integridade do dump e firmware real.
2. Desmontar os candidatos e seguir construtor, orçamento, backing, reservas
   vizinhas e verificações de capacidade; não portar somente um getter.
3. Identificar DATA/pmap e pré-condições de acesso naquele firmware.
4. Preparar guards completos, restauração e testes offline do fluxo real.
5. Testar num console daquela versão: orçamento, alocação e acesso à faixa
   adicional, reservas, gameplay e restauração. Distinguir CPU/GPU e processo
   que aloca/mapeia. Confirmar também recusa quando o acesso não está liberado.
6. Só então alterar a whitelist e marcar o firmware validado. Um resultado
   offline nunca promove uma versão para suportada.

## Validação deste kit

Compilação SDK com `-Werror`, testes offline de integridade/scanner e comparação
com o dump13.20 já disponível. O novo diagnóstico ainda não foi enviado ao
console; funcionamento em outros firmwares não foi testado. Para executar
os testes sem console:

```sh
python3 -m unittest discover -s tests -p 'test_firmware_report.py' -v
```

Licença GPL-3.0, como o projeto. Não houve publicação/envio a colaboradores.
