# Modo reforçado

[English](HARDENED_MODE.md) · **Português**

Leia tudo isto antes de colocar uma senha, uma frase-senha ou qualquer outra
coisa secreta numa placa DigiMacro.

## Visão geral

Sem uma série de precauções, uma placa DigiMacro **não** é um lugar seguro
para senhas nem para nenhum tipo de material secreto. Só existem 62 códigos
morse possíveis, e bastam alguns minutos para testar todos; e a placa
conversa com qualquer computador: basta conectá-la em outro lugar e abrir a
página de configuração para ver todas as macros dela.

Daqui em diante, vamos usar a palavra "segredos" para senhas, nomes de
usuário ou qualquer outra informação privada que você guarde numa macro para
ser digitada depois; e a palavra "senha" fica reservada para a única senha
que você pode definir na própria placa, na página de configuração, para
protegê-la. São coisas muito diferentes: não confunda uma com a outra.

Se você quer guardar segredos (como credenciais de administrador) numa
macro, eu gostaria de ao menos tentar convencê-lo do contrário: pense bem,
com calma, se precisa mesmo disso. Quase sempre existe um jeito mais seguro e
melhor de fazer a mesma coisa -- a maioria dos sistemas tem os seus próprios
meios de automatizar ações privilegiadas. Estude-os e use-os.

Mas é verdade que há situações em que não existe alternativa melhor e o risco
ou pode ser atenuado, ou é simplesmente aceitável. Depende das suas
circunstâncias, e a decisão é sua. Então, se você quer mesmo fazer isso, eis
o que fazer, nesta ordem:

1. **Defina uma senha na placa.** Uma senha forte: os 12 caracteres
   inteiros, misturando letras maiúsculas e minúsculas, números e pontuação,
   na seção **Senha** da página de configuração. Assim que ela é definida, a
   página passa a avisar que a sua placa não está totalmente reforçada.

2. **Trave o chip,** imediatamente, antes de colocar qualquer segredo na
   placa. Com um gravador ISP (In-System Programming), grave os bits de
   trava do chip. Isso impede que alguém leia a memória da placa, onde os
   segredos vão ficar, com um gravador próprio, o que passaria por cima da
   senha. A página de configuração então diz que a sua placa está reforçada.
   Veja *Gravando os bits de trava*.

   A senha e os bits de trava dependem um do outro: qualquer um dos dois
   sozinho é fácil de contornar. É por isso que toda placa que eu mesmo já
   entreguei a usuários finais saiu com os bits de trava gravados -- para
   que ela fique "totalmente reforçada" assim que o usuário definir a senha
   (dá para fazer em qualquer ordem: 1 e depois 2, ou 2 e depois 1, desde que
   você faça os dois).

3. **Ligue o bloqueio,** faça o que fizer além disso. A placa então para de
   responder quando alguém toca códigos errados demais. Sozinho, ele é a
   forma mais fraca de proteção, já que só há 62 códigos para testar, mas é
   melhor que nada, e funciona junto com a chave. Veja *O bloqueio*.

4. **Proteja os segredos com uma chave,** se você vai conectar a placa em
   apenas uns poucos computadores que você controla, todos com Linux ou
   Windows, e vai precisar dos segredos em momentos previsíveis. Os segredos
   então ficam guardados cifrados, e a placa só os digita nesses
   computadores, o que a torna bem menos útil para quem a roubar. Veja *Os
   segredos e a chave*.

5. **Salve uma cópia de segurança.** Veja *Cópias de segurança*.

As seções a seguir detalham cada passo.

## Gravando os bits de trava

Faça isto logo depois de definir a senha, antes de colocar qualquer segredo
na placa. Se a placa já tem macros, salve uma cópia de segurança antes (veja
*Cópias de segurança*), só por garantia. Travar não apaga nada, e todo o
resto -- as macros inclusive -- continua podendo ser configurado e mudado
pela página de configuração depois.

