# HP Deskjet 1000 J110 para Apple Silicon

Protótipo local para reconstruir o suporte da Deskjet 1000 J110 em Macs Apple Silicon (M1–M4), conectada por USB.

## Estado atual

- `build.sh` compila o filtro CUPS `hpcups` do HPLIP 3.23.12 para `arm64` e extrai o PPD exato `Deskjet 1000 J110 series`.
- A compilação local do filtro foi concluída; `file` e `lipo` confirmaram ARM64.
- O transporte HPLIP identifica a impressora conectada como Deskjet 1000 J110 (USB VID `03f0`, PID `8811`).
- `src/j110-status.c` lê o status USB básico; a consulta retornou `0x10`.
- `src/j110-supplies.c` faz um GET no canal LEDM e converte os registros XML de consumíveis em tipo, rótulo, estado e nível restante. A versão ARM64 compilada foi executada na impressora e recebeu HTTP 200.
- No macOS Tahoe 26.6.2 (25G83), foi criada a fila CUPS `HP_Deskjet_1000_J110_ARM` usando o URI USB da J110 e o filtro ARM64.
- A primeira impressão de teste A4 saiu como uma faixa preta horizontal de aproximadamente 20 cm por 0,4 cm. Depois da mudança para raster K e PCL preto dedicado, a impressão ainda produziu uma faixa no topo.
- A comparação do raster e do PCL9 revelou que `build.sh` não passava `APDK_LITTLE_ENDIAN`, flag exigida pelo Makefile do HPLIP. O build agora passa a flag; após instalar essa compilação, a impressão passou a mostrar o texto, mas ele ficou horizontalmente esticado. A faixa cinza ao fundo na foto veio da reutilização do papel, segundo o usuário.
- FastDraft 300×300 dpi também foi impresso e continuou esticado. A comparação com o perfil HP legado confirma que o modo Normal usa renderer 600×300 dpi e saída 600×600 dpi; FastDraft usa 300×300 dpi. Como FastDraft não corrigiu a deformação física, a resolução do raster CUPS, sozinha, não explica o problema. O raster produzido offline tem o texto em cerca de 2,99×0,18 pol.; a etapa PCL3GUI ou a interpretação da impressora ainda pode estar alterando a geometria.
- O último teste foi em A4 usando o PDF de teste em tamanho Letter. O texto continuou esticado e ganhou margem superior; isso é compatível com o reposicionamento vertical ao ajustar uma página Letter para A4 e não corrige a deformação.
- A inspeção offline do raster CUPS K em A4/FastDraft mostrou 2406×3318 pixels a 300×300 dpi, 8 bits por pixel e 2406 bytes por linha. Portanto, `cupsRowStep=2` não duplica os bytes da linha nesse raster. O cabeçalho PCL também anuncia 300 dpi; a escala incorreta ainda não tem causa confirmada.
- Foi enviado um teste PCL bruto com a estrutura K+RGB de duas componentes do XML HP antigo, preservando o canal preto e acrescentando uma linha RGB vazia por linha K. O trabalho 139 completou uma folha, mas o usuário confirmou que o texto saiu esticado da mesma forma; essa diferença de formato não é suficiente para corrigir a escala.
- A causa encontrada no caminho K era a conversão de formato: o raster CUPS fornece 8 bits por pixel, mas `Pcl3Gui2` criava `Mode9` com a opção padrão que presume pixels já compactados em 1 bit. Isso fazia o compressor usar os primeiros 1/8 dos pixels como se fossem a linha inteira. O caminho K agora ativa a conversão 8→1 bit do próprio `Mode9`, no mesmo modo que o HPLIP já usa em `Pcl3::Configure`; o PCL offline cresceu de 4.224 para 6.257 bytes e mantém a largura raster de 2.406 pixels a 300 dpi.
- O teste PCL corrigido foi enviado nos trabalhos 140 e 141; no trabalho 141, o usuário confirmou que o texto saiu perfeito.
- A fila instalada foi atualizada com o filtro ARM64 corrigido. A comparação do binário em `/Library/Printers/HP/Deskjet1000J110ARM/hpcups` com `build/hpcups` confirmou que são idênticos; a fila USB está ativa e o trabalho 142, enviado pelo fluxo CUPS normal em A4/KGray/FastDraft, concluiu uma folha.
- A fila expõe tamanhos de papel, origem automática/manual, papel comum/fotográfico, qualidade FastDraft/Normal/Best/Photo e modos CMYGray, KGray e RGB.
- Na consulta observada, o dispositivo informou o cartucho CMY como ausente. Para K, reportou `refilledBlack`, nível bruto `0%` e estado de quantidade `unknown`; a ferramenta mostra `refilledBlack`, mas marca o nível como desconhecido. Isso não é tratado como cartucho vazio nem como bloqueio de impressão.
- `build-status.sh` compila os utilitários ARM64 sem instalar nada.
- `src/j110-align.c` implementa o fluxo LEDM manual da J110, mas ainda não foi exercitado em uma calibração física. Ele consulta o modo e o estado, exige a confirmação `ALIGN J110` antes de solicitar a página, valida as faixas de A–J e envia os valores escolhidos somente depois da leitura da página.
- A inspeção estática do DMG antigo `HP_Mountain_Lion_Ink_SW_v12.23.17.dmg` confirmou que o instalador da família J110 inclui o HP Utility 5.8.0 (universal, incluindo x86_64) e seu plugin `ColorCleaning.task`, chamado “Clean Printheads”. O plugin genérico contém exemplos PCL/PML, código para fluxos DMF/LDL/PCL/PML e páginas de verificação. A configuração do modelo é mais específica: “Deskjet 1000 J110 series” seleciona três níveis via DMF, além da página interna `com.hp.dmf.InternalPages.usagePage`. A análise estática do código do plugin mostra que ele cria `ADMInternalPagesManagement` com o serviço resolvido, escolhe um identificador interno distinto por nível e chama `startPrintPage:withTimeout:`; o caminho não monta diretamente os payloads PCL/PML genéricos.
- A inspeção acima mostrou que a limpeza da J110 no Mac antigo era delegada ao DMF (Device Model Framework), que não está implementado neste protótipo. `src/j110-clean.c` e `build/status/j110-clean` agora recusam a operação e não enviam bytes à impressora. O utilitário anterior montava uma sequência PCL/PML genérica sem evidência de que fosse a rota da J110; ela foi removida. Ainda é necessário entender a chamada DMF, os limites de nível e o estado do job antes de reimplementar a limpeza.
- A análise estática de `HPDeviceModel.framework` confirmou que o `ADMInternalPagesManager` escolhe um transporte para a página interna e acompanha o estado até conclusão, falha ou timeout. Para imprimir por PML, o transporte consulta no catálogo do modelo o OID e o valor associados à página e envia uma requisição PML; portanto, os dados de operação não estão no plugin `ColorCleaning.task`. Os arquivos do catálogo DMF do pacote estão cifrados. O framework deriva os caminhos de família/arquivo cifrando os nomes em minúsculas com 3DES; separadamente, a rotina de leitura detecta tabelas cifradas e as descriptografa com o identificador de cifra 0 antes de interpretá-las como XML ou plist. Isso explica como o framework localiza e seleciona o parser, mas ainda não recupera a entrada J110 nem prova que PML é o transporte escolhido para ela.
- Foi possível reverter estaticamente a cifra 3DES dos nomes de arquivo usando a chave embutida no framework: o blob de 193 bytes correspondente a `deskjet 1000 j110 series` foi localizado no catálogo. Isso elimina a incerteza sobre qual arquivo contém a tabela do modelo. A cifra do conteúdo ainda não foi reproduzida; tentativas com AES/3DES e IVs candidatos não produziram XML/plist válido. Um decodificador temporário que reproduz as chamadas CSSM do framework conseguiu inicializar CSSM no Tahoe, mas `CSSM_ModuleLoad` para Apple CSP retornou `0xffffffce` e o anexo também falhou. Portanto, ainda não extraímos OID, valor PML ou configuração de transporte desse blob.
- O binário `Configure` contém os IDs internos `cleaningPage`, `cleaningPageLevel2`, `cleaningPageLevel3` e `cleaningVerificationPage`. O plugin Mac seleciona um ID conforme o nível e chama `startPrintPage`; a configuração J110 declara três níveis. O Toolbox Windows, por sua vez, anuncia máximo de nível, inicia limpeza e acompanha o job. Essa correspondência funcional não revela se Windows e Mac usam o mesmo transporte ou os mesmos valores. Nenhum comando de limpeza foi enviado à impressora.
- A análise estática do pacote Windows `Full_Webpack-1325-DJ1000_J110_Full_Webpack.exe` confirmou componentes específicos da J110: o INF x64 aponta para `HP Deskjet 1000 J110 series` e o pacote inclui o Toolbox x64. Nomes de métodos e recursos do Toolbox mostram que a limpeza consulta o nível máximo anunciado pelo dispositivo, inicia a operação, acompanha o estado e pode imprimir páginas de verificação; a interface contém fluxos de limpeza de níveis 1–3. O alinhamento manual usa opções e parâmetros LEDM, coerentes com `/Calibration/State`, `/Calibration/Session` e `ManualSelectedPatterns`. Esta foi uma análise estática do pacote, sem executar ou instalar binários Windows; os nomes e recursos não confirmam por si só o formato exato do comando aceito pela J110.
- Consultas LEDM de leitura retornaram HTTP 200 para `/Calibration/State` e `/DevMgmt/ConsumableConfigDyn.xml`. O estado de calibração atual é `CalibrationRequired` e o XML declara `AlignmentMode=manual`, confirmando a disponibilidade do fluxo de alinhamento manual. Nenhuma sessão de calibração foi iniciada nem nenhum valor alterado.

