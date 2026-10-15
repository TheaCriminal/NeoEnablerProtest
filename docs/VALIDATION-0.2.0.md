# Validação local da versão0.2.0 — 2026-10-15

Estado: construída localmente, ainda não enviada ao PS5; sem commit/tag/push
ou release remota. Arquivos em `build/neo-manager.elf` e
`build/neo-manager-0.2.0.elf`, com checksums próprios e notas de release.

SHA-256 de ambos:
`1250942defff7e46e70068a60582c1183fc98b254071d0d113ae79e0bbdfb598`.

Resultados:

- SDK prospero-clang, `-O2 -Wall -Wextra -Werror`: passou.
- Testes host `tests/budget_test.c`: passaram. Não acessam console.
- Verificador x86 do experimento de memória, usando a captura real de TEXT13.20:
  original5,25GiB, passo5,5GiB e8GiB chegam à continuação original.
- Versão no manager/CHANGELOG e extração de notas de release: passaram.
- Módulo de orçamento não contém cookie ou offset de marcador Byepervisor.
- Checksums dos dois artefatos salvos conferidos; diff sem erros de whitespace.
- CI configurado para executar os testes host; não foi disparado remotamente.

Os testes cobrem páginas4KiB/2MiB/1GiB, permissões presentes/RW/XOTEXT,
falhas de leitura sem acessar instruções quando as permissões recusam,
firmware/layout desconhecido, ausência de jogos/áreas ativas, mudança de
estado entre leituras, falhas de escrita/readback, rollback e bloqueio após
rollback não confirmado. Também cobrem aplicação automática após acesso
ficar disponível, espera por fechamento do jogo, desativação manual sem
reativação e habilitação manual posterior.

A seleção8GiB anterior foi medida no Driveclub e o usuário confirmou gameplay
normal com patch que usa memória. Isso não valida o detector novo: não houve
teste real da0.2.0 com HV protegido, desbloqueado ou com métodos diferentes.
Guest PTEs são pré-condição, não atestado do estado global de NPT/GMET/HV.
O mecanismo e os limites estão em `TECHNICAL.md`.

Próximo envio exige autorização explícita. Encerrar o painel experimental8766
pelo Quit antes de usar0.2.0; Quit mantém os patches. A0.2.0 usa8765 e preserva
o comportamento de ativar NEO e tentar8GiB ao iniciar. Alterações efetivas do
orçamento aguardam todos os jogos fechados.
