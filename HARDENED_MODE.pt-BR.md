# Modo protegido

[English](HARDENED_MODE.md) · **Português**

Leia tudo isto antes de colocar uma senha, uma frase-senha ou qualquer outro
segredo numa placa DigiMacro.

Uma placa DigiMacro sem senha **não** é um lugar seguro para um segredo. O
núcleo dela aceita firmware novo de qualquer um nos seis segundos depois de
ser conectada — é assim que a página a atualiza — e um firmware que digita a
EEPROM inteira se escreve em segundos: todas as macros junto. O toque longo,
o bloqueio e a verificação dos segredos moram todos no firmware, que é
exatamente o que um firmware desses substitui. O que vem depois de `{secret}`
numa macro é cifrado, com uma chave que a placa não guarda, então esse
firmware o digita embaralhado — mas ele ainda pode esperar a placa ser ligada
no seu computador, provar quem é para o cliente e receber a chave.

**Uma senha fecha essa porta.** Quem a verifica é o núcleo da placa, não o
firmware, e uma atualização precisa dela, haja firmware rodando ou não — e
uma placa que ficou com a atualização pela metade encerra o próprio
destravamento depois de alguns segundos. Mas a senha não faz nada contra quem
abre o chip com um gravador: lendo a flash e a EEPROM diretamente, senha e
tudo. O modo protegido fecha essa porta também. Ele precisa de um gravador
ISP uma vez, é permanente até você decidir apagar tudo, e a placa continua se
atualizando pela página como antes. Este documento diz contra o que ele
protege, contra o que não protege, e como configurá-lo.

Isto vale para a v2. Uma placa v1 — firmware 9 ou anterior, com Micronucleus
— é protegida de outro jeito, perdendo o bootloader: veja *Placas v1* no fim.

## O que cada camada impede

Cada camada abaixo responde a uma pergunta diferente, e nenhuma responde à
das outras. Um segredo só está tão seguro quanto a mais fraca delas em que
você confia.

| camada | impede | não impede |
|---|---|---|
| **o toque longo** — quatro segundos no sensor abrem uma janela de meio minuto | uma página web ou programa em qualquer computador ler ou gravar a tabela | quem estiver com a placa na mão |
| **a senha** | quem está com a placa na mão ler a tabela pelo aplicativo, ou gravar nela um firmware que a lê inteira | ler o chip com um gravador |
| **o bloqueio** | alguém ir tocando os 62 códigos possíveis até acertar | qualquer coisa que não envolva tocar |
| **segredos** — `{secret}` e `{secret!}` | um segredo digitar num computador que não enviou a chave da placa; quem lê a EEPROM — pelo aplicativo, com um gravador ou num laboratório — ler o segredo | a placa, ou uma cópia feita a partir da EEPROM dela, receber a chave de um computador que a envia |
| **bits de trava** | ler a flash ou a EEPROM com um gravador, e mudar os fuses | apagar o chip, o que também limpa os bits de trava |
| **EESAVE deixado não programado** | esse apagamento virar uma porta de entrada: apagar o chip apaga a EEPROM junto | — |
| **detecção de queda de tensão** | uma alimentação fraca corromper a flash que a placa grava ao se atualizar | — |

A senha e as duas seguintes, juntas, são o modo protegido; a última impede
que uma placa que grava a própria flash se danifique. Cada uma sozinha vale
pouco:

- **A senha** impede qualquer coisa pela USB: a página, e firmware novo. Mas
  um gravador lê o chip diretamente, a menos que **os bits de trava** estejam
  gravados.
- **Os bits de trava** impedem um gravador de ler o chip. Mas não conseguem
  impedir o chip de gravar a própria flash — não existe seção de boot
  protegida no ATtiny85 — então não valem nada enquanto qualquer um puder
  gravar firmware na placa pela USB. **A senha tem que estar lá.**
- **Os bits de trava podem ser limpos,** mas só apagando o chip inteiro. Com
  **EESAVE não programado** — o padrão de fábrica, e o de um Digispark — esse
  apagamento leva a EEPROM junto. Um atacante que limpa os bits de trava não
  encontra mais nada para ler.

## O que o modo protegido não impede

Seja honesto consigo mesmo sobre estes pontos antes de confiar nele.

