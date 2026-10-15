# Formatos de evidência

`neo-diagnostic-v1` usa um objeto JSON por linha (UTF-8), nesta ordem:

| record | Campos principais |
| --- | --- |
| `session` | `firmware_raw` stringhex32, `firmware_rc/errno/bytes`, `sdk_raw`, `kernel_data_base`, `scope:metadata_only`, `patches:false`, `snapshot_atomic:false` |
| `process` | `pid`, `kind:native_payload/game`, `title_id`, `app_info_rc/errno`, `neo_driver_rc`, `base_driver_rc`, `compat_driver_rc`, `mode` |
| `query` | `pid`, `node`, `rc`, `errno`, `returned_bytes`, `value_hex` stringhex64 ou null |
| `end` | `records_before_end`, `scan_complete`, `game_count`, `scan_rc`, `scan_errno` |

Todos os objetos têm `format:neo-diagnostic-v1`. O footer conta todos os
registros anteriores, incluindo a sessão e excluindo ele mesmo. Falhas de
consulta individuais não invalidam a captura se forem registradas; falha
na identificação do firmware ou na enumeração completa impede criar um caso.
As consultas atuais são `kern.app.dmem_size_pid`, `kern.app.fmem_size_pid` e
`kern.app.fmem_avail_pid`. Não são medidas de uso físico/GPU nem prova de
alocação. Uma consulta bem-sucedida com tamanho inesperado tem valor nulo.

`neo-firmware-case-v1` é o `report.json` produzido offline. Contém firmware,
hash e registros da coleta; dump opcional com hash/tamanho/base fornecida;
ocorrências e candidatos; perfil de referência quando houver; limitações;
`assessment:unvalidated_case`, `automatic_patch_allowed:false` e
`hardware_validation_required:true`. Uma coincidência com o hash de referência
é registrada separadamente; não valida o estado atual do console.

`neo-firmware-registry-v1` é a tabela de pesquisa estática. Versões ausentes
têm estado padrão `unsupported`. Nenhuma ferramenta deste kit altera essa
tabela automaticamente ou gera offsets para o payload em execução.