## Build local

Requisitos: macOS Apple Silicon com Xcode Command Line Tools, `brew`, `jpeg-turbo` e `libusb` já disponíveis. Os scripts não instalam nem atualizam dependências.

```sh
HPLIP_SRC="$PWD/vendor-source/hplip-3.23.12" ./build.sh
HPLIP_SRC="$PWD/vendor-source/hplip-3.23.12" ./build-status.sh
```

Saídas em `build/`:

- `hpcups`, filtro CUPS ARM64.
- `ppd/hp-deskjet_1000_j110_series.ppd`, PPD extraído do HPLIP.
- `libjpeg.8.dylib`, biblioteca JPEG usada pelo filtro.
- `status/j110-status`, leitor de status USB básico.
- `status/j110-supplies`, leitor LEDM de consumíveis.
- `status/j110-clean`, aviso ARM64 que recusa enviar comandos até a implementação do fluxo DMF específico da J110.
- `status/j110-align`, alinhamento manual LEDM; solicita uma página e altera a calibração apenas após a confirmação e a entrada das escolhas A–J.

Execute `build/status/j110-status` ou `build/status/j110-supplies` com a impressora conectada. Ambos fazem somente consultas de leitura e não exibem o número de série. `build/status/j110-clean` está desativado e não envia comando. `build/status/j110-align` inicia alinhamento manual somente depois de ler o estado `CalibrationRequired`, confirmar o modo `manual` e digitar `ALIGN J110`; depois da página, solicita os valores dos padrões A–J. Não execute um segundo alinhamento se o primeiro ficar pendente.