- **Quem tem a senha.** Essa pessoa consegue ler todas as macros pelo
  aplicativo, e consegue gravar na placa um firmware que faça qualquer coisa
  — que digite a EEPROM inteira, ou que sobrescreva o próprio núcleo. Isso
  não acrescenta nada ao que a senha já lhe dá, mas vale saber que a senha é
  tudo.
- **Um laboratório.** O ATtiny85 é um microcontrolador de uso geral, não um
  elemento seguro. Bits de trava de chips como ele já foram derrotados com
  técnicas como glitch de tensão e de clock, e abrindo o encapsulamento
  ("delidding", como se diz). O modo protegido detém alguém com um gravador e
  uma tarde livre. Não detém alguém com equipamento, dinheiro, tempo,
  habilidade e um motivo. Os segredos são a exceção: não estão na placa de
  nenhuma forma que se leia sem a chave. Mas o valor de verificação está, e
  com ele um laboratório consegue fazer uma cópia da placa para a qual os
  seus computadores mandariam a chave. Trate uma placa perdida como uma chave
  perdida: veja *Mudando as coisas depois*.
- **O computador em que ela digita.** Uma macro digita o segredo como
  teclas, então qualquer coisa nesse computador que vê teclas vê o segredo:
  um keylogger, um programa malicioso, um gravador de tela, o campo que
  estiver com o foco. A chave decide *onde* a placa digita um segredo, não o
  que esse computador faz com ele.
- **O seu arquivo de chaves.** Quem consegue conectar a placa num computador
  onde o cliente roda consegue fazer os segredos dela rodarem. Quem consegue
  ler `~/.config/digimacro/host.key` (`%APPDATA%\digimacro\host.key` no
  Windows) consegue isso em qualquer lugar e, com a EEPROM da placa, ou uma
  cópia de segurança que consiga abrir, lê os segredos diretamente. O root
  desse computador também consegue ler a chave no barramento USB.
- **Alguém vendo você tocar.** Código visto é código conhecido. O bloqueio
  limita adivinhar, não saber. Os segredos ajudam aqui: um código visto não
  serve de nada em outro computador, se a macro tiver um.
- **Uma senha fraca.** A placa aceita uma resposta errada por segundo, e o
  aplicativo ainda exige o toque longo, mas uma senha curta acaba caindo
  para alguém paciente com a placa na mão — e, sem firmware na placa, depois
  de uma atualização interrompida, o núcleo pede só a senha. Use os doze
  caracteres inteiros.
- **As suas próprias cópias de segurança.** A cópia do aplicativo é cifrada
  com a senha da placa, então é tão forte quanto essa senha — mas um arquivo
  copiado pode ser atacado fora da placa, sem toque longo e sem a espera de
  um segundo. Uma placa sem senha gera cópias às claras. Os segredos numa
  cópia continuam cifrados com a chave, que nenhuma cópia guarda. Guarde as
  cópias onde você guardaria os próprios segredos, e longe do arquivo de
  chaves.
- **Uma página falsificada ou adulterada.** A página lê todas as macros e
  guarda a senha, então, se alguém conseguir mudar o código que ela roda,
  nada mais vale. A nossa é servida por HTTPS e fizemos a lição de casa, mas
  ela está hospedada num provedor de nuvem barato que não podemos garantir
  que vá ser à prova de invasão para sempre. Tirar o computador da rede não
  basta por si só: uma página carregada enquanto estava online pode guardar
  o que lê e enviar assim que o computador se reconectar. Use uma cópia
  servida do seu próprio clone do repositório
  (`cd web && python3 -m http.server 8000`), num computador que fique
  offline — sem Wi-Fi, sem Bluetooth, sem Ethernet — enquanto a página
  estiver aberta.

## O que custa

