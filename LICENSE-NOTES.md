# Licenças e proveniência

Este protótipo compila partes do HPLIP para uso local. A fonte HPLIP é baixada de SourceForge e fica em `vendor-source/`; os binários e PPDs gerados ficam em `build/`. Ambos são ignorados pelo Git e não devem ser redistribuídos sem revisão das licenças aplicáveis.

A distribuição HPLIP inclui `COPYING` com as licenças por componente. O filtro `hpcups` e o transporte `hpmud` contêm código HP; consulte também os avisos nos arquivos-fonte usados. Compilar localmente não concede direitos adicionais de redistribuição.

Os arquivos em `shim/` são adaptações locais usadas para compilar o filtro no macOS. Eles não substituem o transporte USB HPLIP.