`build.sh` baixa HPLIP 3.23.12 do SourceForge se `HPLIP_SRC` não for definido. Nenhum script instala ou configura filas CUPS.

## Próximas etapas

1. Validar o caminho normal do CUPS em outros modos de cor e qualidades; o caminho KGray/FastDraft já imprimiu corretamente.
2. Reproduzir fielmente o decifrador DMF para o blob já identificado como `deskjet 1000 j110 series`; depois identificar o transporte e os dados de página e correlacioná-los com `StartCleanPens`/`GetJobState` do Toolbox Windows. Só então implementar nível, acompanhamento e página de verificação e validar em sessões físicas autorizadas.
3. Revisar o empacotamento de instalação/desinstalação reversíveis.

Operações que consomem tinta ou alteram calibração devem sempre ser iniciadas de forma explícita pelo usuário.

## Licenças e proveniência

O código HPLIP baixado e os componentes compilados seguem os avisos e licenças correspondentes no HPLIP, incluindo `COPYING`. Fonte baixada e binários locais ficam em `vendor-source/` e `build/`, ignorados pelo Git. Revise as licenças por componente antes de redistribuir qualquer fonte ou binário HP. Consulte `LICENSE-NOTES.md`.

## Instalar a fila USB no macOS

Após compilar o filtro, execute:

```sh
sudo ./install.sh
```

O instalador procura uma URI USB da Deskjet 1000 J110, mostra as alterações e exige que você digite `INSTALL J110`. Se a descoberta falhar, passe a URI da impressora explicitamente com `--device`. Ele instala o filtro e o PPD em `/Library/Printers/HP/Deskjet1000J110ARM`, cria a fila `HP_Deskjet_1000_J110_ARM` e mantém a impressora padrão atual. A instalação não envia trabalho de impressão.

O teste físico do PCL de impressão corrigido saiu perfeito, confirmado pelo usuário. A fila normal CUPS foi atualizada e o trabalho 142 completou uma folha em A4/KGray/FastDraft. O PDF de teste está em Letter, então ao usar A4 pode haver uma margem superior por reposicionamento do conteúdo. A limpeza foi desativada após a descoberta de que o perfil J110 usa DMF; ainda não foi enviada uma implementação desse fluxo.

Para remover a instalação, rode `sudo ./uninstall.sh`; o script também exige confirmação e remove apenas a fila dedicada e os arquivos que marcou como seus.