- **Um gravador ISP, uma vez,** para os fuses e os bits de trava.
  O [DigiISP](https://digiisp.postcogito.org) transforma um segundo
  Digispark num gravador, programado pelo navegador sem instalar nada. Ele
  lê e grava flash e EEPROM e configura fuses e bits de trava, tudo pela
  página dele, e desenha a ligação para um Digispark como alvo. Qualquer
  outro gravador AVR também serve; as linhas do avrdude estão abaixo.
- **Recomeçar apaga a identidade da placa,** a menos que você restaure uma
  cópia. O número de série fica na EEPROM. Um número novo faz o Chrome pedir
  que você escolha a placa de novo, e os segredos precisam de uma chave
  nova, que cada computador então precisa no seu arquivo de chaves; uma
  cópia restaurada traz os dois de volta, e o arquivo antigo volta a
  servir.

Atualizações não são um custo: uma placa protegida as recebe pela página,
atrás do toque longo e da senha, como qualquer outra placa v2.

## Configurando

A ordem importa. Tudo é configurado enquanto o chip ainda está aberto, e ele
é travado por último, porque travar é o passo que não se desfaz sem perder
tudo.

### 1. Coloque a v2 na placa

Pela página — **Preparar uma placa nova** para um Digispark como vem de
fábrica, **(mudar para a v2…)** para uma placa na v1 — ou com o gravador,
gravando `web/firmware/digimacro-v2-full.hex` como mostra a seção *Com um
gravador* do [README](README.pt-BR.md#com-um-gravador). Conecte, e entre com o toque longo.

### 2. Configure

No aplicativo:

- **uma senha, de doze caracteres.** Sem ela, nada do que vem abaixo vale a
  pena: a placa aceita firmware de qualquer um por seis segundos depois de
  cada reconexão;
- **o bloqueio**;
- **uma chave para os segredos** (**Segredos**, **Criar uma chave**), com
  `{secret!}` antes da parte secreta de cada macro que tenha uma — logo no
  início dela, a menos que ela digite um nome de usuário antes — e o arquivo
  da chave instalado em cada computador de confiança. Guarde uma cópia do
  arquivo num lugar seguro: sem ele, ninguém consegue ler nem mudar os
  segredos de novo.

Ou restaure uma cópia que tenha tudo isso: **Cópia de segurança**, escolha o
arquivo, digite a senha com que ela foi salva e clique em **Restaurar** duas
vezes — o primeiro clique pede confirmação. A placa reinicia sozinha para o
número de série restaurado valer; clique em Conectar e escolha-a de novo.

De um jeito ou de outro, teste cada segredo num computador de confiança e
num que não seja, enquanto o chip ainda está aberto para
corrigir erros.

### 3. Salve uma cópia de segurança

**Cópia de segurança**, **Salvar uma cópia**. Se algo der errado nos
próximos passos, o apagamento pelo gravador é o caminho de volta, e ele leva
a EEPROM junto.

### 4. Confira os fuses

A linha de status agora diz **(não totalmente protegida…)**, em vermelho: a
placa tem senha e não está travada. Clique nela para ver a lista de
verificação, que mostra o que o próprio chip lê:

- uma senha definida;
- os bits de trava — desmarcados até o passo 5;
- EESAVE não programado;
- debugWIRE desligado;
- SELFPRGEN programado, de que a placa precisa para se atualizar;
- detecção de queda de tensão ligada.

Se qualquer item além dos bits de trava estiver desmarcado, configure os
fuses com o gravador. Gravar fuses não apaga nada:

| fuse | valor | por quê |
|---|---|---|
| baixo | `0xE1` | como num Digispark: o clock do PLL, que a placa então ajusta para os 16,5 MHz de que a USB precisa. `0xF1`, com um atraso de partida maior, também funciona |
| alto | `0xDD` | **EESAVE (bit 3) é 1, não programado**, então um apagamento do chip leva a EEPROM junto. Detecção de queda de tensão em 2,7 V, para que uma alimentação fraca resete o chip em vez de deixá-lo gravar a flash errado. O reset continua habilitado, para que um gravador ainda consiga apagar o chip se você recomeçar; desabilitá-lo não ganharia nada, já que os bits de trava já impedem tanto a gravação ISP quanto a de alta tensão de ler o chip — testado e confirmado. debugWIRE continua desligado |
| estendido | `0xFE` | SELFPRGEN programado: o núcleo grava o firmware na própria flash, e sem isso a placa não consegue se atualizar |

Com o avrdude e o DigiISP, ou qualquer USBasp:

```
avrdude -c usbasp-clone -p t85 -U lfuse:w:0xe1:m -U hfuse:w:0xdd:m -U efuse:w:0xfe:m
```

**Se o gravador não consegue falar com o chip de jeito nenhum,** verifique
se a placa está com o pino de reset desabilitado: algumas placas vêm com o
fuse alto em `0x5D` em vez de `0xDD`, o que transforma o P5 num pino comum.
Um gravador como o DigiISP não consegue falar com um chip assim, e ele
precisa antes de um gravador de alta tensão para religar o reset — um XGecu
TL866 ou T48, por exemplo, que o `minipro` controla no Linux e que faz
gravação serial de alta tensão no ATtiny85. A maioria dos Digisparks mantém
o reset habilitado.

Conecte a placa de novo, entre e confira outra vez: só os bits de trava
devem estar desmarcados.

### 5. Trave

Grave o byte de trava **`0xFC`**: LB1 e LB2 os dois programados, modo 3. A
flash e a EEPROM não podem mais ser lidas nem gravadas por um gravador, e os
fuses não podem mais ser mudados. **Não apague o chip neste passo.**

```
avrdude -c usbasp-clone -p t85 -U lock:w:0xfc:m
```

Depois leia a EEPROM de volta com o gravador. Ela não deve mais mostrar os
seus dados.

A linha de status agora deve dizer **(protegida)**. A placa funciona
exatamente como antes: os bits de trava restringem o gravador, não o
programa. O aplicativo continua funcionando, porque fala com a placa, e a
placa continua lendo a própria EEPROM e gravando a própria flash.

### 6. Salve uma nova cópia de segurança

Salve uma agora, da placa do jeito que ela vai ser usada, e guarde-a onde
você guardaria os próprios segredos. Os bits de trava impedem um gravador de
ler a EEPROM, não a placa, então o aplicativo consegue fazer cópia de uma
placa travada também: faça de novo sempre que as macros mudarem.

## Mudando as coisas depois

- **Macros, senha, bloqueio, chave:** pelo aplicativo, como sempre. A
  proteção não muda nada disso.
- **Uma placa perdida ou roubada:** apague a linha dela do arquivo de chaves
  em cada computador. Os segredos dela continuam ilegíveis, e nenhuma cópia
  dela volta a receber a chave.
- **O firmware:** pela página, como em qualquer placa v2 — **(atualizar…)**
  na linha de status, o toque longo e a senha. Os bits de trava ficam; nada
  é apagado.
- **Recomeçar:** salve uma cópia, apague o chip com o gravador — o que limpa
  os bits de trava e a EEPROM juntos — e refaça os passos a partir do 1.
  **Sem uma cópia, as suas macros não voltam.** O apagamento leva a única
  cópia, de propósito.
- **Voltar ao Micronucleus:** **De volta a um Digispark comum**, no fim da
  página, faz isso sem gravador: apaga a EEPROM e põe o Micronucleus de
  volta. Os bits de trava ficam, o que deixa um Digispark que um gravador
  não consegue ler; para limpá-los também, apague o chip e grave com o
  gravador o bootloader original do Micronucleus 2.6 (`t85_default.hex`, que
  o repositório do DigiISP traz em `firmware/bootloader/`), mantendo o fuse
  estendido em `0xFE`. A placa volta a ser um Digispark como veio de
  fábrica.

## Lista de verificação

- [ ] v2 na placa, pela página ou pelo gravador
- [ ] uma senha de doze caracteres, o bloqueio, uma chave instalada e o
      arquivo dela guardado em segurança, e `{secret!}` antes de cada
      segredo — ou uma cópia restaurada que tenha tudo isso
- [ ] segredos testados num computador de confiança e num que não seja
- [ ] uma cópia de segurança salva
- [ ] fuses `0xE1` `0xDD` `0xFE`: a lista de verificação mostra faltando só
      os bits de trava
- [ ] byte de trava `0xFC` gravado, sem apagar
- [ ] EEPROM lida de volta com o gravador, e sem os seus dados
- [ ] a linha de status do aplicativo diz **(protegida)**
- [ ] uma nova cópia de segurança salva, e guardada com os segredos

## Placas v1

Uma placa v1 — firmware 9 ou anterior — mantém o Micronucleus, que aceita um
sketch novo de qualquer um por alguns segundos depois de cada reconexão, com
senha ou sem: a senha da v1 mora no firmware, e o sketch a substitui.
Proteger uma placa v1, portanto, significa remover também o bootloader, o
que impede ainda a mudança para a v2 pela página; a versão deste documento
da v1.0 (`git show v1.0:HARDENED_MODE.md`) explica o processo, e a linha de
status da página continua dizendo **(protegida)** ou **(não totalmente
protegida…)** para essas placas. Mudar a placa para a v2 antes é mais
simples, e mantém as atualizações.