Se você não tem um gravador ISP mas tem um segundo Digispark, dá para
transformá-lo num com o [DigiISP](https://digiisp.postcogito.org). Se já tem
outro gravador ISP, use-o, claro. Seja qual for o gravador, consulte a
documentação dele para saber como gravar os fuses e os bits de trava do chip.

Primeiro, confira os fuses. Conecte a placa na página de configuração e
clique em **(não totalmente reforçada…)** na linha de status dela: aparece
uma lista de verificação com o que o próprio chip informa. Tudo, menos os
bits de trava, deve estar marcado. Se mais alguma coisa não estiver, grave
estes valores de fuse com o gravador, o que não apaga nada:

* fuse baixo: `0xE1`
* fuse alto: `0xDD`
* fuse estendido: `0xFE`

Depois conecte a placa de novo e confira outra vez: só os bits de trava devem
estar faltando. Acerte isso agora, porque, depois que o chip é travado, os
fuses não podem mais ser mudados.

Então grave o byte de trava: `0xFC`. Garanta que o gravador **não** apague o
chip ao fazer isso: alguns apagam por padrão antes de gravar, e isso
eliminaria tudo o que a placa guarda.

Se não fizermos isso, qualquer um poderia usar um gravador igualzinho ao seu
para ler a memória onde ficam as macros e a senha, já que um gravador passa
por cima da verificação da senha. Com os bits de trava gravados, o que se lê
parece um chip vazio, como sai da fábrica: todo byte vem como `0xFF`.

O chip ainda pode ser reaproveitado -- qualquer um pode apagá-lo e instalar
outros programas nele. Mas apagar o chip inteiro apaga também as macros (o
valor do fuse alto acima garante isso), então os segredos ficam a salvo.

Com os bits de trava gravados, a página de configuração diz que a sua placa
está **(reforçada)**. A placa funciona exatamente como antes, inclusive as
atualizações de firmware.

## O bloqueio

Quando você toca um código morse que não tem macro, a placa pisca o LED
rapidamente três vezes e conta um código errado. O bloqueio, na seção
**Bloqueio** da página de configuração, funciona em dois estágios:

* **bloqueio leve:** depois de um certo número de códigos errados (três, por
  padrão), a placa ignora todo toque por um tempo: um minuto por padrão,
  ajustável até 40.

* **bloqueio forte:** depois de um certo número de bloqueios leves (também
  três por padrão; 0 quer dizer nunca), a placa para de responder a toques,
  inclusive à macro de inicialização, até você dar a senha na página de
  configuração.

Uma senha errada também conta como código errado. Desconectar a placa não
leva o atacante a lugar nenhum: as contagens sobrevivem a isso, e um
bloqueio leve recomeça a espera do zero quando a placa é conectada de novo.

Só a senha zera as contagens. Um código que funciona não zera, senão quem
soubesse um único código poderia continuar chutando para sempre; mas isso
quer dizer que os seus próprios deslizes também se acumulam. Se isso o
incomoda, ponha `{clear}` numa macro: ele zera as contagens como a senha faz.
Tenha em mente que isso praticamente anula o bloqueio: quem descobrir esse
código pode usá-lo para ganhar quantas tentativas quiser nos outros. Dê a
ele um código longo que ninguém veja você tocar, e nunca o coloque na macro
de inicialização. Na melhor das hipóteses, é segurança por obscuridade.

## Os segredos e a chave

Um dispositivo USB não tem como saber em qual computador está conectado, mas
um pequeno programa chamado `digimacro-host` pode dizer à placa que ela está
num dos seus. Ele vem em duas versões, uma para Linux e uma para Windows, na
[página de releases](https://github.com/marcocarnut/DigiMacro/releases).
Copie a versão certa para cada computador em que você pretende conectar o
seu DigiMacro.

Para usá-lo:

* Na seção **Segredos** da página de configuração, clique em **Criar uma
  chave**. Ela vai aparecer na sua pasta de Downloads como
  `digimacro-XXXXXXXX.key`, onde `XXXXXXXX` é o número de série da sua
  placa. Copie-a para cada computador em que você confia, como
  `~/.config/digimacro/host.key` no Linux ou `%APPDATA%\digimacro\host.key`
  no Windows, que é onde o `digimacro-host` a procura. Um mesmo arquivo pode
  guardar as chaves de várias placas, uma por linha.

  Guarde uma cópia do arquivo num lugar seguro. A chave não fica guardada
  nem na placa nem na página, só nesse arquivo: sem ele, ninguém, nem você,
  consegue ler ou mudar os segredos de novo (dá para apagá-los e digitá-los
  outra vez, porém). Da próxima vez que você abrir a página de configuração,
  os segredos vão aparecer embaralhados até você lhe dar o arquivo
  (**Carregar o arquivo da chave**).

* Ponha `{secret!}` no início de cada macro que você quer proteger, e grave
  as macros. Tudo o que vem depois dele fica guardado cifrado, e só é
  digitado se o computador tiver enviado a chave no último minuto (ajustável
  até 40 minutos em **Segredos**). Se não tiver, a placa age como se o
  código nem estivesse na tabela: três piscadas, e um código errado para o
  bloqueio. Para uma macro que digita um nome de usuário e depois um
  segredo, ponha `{secret}` entre os dois: o nome de usuário é digitado em
  qualquer lugar, e o resto só onde a chave foi enviada.

* Sempre que quiser que um computador receba os segredos, rode o
  `digimacro-host` nele. `digimacro-host -e 30` reenvia a chave a cada 30
  segundos enquanto estiver rodando: inicie-o quando entrar na sua sessão, e
  os segredos funcionam enquanto você estiver logado. No Linux, ele precisa
  da mesma regra do udev que a página de configuração; o
  [README](README.pt-BR.md#segredos) tem os detalhes.

Tenha em mente que a chave protege contra a placa revelar os segredos a um
computador diferente do pretendido, mas não a outros programas ou campos de
formulário no mesmo computador.

## Cópias de segurança

A página de configuração pode salvar as suas macros num único arquivo e
restaurá-las, junto com a senha, as configurações do bloqueio e o número de
série da placa. Se a sua placa tem senha, o arquivo é cifrado com ela e a
pede na restauração, então a cópia é tão segura quanto a sua senha (você
escolheu uma senha forte, difícil de adivinhar, não escolheu?). Se a sua
placa não tem senha, a cópia não é cifrada de jeito nenhum. Em qualquer
caso, os segredos dentro dela continuam cifrados com a chave, que nenhuma
cópia guarda.

Como uma cópia leva junto o número de série, restaurá-la numa placa
diferente cria um clone que os seus computadores não conseguem distinguir do
original, e para o qual mandariam a chave. Guarde as cópias onde você
guardaria os próprios segredos, e longe do arquivo da chave.

## Ambiente

A página de configuração tem acesso a tudo o que está na placa. A versão
oficial publicada nunca envia nada para a internet, mas você não tem como ter
certeza de que a cópia que carregou é a oficial, e tirar o computador da rede
não basta por si só: uma cópia falsa poderia guardar o que lê e enviar no
momento em que o computador voltasse a ficar online. O único jeito de ter
certeza é servir a página você mesmo, a partir da sua própria cópia do
repositório, num computador que fique offline -- sem Wi-Fi, Ethernet nem
Bluetooth -- o tempo todo; veja *Uma página falsificada ou adulterada*
abaixo. A maioria das pessoas não se dá a esse trabalho. Ainda assim, nós
recomendamos.

Tome também os cuidados de sempre: garanta que você não está sendo filmado
ou observado de alguma outra forma, e que não há malware, keylogger ou
gravador de tela rodando no seu computador.

## Detalhes técnicos

O material abaixo é um olhar mais técnico e aprofundado sobre o que já foi
visto acima, para nerds e o pessoal de cibersegurança.

### O que cada camada impede

Cada camada abaixo responde a uma pergunta diferente, e nenhuma responde à
das outras. Um segredo só está tão seguro quanto a mais fraca delas em que
você confia.

| camada | impede | não impede |
|---|---|---|
| **o toque longo** — quatro segundos no sensor abrem uma janela de meio minuto | uma página web ou programa em qualquer computador ler ou gravar a tabela | quem estiver com a placa na mão |
| **a senha** | quem está com a placa na mão ler a tabela pelo aplicativo, ou gravar nela um firmware que a lê inteira | ler o chip com um gravador |
| **o bloqueio** | alguém ir tocando os 62 códigos possíveis até acertar | qualquer coisa que não envolva tocar |
| **segredos** — `{secret}` e `{secret!}` | um segredo ser digitado num computador que não enviou a chave da placa; quem lê a EEPROM — pelo aplicativo, com um gravador ou num laboratório — ler o segredo | a placa, ou uma cópia feita a partir da EEPROM dela, receber a chave de um computador que a envia |
| **bits de trava** | ler a flash ou a EEPROM com um gravador, e mudar os fuses | apagar o chip, o que também limpa os bits de trava |
| **EESAVE deixado não programado** | esse apagamento virar uma porta de entrada: apagar o chip apaga a EEPROM junto | — |
| **detecção de queda de tensão** | uma alimentação fraca corromper a flash que a placa grava ao se atualizar | — |

A senha e as duas seguintes, juntas, são o modo reforçado; a última impede
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

### O que o modo reforçado não impede

Seja honesto consigo mesmo sobre estes pontos antes de confiar nele.

- **Quem tem a senha.** Essa pessoa consegue ler todas as macros pelo
  aplicativo, e consegue gravar na placa um firmware que faça qualquer coisa
  — que digite a EEPROM inteira, ou que sobrescreva o próprio núcleo. Isso
  não acrescenta nada ao que a senha já lhe dá, mas vale saber que a senha é
  tudo.
- **Um laboratório.** O ATtiny85 é um microcontrolador de uso geral, não um
  elemento seguro. Bits de trava de chips como ele já foram derrotados com
  técnicas como glitch de tensão e de clock, e abrindo o encapsulamento
  ("delidding", como se diz). O modo reforçado detém alguém com um gravador e
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
- **O seu arquivo da chave.** Quem consegue conectar a placa num computador
  onde o cliente roda consegue fazer os segredos dela serem digitados. Quem
  consegue ler `~/.config/digimacro/host.key` (`%APPDATA%\digimacro\host.key`
  no Windows) consegue isso em qualquer lugar e, com a EEPROM da placa, ou
  uma cópia de segurança que consiga abrir, lê os segredos diretamente. O
  root desse computador também consegue ler a chave no barramento USB.
- **Alguém vendo você tocar.** Código visto é código conhecido. O bloqueio
  limita adivinhar, não saber — embora, desde o firmware 17, tocar um código
  conhecido não zere mais as contagens, e assim um código não compra mais
  tentativas nos outros. Os segredos ajudam aqui: um código visto não serve
  de nada em outro computador, se a macro tiver um.
- **Uma senha fraca.** A placa aceita uma resposta errada por segundo, e o
  aplicativo ainda exige o toque longo, mas uma senha curta acaba caindo
  para alguém paciente com a placa na mão — e, sem firmware na placa, depois
  de uma atualização interrompida, o núcleo pede só a senha. Use os doze
  caracteres inteiros.
- **As suas próprias cópias de segurança.** A cópia do aplicativo é cifrada
  com a senha da placa, então é tão forte quanto essa senha — mas um arquivo
  copiado pode ser atacado offline, sem toque longo e sem a espera de
  um segundo. Uma placa sem senha gera cópias às claras. Os segredos numa
  cópia continuam cifrados com a chave, que nenhuma cópia guarda. Guarde as
  cópias onde você guardaria os próprios segredos, e longe do arquivo da
  chave.
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

### O que custa

- **Um gravador ISP, uma vez,** para os fuses e os bits de trava.
  O [DigiISP](https://digiisp.postcogito.org) transforma um segundo
  Digispark num gravador, programado pelo navegador sem instalar nada. Ele
  lê e grava flash e EEPROM e configura fuses e bits de trava, tudo pela
  página dele, e desenha a ligação para um Digispark como alvo. Qualquer
  outro gravador AVR também serve; as linhas do avrdude estão abaixo.
- **Recomeçar apaga a identidade da placa,** a menos que você restaure uma
  cópia. O número de série fica na EEPROM. Um número novo faz o Chrome pedir
  que você escolha a placa de novo, e os segredos precisam de uma chave
  nova, que cada computador então precisa ter no seu arquivo da chave; uma
  cópia restaurada traz os dois de volta, e o arquivo antigo volta a
  servir.

### Configurando

A ordem importa. A senha e os bits de trava vêm primeiro, antes de qualquer
segredo, para que nenhum segredo jamais fique num chip que possa ser lido:
um gravador passa por cima da senha, e sem senha qualquer um pode gravar na
placa um firmware que a lê inteira. Travar não apaga nada nem muda nada do
que a página consegue fazer, então todo o resto é configurado depois, pela
página, como sempre.

Se a placa já tem macros, salve uma cópia de segurança antes de começar
(**Cópia de segurança**, **Salvar uma cópia**): um deslize com o gravador
pode apagar o chip, e o apagamento leva a EEPROM junto.

#### 1. Instale o firmware

Pela página — **Preparar uma placa nova** para um Digispark como vem de
fábrica, com o bootloader Micronucleus. Ela instala primeiro o núcleo,
depois o aplicativo principal. Conecte, e entre com o toque longo.

#### 2. Defina uma senha

**Senha**, doze caracteres. Sem ela, nada do que vem abaixo vale a pena: a
placa aceita firmware de qualquer um por seis segundos depois de cada
reconexão.

#### 3. Confira os fuses

A linha de status agora diz **(não totalmente reforçada…)**, em vermelho: a
placa tem senha e não está travada. Clique nela para ver a lista de
verificação, que mostra o que o próprio chip lê:

- uma senha definida;
- os bits de trava — desmarcados até o passo 4;
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

#### 4. Trave

Grave o byte de trava **`0xFC`**: LB1 e LB2 os dois programados, modo 3. A
flash e a EEPROM não podem mais ser lidas nem gravadas por um gravador, e os
fuses não podem mais ser mudados. **Não apague o chip neste passo.**

```
avrdude -c usbasp-clone -p t85 -U lock:w:0xfc:m
```

Depois leia a EEPROM de volta com o gravador. Ela deve vir toda como
`0xFF`.

A linha de status agora deve dizer **(reforçada)**. A placa funciona
exatamente como antes: os bits de trava restringem o gravador, não o
programa. O aplicativo continua funcionando, porque fala com a placa, e a
placa continua lendo a própria EEPROM e gravando a própria flash.

#### 5. Configure o resto

No aplicativo:

- **o bloqueio**;
- **uma chave para os segredos** (**Segredos**, **Criar uma chave**), com
  `{secret!}` antes da parte secreta de cada macro que tenha uma — logo no
  início dela, a menos que ela digite um nome de usuário antes — e o arquivo
  da chave instalado em cada computador de confiança. Guarde uma cópia do
  arquivo num lugar seguro: sem ele, ninguém consegue ler nem mudar os
  segredos de novo.

Ou restaure uma cópia que tenha tudo isso: **Cópia de segurança**, escolha o
arquivo, digite a senha com que ela foi salva e clique em **Restaurar** duas
vezes — o primeiro clique pede confirmação. A placa assume a senha da cópia
junto com todo o resto, e reinicia sozinha para o número de série restaurado
valer; clique em Conectar e escolha-a de novo.

De um jeito ou de outro, teste cada segredo num computador de confiança e
num que não seja.

#### 6. Salve uma cópia de segurança

**Cópia de segurança**, **Salvar uma cópia**, e guarde-a onde você guardaria
os próprios segredos. Os bits de trava impedem um gravador de ler a EEPROM,
não a placa, então o aplicativo faz cópia de uma placa travada como de
qualquer outra: faça de novo sempre que as macros mudarem.

### Mudando as coisas depois

- **Macros, senha, bloqueio, chave:** pelo aplicativo, como sempre. O
  reforço não muda nada disso.
- **Uma placa perdida ou roubada:** apague a linha dela do arquivo da chave
  em cada computador. Os segredos dela continuam ilegíveis, e nenhuma cópia
  dela volta a receber a chave.
- **O firmware:** pela página — **(atualizar…)** na linha de status, o
  toque longo e a senha. Os bits de trava ficam; nada é apagado.
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

### Lista de verificação

- [ ] núcleo e aplicativo, pela página ou pelo gravador
- [ ] uma senha de doze caracteres
- [ ] fuses `0xE1` `0xDD` `0xFE`: a lista de verificação mostra faltando só
      os bits de trava
- [ ] byte de trava `0xFC` gravado, sem apagar
- [ ] EEPROM lida de volta com o gravador: toda `0xFF`
- [ ] a linha de status do aplicativo diz **(reforçada)**
- [ ] o bloqueio, uma chave instalada e o arquivo dela guardado em
      segurança, e `{secret!}` antes de cada segredo — ou uma cópia
      restaurada que tenha tudo isso
- [ ] segredos testados num computador de confiança e num que não seja
- [ ] uma cópia de segurança salva, e guardada com os segredos
