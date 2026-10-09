# DigiMacro

[English](README.md) · **Português**

Um teclado de macros acionado por código morse, num Digispark ATtiny85,
configurado por uma página web.

Aplicativo web de configuração: https://digimacro.postcogito.org

Para o PC ou o celular, ele se apresenta como um teclado USB. Tocando um
sensor em código morse, ele digita uma de 62 sequências de teclas possíveis,
que você programa antes pela página web que o acompanha (precisa do Chrome ou
do Edge num computador: não do Safari, e não num celular). Ele também pode ser acionado por um controle remoto infravermelho,
se você acrescentar um sensor de infravermelho (TSOP4838 ou similar).

Ele também pode digitar sozinho ao ser ligado (esse foi, aliás, o uso
original da primeiríssima versão, muitos anos atrás: fazer uma TV LG abrir o
navegador embutido automaticamente ao ser ligada.)

<p align="center">
  <img src="docs/digimacro.jpg" width="420"
       alt="Um Digispark com um fio nu soldado no P0 e um receptor de IR ligado ao P2">
</p>

*É só isso: um Digispark, um fio nu no P0 dobrado pela borda da placa como
sensor de toque e — opcional — um receptor de IR TSOP4838 com a saída ligada
ao P2.*

> ### ⚠️ Vai guardar senhas ou segredos? Leia antes o [HARDENED_MODE.pt-BR.md](HARDENED_MODE.pt-BR.md) — inteiro.
>
> Ao longo dos anos, vi gente usando isto para automatizar a digitação de
> senhas em todo tipo de sistema. Sempre avisei que era má ideia, mas, como
> isso nunca deteve ninguém, decidi tornar o uso oficialmente suportado e fazer
> o melhor que pude para atenuar os problemas óbvios de segurança. Se é isso
> que você quer fazer, leia o documento acima por inteiro e faça tudo o que
> ele diz. Mesmo assim, não me responsabilizo se suas senhas ou segredos
> vazarem algum dia.
>
> **No mínimo, defina uma senha.** Uma placa sem senha aceita firmware novo de
> qualquer um nos seis segundos depois de conectada, e um firmware que digite
> todas as macros se escreve em segundos.

A v1 do firmware e da página está na tag `v1.0`, guardada como histórico. Ela
está totalmente obsoleta e, até onde sei, não sobrou nenhuma em uso: a v2 é
melhor em todos os aspectos. A página ainda oferece mudar uma placa v1 para a
v2.

## Do que você precisa

- **Um Digispark** (ATtiny85) ou um clone, do jeito que vem: a página grava a
  v2 nele pelo bootloader Micronucleus que vem de fábrica, que a v2 depois
  substitui. Ou qualquer placa ATtiny85 e um gravador ISP. O
  [Franzininho](https://franzininho.com.br/) funciona do jeito que vem, com
  o pino P0 da sua barra de pinos como sensor.
- **Um sensor no P0.** Qualquer pedacinho de metal serve — um fio desencapado,
  uma moeda, um quadrado de fita de cobre, ou até a barra de pinos que às
  vezes já vem soldada na placa — e nenhum outro componente é necessário. Um
  botão do P0 para o terra também funciona (`make BUTTON=1`), assim como um
  sensor lido através de um resistor de dreno de cerca de 1 MΩ (`make RC=1`).
- **O LED da placa no P1**, que é onde o Digispark o tem. Um clone com o LED
  no P0 precisa de `PIN_LED` alterado em `DigiMacro.ino`, já que o P0 é o
  sensor.
- **Um receptor de IR opcional no P2:** um TSOP4838 ou qualquer outro do tipo,
  com a saída no P2, permite que um controle remoto que fale NEC — a maioria
  dos baratos fala — dispare macros. Nada mais é necessário, e uma placa sem
  ele não muda em nada.
- **Chrome ou Edge**, num computador: WebHID — e WebUSB, para chegar ao
  Micronucleus ao preparar uma placa — não existem no Firefox nem no Safari.
  Nem no celular ou tablet: todo navegador de iPhone ou iPad é Safari por
  baixo, e o Chrome do Android não tem WebHID. Testado no Linux, e no
  Windows 10 numa máquina virtual; o macOS deve funcionar do mesmo jeito no
  Chrome ou no Edge, sem instalar nada, mas ninguém testou ainda. A placa em
  si digita em qualquer coisa que aceite um teclado USB, celulares
  inclusive; só a configuração precisa da página.
- **Para instalar,** nada além da página: o firmware publicado está versionado
  em `web/firmware/`.
- **Para compilar,** `make`, `python3` e avr-gcc, no Linux. O Makefile procura
  o avr-gcc que o core AVR do Arduino instala em `~/.arduino15`; `make
  AVR=/caminho/para/bin/` aponta para outro lugar. Os testes também precisam
  de `gcc`, `g++` e `node`.

<p align="center">
  <img src="docs/franzininho.jpg" width="300"
       alt="Uma placa Franzininho DIY: um ATtiny85 num soquete, um conector USB e uma barra de pinos">
</p>

*Um [Franzininho](https://franzininho.com.br/) DIY. Tocar o P0 da barra de
pinos é todo o sensor de que ele precisa.*

## Primeiros passos

1. **No Linux, deixe o navegador chegar à placa.** Todo dispositivo hidraw e
   USB é só do root até que uma regra do udev diga o contrário:
   ```
   sudo cp web/60-digimacro.rules /etc/udev/rules.d/
   sudo udevadm control --reload
   ```
   No Windows, o Micronucleus precisa de um driver uma vez, para preparar uma
   placa: rode o [Zadig](https://zadig.akeo.ie/) enquanto a placa está no
   bootloader, nos primeiros segundos depois de conectá-la, e instale o WinUSB
   para ela. O teclado em si não precisa de nada em lugar nenhum. Se a placa
   foi preparada no Linux, o Windows não precisa de nada instalado.
2. **Abra o aplicativo.** Aponte o navegador para:

   https://digimacro.postcogito.org/

   Ou sirva você mesmo. WebHID exige um contexto seguro, e `localhost`
   conta:

   ```
   cd web && python3 -m http.server 8000
   ```

   Depois abra `http://localhost:8000/`. Qualquer servidor https servindo
   `web/` funciona do mesmo jeito.
3. **Grave a v2 na placa.** Um Digispark do jeito que vem: clique em
   **Preparar uma placa nova**, conecte a placa, clique de novo em até cinco
   segundos e escolha *Micronucleus*. Depois disso a placa volta como
   DigiMacro, mas ainda sem firmware, e a página oferece instalá-lo — veja
   *Preparando uma placa* abaixo. Com um gravador: veja *Com um gravador*.
4. **Conecte.** Escolha a placa e então **segure o sensor por quatro
   segundos**. A página lê sozinha tudo o que a placa guarda; se a placa tem
   senha, digite-a e clique em Destravar.
5. **Escreva algumas macros**, clique em **Mandar para a placa** e toque um
   código num editor de texto.

## Tocando

Um toque de menos de um quarto de segundo é ponto, e um mais longo é traço.
700 ms de silêncio encerram o código. Um código tem de um a cinco símbolos,
então existem 62 códigos. Segurar o sensor por quatro segundos não toca nada;
abre a janela durante a qual o aplicativo pode ler e gravar a tabela, por
meio minuto.

Tocar o sensor interrompe o que estiver sendo digitado. O sensor é lido entre
as teclas — uma leitura do toque leva um quarto de milissegundo, contra vinte
de uma tecla — e durante as pausas, então até uma macro que pula para si
mesma, que por definição roda para sempre, para quando tocada. Esse toque não
conta também como símbolo, e mantê-lo abre a janela, então uma macro
desembestada é interrompida e consertada num gesto só. O único laço que o
toque não interrompe é um que nunca digita nem pausa. O firmware limita esse
caso contando os saltos, e o aplicativo se recusa a gravar um.

### O que o LED diz

| LED | significa |
|---|---|
| apagado | ocioso, e também enquanto você toca um código, para não soletrá-lo a quem estiver olhando |
| aceso, fixo | uma macro está digitando ou em pausa; toque o sensor para interrompê-la |
| três piscadas rápidas | o código não correspondeu a nada |
| piscada lenta, uma por segundo | a janela está aberta: o aplicativo pode ler e gravar a tabela por meio minuto |
| um lampejo curto a cada dois segundos | bloqueio leve: códigos errados demais, então todo toque é ignorado até a espera acabar |
| tremulação rápida, oito vezes por segundo | bloqueio forte: todo toque é ignorado até a senha ser dada no aplicativo |

A piscada da janela tem prioridade sobre os dois bloqueios, então dá para ver
que o toque longo pegou mesmo com a placa bloqueada.

## O aplicativo

`web/index.html` mostra uma coisa de cada vez:

- **Nada conectado:** só Conectar.
- **Conectado:** o toque longo, e a senha se a placa tiver uma, digitada antes
  ou depois do toque longo, como preferir.
- **Dentro:** tudo o que a placa guarda, já lido.

**Mandar para a placa**, à direita da linha de status, só fica habilitado
quando o que está na tela difere do que a placa guarda, comparando os bytes
que seriam gravados. Todas as outras ações informam o resultado numa linha
sob o próprio botão. A janela dura meio minuto e editar leva mais tempo,
então, quando ela expira, a ação pede o toque longo de novo, espera por ele e
dá a senha de novo sozinha. A página guarda a senha só enquanto a placa
continua conectada.

As seções são **Macros**, com a escolha do layout de teclado ao lado do
título; **Teclas**, uma paleta que insere no cursor qualquer coisa da lista
abaixo; **Inicialização**; **Computador**; **Senha**; **Bloqueio**; **Cópia
de segurança**; e uma referência. O aplicativo fala inglês e português do
Brasil, escolhido pelo navegador e trocado no canto superior direito.

### Escrevendo macros

Uma macro é texto, com chaves para o que o texto não consegue dizer:

```
 hello{Enter}                 anything is typed as itself
 \n  \t  \\  {{               newline, tab, backslash, a literal {
 {Ctrl+c}  {Alt+Tab}  {F5}    modifiers, and keys that are not characters
 {pause 1.5}                  wait, in seconds
 x{11}                        eleven of them, as in a regular expression
 (ab){3}                      ...of a whole group rather than one thing
 {goto .-}                    carry on with another macro, and never come back
 {host}  {host!}              stop unless this computer sent the host key lately
 {IR 00 45}                   where the remote's button 00 45 starts typing
```

`{11}` significa onze ao todo, como um quantificador numa expressão regular,
e uma contagem conta o que vier antes dela: uma tecla, uma letra acentuada ou
um grupo entre parênteses. Um parêntese é só um parêntese, a menos que uma
contagem venha depois do que fecha.

O editor codifica enquanto você digita, então um erro é apontado ao lado da
macro que o tem, e o orçamento de bytes — 480 no total — está sempre na tela.
Nada é gravado enquanto todas as macros não forem válidas.

**Caracteres acentuados são assunto do computador, não da placa.** A placa
envia pressionamentos de tecla, então o que aparece depende do layout de
teclado do computador em que ela está conectada. Escolha esse layout ao lado
do título Macros e `coração` é aceito e expandido no acento e depois na
letra. Deixe no US puro, em que não há como digitar um acento, e o editor
avisa em vez de digitar outra coisa.

### O controle remoto

Com um receptor de IR no P2, um botão do controle remoto começa a digitar
logo depois do seu `{IR}`, onde quer que ele esteja na macro, e segue até o
fim dela. Acionada pelo código, a mesma macro digita direto, passando pelo
`{IR}`, que não faz nada. Uma macro pode ter vários, então uma macro só pode
ser um menu de pontos de partida.

Uma macro que só o controle remoto deve alcançar recebe um código de `IR1` a
`IR30` em vez de pontos e traços. Nenhum toque produz esses códigos, então
nenhum toque e nenhum chute jamais a digita; um `{goto IR1}` ainda pode.

Para descobrir os dois bytes de um botão, ponha o cursor numa macro e clique
em **botão do controle** na paleta de teclas: isso coloca `{IR }` ali, e a
placa digita entre as chaves o próximo botão que receber — em até dez
segundos. Não é preciso nem o toque longo nem a senha: tudo o que isso pode
fazer é digitar um botão em hexa.

Botões que não estão na tabela não fazem nada, e nunca contam para o
bloqueio: as salas estão cheias de controles remotos dos outros. Uma placa
bloqueada ignora o controle remoto, assim como ignora os toques, e uma
verificação `{host}` vale onde quer que o controle comece. Segurar um botão
executa a macro dele uma vez. O receptor é opcional, e `HAVE_IR 0` em
`DigiMacro.ino` compila sem ele.

### Inicialização

Uma macro pode se digitar sozinha quando a placa é conectada. Ela é guardada
como o código, e não como uma posição na tabela, então reordenar as macros
não a faz apontar para outro lugar, e um código que não existe mais não
digita nada. É uma configuração, não uma macro, então tem um Salvar próprio;
ela espera enquanto a tabela tiver por gravar uma macro que ela nomeia, e
gravar uma tabela sem o código da macro de inicialização a apaga.

Ela espera o computador ter onde colocar as teclas, o que não é o mesmo que
estar conectada. O computador lê os descritores cedo e liga o teclado à sua
pilha de entrada algum tempo depois, e uma macro que começasse nesse meio
tempo perderia o tanto dela que coubesse no intervalo. Então a placa espera
o computador lhe mandar um relatório das luzes de trava — o computador
falando *com* o teclado, o que ele só faz quando já existe um — e desiste de
esperar depois de um segundo e meio, para o caso de um computador que nunca
o mande.

## Segurança

O que vem a seguir é em camadas, opcional, e ausente num chip apagado: uma
placa que digita onze setas para a direita numa televisão não deveria pedir
nada a ninguém. Sem senha, nada disso protege a EEPROM de alguém com a placa
e um notebook, já que a placa aceita firmware novo de qualquer um nos
segundos depois de reconectada. Com senha, uma atualização também a exige; um
gravador ainda lê o chip, a menos que ele esteja protegido. O
[HARDENED_MODE.pt-BR.md](HARDENED_MODE.pt-BR.md) explica o que cada camada
impede e o que não impede.

### O toque longo

Chegar à tabela exige o toque longo de quatro segundos, que nenhuma página
web ou programa consegue fazer sozinho. Numa placa sem senha, é a única coisa
entre uma página hostil e as macros.

### A senha

Uma *segunda* barreira para a tabela, e a única para o firmware. A janela diz
que tem alguém na placa; a senha diz que é a pessoa certa, o que um ladrão
com a placa na mão não é. Ela não protege a *execução* de uma macro: um
dispositivo que pede senha antes de digitar é um dispositivo que ninguém
carregaria.

Quem a verifica é o núcleo da placa, não o firmware, porque ela protege as
atualizações mesmo quando não há firmware para perguntar — depois de uma
atualização interrompida no meio, por exemplo, quando a EEPROM ainda guarda
todos os segredos. Definir, trocar e remover a senha cabem ao firmware.

Uma resposta errada custa um segundo e, com o bloqueio ligado, conta como
código errado — com o firmware rodando. Só com o núcleo, depois de uma
atualização interrompida, não há bloqueio contra o qual contar, e o segundo
é tudo o que limita as tentativas: mais um motivo para doze caracteres. É só
isso: a senha nunca é recusada por bloqueio, porque é ela que desfaz um
bloqueio forte, e um limite sobre ela é a única trava que pode deixar o
próprio dono de fora também. Sua defesa contra adivinhação é o próprio
tamanho, até doze caracteres. A comparação vai até o fim aconteça o que
acontecer, então o tempo que ela leva não diz quanto da resposta estava
certo. O destravamento dura tanto quanto a janela e fica na RAM, então
desconectar a placa a trava de novo. Só com o núcleo rodando, não há janela:
o próprio núcleo encerra o destravamento depois de uns sete segundos sem
chegar uma página de firmware, para que uma placa largada no meio de uma
atualização não fique aberta.

### O bloqueio

Ele exige a senha, já que dá-la é o único jeito de desfazer um bloqueio
forte. São dois estágios, para que o dono possa errar um código sem precisar
do aplicativo:

- **Bloqueio leve.** Depois de um número definido de códigos errados, a placa
  ignora todo toque, certo ou errado, por um tempo definido: um minuto por
  padrão, até quarenta.
- **Bloqueio forte.** Depois de um número definido de bloqueios leves
  seguidos, ela ignora os toques até a senha ser dada no aplicativo. A macro
  de inicialização também para, senão bastaria a um ladrão conectar a placa
  de novo.

Uma macro que de fato é executada zera as duas contagens. Assim, quem tenta
adivinhar tem no máximo *códigos errados × bloqueios leves* tentativas antes
de a placa parar de ouvir, o que, com 1 e 3, dá três, de 62 códigos. O
aplicativo calcula essas chances para a configuração que você escolher.

Tudo fica na EEPROM, então desconectar a placa não leva ninguém a lugar
nenhum. A placa não tem relógio para saber quanto tempo ficou desconectada,
então um bloqueio leve encontrado ao ligar recomeça a espera inteira. Um
código errado é contado *antes* das três piscadas que o informam, então puxar
o cabo assim que as piscadas começam não adianta nada. Um código que é
executado não grava nada, a menos que haja uma contagem a zerar, então o uso
normal não desgasta a EEPROM.

### Confiando no computador

Um dispositivo USB não faz ideia de em qual computador está conectado, então
uma placa que digita uma senha a digita para quem a tiver na mão. Para um
computador cujo software você controla, há uma resposta: um pequeno programa
cliente nele manda uma chave à placa e, por um tempo depois disso — um minuto
por padrão, até quarenta —, as macros protegidas por uma verificação do
computador são executadas. Conectadas em qualquer outro computador, elas
param.

```
 {host}      stop here, quietly, unless this computer sent the key lately
 {host!}     stop as if the code were not in the table at all
```

`{host!}` é o que importa contra quem tenta adivinhar. Recusado, ele parece
exatamente um código que não existe — três piscadas, e um código errado para
o bloqueio —, então, no computador errado, um código protegido não se
distingue de um vazio. Isso só vale se nada aparecer antes da verificação, e
o aplicativo avisa sobre isso. O LED fica apagado até uma macro digitar ou
pausar, pelo mesmo motivo: uma piscada na entrada entregaria o código.
`{host}` pode ir em qualquer lugar, para uma macro que digita um nome de
usuário e então para antes da senha. Uma macro recusada não conta como um
código que funcionou, então tocar um código protegido no computador errado
não zera os erros de quem tenta adivinhar.

A chave são oito bytes aleatórios, criados na seção Computador do aplicativo,
gravados na placa e baixados num arquivo com o nome do número de série da
placa. O cliente é o `tools/hostauth`, um único arquivo C sem bibliotecas,
para Linux e Windows. Os dois estão prontos na
[página de releases](https://github.com/marcocarnut/DigiMacro/releases):
`digimacro-host-linux-x86_64`, linkado estaticamente para rodar em qualquer
distribuição, e `digimacro-host.exe`. No Linux ele precisa da mesma regra do
udev que o aplicativo. Compilado a partir do código-fonte:

```
make -C tools/hostauth install            # into ~/.local/bin
mkdir -p ~/.config/digimacro
cat ~/Downloads/digimacro-*.key >> ~/.config/digimacro/host.key
chmod 600 ~/.config/digimacro/host.key
digimacro-host                            # once
digimacro-host -e 30                      # every 30 seconds, for as long as it runs
```

Para o Windows, pegue o `digimacro-host.exe` na página de releases, ou deixe
`make -C tools/hostauth windows` fazer a compilação cruzada com o MinGW-w64
(`gcc-mingw-w64-x86-64` no Debian e no Ubuntu). É um arquivo só, que não
precisa de nada instalado junto, e lê as chaves de
`%APPDATA%\digimacro\host.key`:

```
mkdir "%APPDATA%\digimacro"
type digimacro-*.key >> "%APPDATA%\digimacro\host.key"
digimacro-host.exe -e 30
```

Para mantê-lo rodando enquanto você estiver logado, ponha um atalho para
`digimacro-host.exe -e 30` na pasta Inicializar (`shell:startup`).

Rodado com `-e` a partir da sua sessão, e parado quando a tela trava, a
confiança passa a ser "enquanto eu estiver logado". O cliente encontra pelo
número de série cada placa conectada, então um arquivo de chaves serve a
várias placas. `-k` indica outro arquivo de chaves; no Linux, o padrão
respeita `$XDG_CONFIG_HOME`.

Mandar a chave não exige nem o toque longo nem a senha, porque não há
ninguém na placa — como o INFO, e a leitura da flash, que não guarda nada
secreto. Por isso não pode fazer mais nada: não abre janela, não destrava
nada e não lê nada, e uma chave errada **não** conta para o bloqueio. Se
contasse, qualquer programa em qualquer computador poderia pôr a placa em
bloqueio forte sem ninguém tocar nela. Em vez disso, uma chave errada custa
um segundo, num relógio próprio.

### Cópias de segurança

**Salvar uma cópia** salva a EEPROM inteira da placa num arquivo — macros,
inicialização, bloqueio, chave de computador, senha e número de série — e
**Restaurar** a coloca de volta, então uma placa apagada por um gravador volta
a ser a mesma, inclusive a permissão do Chrome e os arquivos de chave de
computador. A v1 e a v2 usam o mesmo mapa de EEPROM, então a cópia de uma
placa v1 pode ser restaurada numa v2. A senha entra por último, senão a placa
se trancaria no meio da própria restauração, e as contagens de códigos
errados e de bloqueios leves do bloqueio recomeçam. Depois a placa reinicia
sozinha — sai do barramento e volta, como se reconectada —, porque o número
de série só é enviado quando a placa é conectada, e é fácil esquecer disso
depois de restaurar. Para o Chrome, um número de série novo é um dispositivo
novo, então a página pede que você clique em Conectar e escolha a placa de
novo. Firmware anterior ao 8 não sabe reiniciar sozinho, e a página pede que
você reconecte a placa.

Numa placa com senha, o arquivo é cifrado com ela: o scrypt, com os
parâmetros do BIP38, deriva a chave, e o AES-256-GCM cifra a imagem e diz
claramente quando a senha está errada. Os navegadores não têm scrypt, então
o `web/backup.js` o implementa, e um teste o confere com o do próprio node e
com os vetores da RFC 7914. Um arquivo copiado pode ser atacado fora da
placa, sem toque longo e sem a espera de um segundo, então uma senha com
menos de oito caracteres gera um aviso antes; uma placa sem senha salva a
cópia às claras, também depois de um aviso. Restaurar grava a imagem
inteira, seja qual for a placa de origem, então restaurar a cópia de uma
placa em outra dá à segunda a identidade da primeira.

### Toda placa tem um número de série

Quatro bytes aleatórios, criados uma vez pela primeira inicialização do
firmware num chip que nunca teve um e guardados na EEPROM, mostrados como
oito dígitos hexa depois de `postcogito.org:`; o núcleo os lê toda vez que o
computador pergunta. Até o firmware criar um — numa placa nova, só com o
núcleo —, o número de série é `FFFFFFFF`, e o Chrome vê mais uma vez um
dispositivo novo quando ele muda. O prefixo é a regra da obdev para o
`16c0:27db`, o ID compartilhado para teclados diferenciados pelo número de
série: o número de série deve começar com um domínio do autor, para que
dispositivos de pessoas diferentes no mesmo ID não se confundam. O Chrome só
memoriza uma permissão WebHID para um dispositivo que tenha número de série,
então, sem ele, cada reconexão exigiria escolher a placa na lista de novo.
Ser por placa, e não por modelo, faz com que duas placas tenham cada uma sua
permissão, e sua chave de computador.

A aleatoriedade vem do jitter do oscilador do watchdog contra o clock da CPU:
dois osciladores RC independentes, então os bits baixos de um timer amostrado
a cada estouro do watchdog são imprevisíveis. Leva cerca de um segundo, uma
vez na vida da placa.

## Atualizando o firmware

A linha de status mostra a versão do firmware da placa, seguida de **(o mais
recente)**, ou de **(atualizar…)** quando a página traz algo mais novo —
*(atualização disponível)* até o toque longo deixar a página entrar; isso
abre uma caixa de diálogo com a atualização. Clique em **Atualizar o
firmware** com a janela aberta, ou segure o sensor quando ela pedir: a página
grava o novo firmware pela mesma conexão, uma página de flash por vez, e a
placa reinicia nele alguns segundos depois e pede o toque longo de novo. Sem
bootloader, sem segunda lista, sem driver. As macros, a senha, o bloqueio, a
chave de computador e o número de série ficam todos na EEPROM, que uma
atualização não toca.

- **Seis segundos.** Com o firmware rodando, o núcleo só aceita a primeira
  página de uma atualização nos seis segundos depois de a placa ser
  conectada, como o Micronucleus fazia — ou depois que o firmware abre essa
  janela de novo, o que a página lhe pede por trás do toque longo e da senha.
  Com senha definida, o núcleo também a exige, nos dois casos.
- **Interrompida no meio**, por um cabo puxado ou uma página fechada, a placa
  fica sem firmware para rodar: o núcleo apagou antes o cabeçalho do antigo,
  então nunca roda metade de um. Ela volta como DigiMacro só com o núcleo, e
  a página diz **O DigiMacro não está rodando nesta placa** e oferece
  instalá-lo de novo — por trás da senha, se a placa tiver uma, e de nada
  mais. Deixada conectada com a página fechada, a placa encerra o
  destravamento sozinha depois de uns sete segundos.
- **Um firmware que para de responder** por dois segundos é reiniciado pelo
  watchdog — assim como um firmware que trava, saltando de volta ao início —,
  e o núcleo não o inicia de novo: a página mostra a mesma tela. Desconectar
  a placa e conectar de novo lhe dá outra chance.
- **Outra aba ou programa falando com a placa** ao mesmo tempo não causa
  estrago: a página só confia numa resposta à sua própria pergunta, e confere
  cada página que grava lendo-a de volta.

### Preparando uma placa

**Preparar uma placa nova**, ao lado de Conectar, grava a v2 num Digispark do
jeito que ele vem. Clique, conecte a placa e clique de novo em até cinco
segundos: o navegador pede que você escolha *Micronucleus*, e a página grava
por ele o núcleo da v2. A placa então volta como DigiMacro só com o núcleo —
número de série `FFFFFFFF` até o firmware criar um — e **Conectar** leva à
instalação. O firmware é gravado pelo próprio Micronucleus e, na primeira
página dele, o núcleo retoma o vetor de reset que o Micronucleus tinha
apontado para si, então o Micronucleus nunca mais roda. Uma queda de energia
nesses poucos milissegundos precisa de um gravador para consertar, o mesmo
risco de qualquer gravação pelo Micronucleus; acontece uma vez na vida da
placa.

**Uma placa v1** muda do mesmo jeito: conecte-a, e **(mudar para a v2…)** na
linha de status abre a caixa de diálogo do firmware. A placa reinicia no
Micronucleus, recebe o núcleo e volta pedindo o firmware. As macros, a senha,
a chave de computador e o número de série vêm junto: a v2 mantém o mapa de
EEPROM da v1.

O Micronucleus é acessado por WebUSB: o Linux precisa da linha dele em
`web/60-digimacro.rules`, e o Windows precisa do
[Zadig](https://zadig.akeo.ie/) uma vez, rodado enquanto a placa está no
Micronucleus, para instalar o WinUSB para ela.

### Com um gravador

`web/firmware/digimacro-v2-full.hex` é o núcleo e o firmware numa imagem só,
com os fuses que uma placa v2 quer:

```
avrdude -c <programmer> -p t85 -e -U flash:w:web/firmware/digimacro-v2-full.hex:i \
        -U lfuse:w:0xe1:m -U hfuse:w:0xdd:m -U efuse:w:0xfe:m
```

O baixo `0xE1` é o clock do Digispark (`0xF1` também funciona); o alto `0xDD`
liga a detecção de queda de tensão, que um chip que grava a própria flash
quer; o estendido `0xFE` mantém o SELFPRGEN, sem o qual a placa não consegue
se atualizar. O apagamento leva a EEPROM junto: faça uma cópia antes. A
proteção vai além:
[HARDENED_MODE.pt-BR.md](HARDENED_MODE.pt-BR.md).

### Placas v1

A única atualização que uma placa v1 recebe da página é a mudança para a v2:
a v1 está congelada no firmware 9, e a v2 faz tudo o que ela fazia. A placa
reinicia no bootloader e o navegador pede que você o escolha — uma segunda
lista, uma classe de fabricante simples por WebUSB.

- **Escolha em poucos segundos.** O bootloader espera uns seis e depois volta
  ao firmware antigo, então o clique que inicia a mudança precisa encontrar a
  janela já aberta. Se não estiver, esse clique pede o toque longo, e o
  próximo clique faz a mudança.
- **Firmware anterior ao 3** não sabe reiniciar no bootloader, então precisa
  do núcleo gravado à mão: `micronucleus --run
  web/firmware/digimacro-v2-core.hex`. Conecte a placa, e a página instala o
  firmware.
- **Uma placa v1 protegida** não tem bootloader, e mostra **(protegida)** em
  vez disso, ou **(não totalmente protegida…)** com uma lista do que falta —
  *(protegida: sem bootloader)* em firmware anterior ao 8.
- O comando REBOOT da v1 fica por trás da janela e da senha: uma placa no
  bootloader pode ser gravada com qualquer coisa.

O uploader é o do [DigiISP](https://digiisp.postcogito.org), portado para
JavaScript puro em `web/micronucleus.js`.

### Publicando

Um firmware novo chega à página por `make release`, que copia o build para
`web/firmware/` com um json informando a versão e o núcleo a que ele se
destina: `digimacro-v2.hex` para atualizações, `digimacro-v2-core.hex` para
preparar uma placa, `digimacro-v2-full.hex` para um gravador. Faça o commit
deles, e a página oferece a atualização a toda placa que rode algo mais
antigo — só se o núcleo dela falar a mesma API. A página carrega os próprios
arquivos, então servi-la não exige toolchain AVR.

O que é publicado vai para `build/release/`: `make site` gera a página como
um arquivo só, `index.html`, com os scripts e o firmware dentro, e `make
hosts` gera o `digimacro-host-linux-x86_64` (estático) e o
`digimacro-host.exe`. A página é tudo de que o site precisa; os dois clientes
vão no release do GitHub.

## Como funciona

### Ele nunca deixa de ser um teclado

Um teclado HID não é algo com que o navegador converse: o Chrome recusa
páginas de uso HID protegidas, entre elas a dos teclados. Então a placa traz
uma segunda coleção de nível superior numa **página de uso de fabricante**,
`0xFF00`, que o WebHID entrega, enquanto a metade teclado continua sendo
teclado. HID não precisa de driver em lugar nenhum, então o Windows não
precisa de nada instalado.

Isso custou duas coisas. O teclado abriu mão do boot protocol, já que duas
coleções numa interface exigem report IDs e um relatório de boot não pode
ter um, o que também o limita a três teclas simultâneas em vez de seis —
nada que aperte uma tecla de cada vez percebe. E não há landing page: o
Chrome oferece uma para dispositivos WebUSB, não para HID. Uma versão
anterior se reenumerava como dispositivo WebUSB depois do toque longo, e
voltava depois; o registro de projeto da v1 conta essa história (`git show
v1.0:PLAN.md`).

### O núcleo e o firmware

A flash é dividida em `0x1000`. Abaixo, o **núcleo**: o V-USB e os
descritores, o feature report, a verificação da senha e o gravador de
páginas, que recebe firmware novo e recusa qualquer endereço abaixo da
divisa. Ele nunca é regravado por USB, então é mantido pequeno e sem graça,
e testado no computador com mais rigor do que qualquer outra coisa. Acima,
o **firmware**: o sensor, o morse, a tabela, a execução das macros, o
bloqueio, a verificação do computador, o relógio e a janela.

Os dois se chamam por duas tabelas de saltos em endereços fixos — a do
núcleo logo abaixo da divisa, o cabeçalho do firmware nela — e não combinam
mais nada; `core/layout.h` é o contrato inteiro. O cabeçalho do firmware traz
um número mágico, o tamanho, a versão e a API de núcleo para a qual ele foi
compilado, seguido de um CRC: o núcleo só roda firmware que confere. Toda
interrupção, exceto a do V-USB, é repassada ao firmware, e o watchdog o
vigia. O registro de projeto da v2 conta como e por que isso foi decidido:
ele foi aposentado quando tudo o que havia nele estava construído, e `git
show ae7e0ac:PLAN-v2.md` o mostra.

### O protocolo

Um feature report de 36 bytes na coleção de fabricante, nos dois sentidos.
Faz-se um set para perguntar e um get para ver a resposta:

```
 [0] command   [1] address low   [2] address high   [3] count   [4..35] data
```

Protocolo 3, que é o que a v2 fala. O núcleo responde a INFO, PAGE, FREAD e
COMMIT, e a UNLOCK quando não há firmware; o firmware responde ao resto.

| | |
|---|---|
| `0x40` INFO | sempre responde: `[1..2]` "DM", `[3]` protocolo (3), `[4]` a versão do núcleo, `[5..6]` tamanho da EEPROM, `[7]` estado, `[8..11]` os fuses baixo, alto e estendido e o byte de trava como o chip os lê, `[12]` o estado do núcleo (abaixo), `[13]` a versão do firmware, 0 se não houver, `[14]` a API do núcleo, `[15..18]` onde o firmware pode ficar, `[19]` o tamanho da página de flash. Sem firmware rodando, as partes do firmware — `[5..6]`, e todo o `[7]` exceto os bits da senha — vêm como 0 |
| `0x42` RESTART | sai do barramento e volta, como se reconectada, sem reset: o número de série é lido de novo, então um restaurado passa a valer. A janela fecha, a confiança no computador é esquecida e a macro de inicialização não digita; os seis segundos para uma atualização não recomeçam |
| `0x60` READ | endereço e contagem na ida, até 32 bytes de EEPROM na volta |
| `0x61` WRITE | endereço, contagem e até 32 bytes na ida |
| `0x62` UNLOCK | a senha, se a placa tiver uma |
| `0x63` SETPASS | uma senha nova, ou nenhuma, com contagem 0 |
| `0x64` HOSTAUTH | a chave de computador. Não exige nem a janela nem a senha, e não faz mais nada |
| `0x65` PAGE | meia página de firmware novo, 32 bytes num endereço; a segunda metade a grava |
| `0x66` FREAD | flash, até 32 bytes: firmware publicado, então sem barreira. Recusado enquanto houver uma página esperando para ser gravada |
| `0x67` COMMIT | o firmware novo confere: inicie-o |
| `0x68` UPDATE | o firmware abre a janela de atualização: seis segundos para o primeiro PAGE |
| `0x69` LEARN | o próximo botão do controle remoto, em até dez segundos, é digitado como dois bytes em hexa em vez de executado |
| `0xEE` | volta no lugar do comando quando um pedido é recusado: a janela está fechada, a senha não foi dada, a senha ou a chave de computador está errada ou veio menos de um segundo depois de uma errada, ou o comando não existe |

INFO, HOSTAUTH, FREAD e LEARN não exigem barreira. PAGE exige a senha, se
houver uma, e, com o firmware rodando, a janela de atualização; COMMIT só
exige páginas recebidas e um firmware que confira. UNLOCK exige a janela — e,
só com o núcleo, nada. Todo o resto exige a janela, e a senha se houver uma.
O byte de estado do INFO:

| bit | |
|---|---|
| `0x01` | a janela está aberta |
| `0x02` | há uma senha definida |
| `0x04` | ...e ela foi dada, ou não há nenhuma |
| `0x08` | bloqueio leve |
| `0x10` | bloqueio forte |
| `0x20` | o computador mandou a chave há pouco |
| `0x40` | há uma chave de computador |
| `0x80` | só v1: compilado sem bootloader |

e o do núcleo, em `[12]`: `0x01` firmware rodando, `0x02` recebendo páginas,
`0x04` firmware presente mas não iniciado — o watchdog o reiniciou, ou ele
travou.

READ e WRITE alcançam a EEPROM inteira, bytes reservados inclusive — é assim
que o aplicativo grava as configurações, e é por isso que eles ficam por
trás das duas barreiras. Uma leitura além do fim retorna `0xFF`, e uma
gravação além dele para. As gravações vão de 32 em 32 bytes porque um byte de
EEPROM leva 3,4 ms e o dispositivo não consegue responder a mais nada
enquanto grava. Elas usam `eeprom_update_byte`, então uma tabela salva sem
mudanças não causa desgaste.

A placa tem um único relatório para todo mundo, então outro programa que
perguntasse entre uma pergunta e a resposta dela leria a resposta como se
fosse sua. A página pergunta de novo, a menos que a resposta repita o comando
e o endereço dela.

Se o report ID vai no payload além de ir no pedido é a única coisa que a
especificação HID deixa a gosto do freguês. O Chrome não o devolve; o
aplicativo verifica em vez de supor, e o firmware aceita os dois jeitos, o
que é seguro porque nenhum byte de comando é 2.

A v1 (protocolo 2) difere no INFO — `[4]` é a versão do firmware dela, e
`[12..19]` não existem —, tem o `0x41` REBOOT para o Micronucleus e nenhum
dos comandos de PAGE a UPDATE.

### A tabela

O aplicativo regrava a tabela inteira, então as entradas ficam empacotadas em
vez de em posições fixas:

```
 [hdr][keystroke...][0x00]      repeated, in any order
 [0xFF]                         end of table
```

`hdr` é o código: três bits de número-de-símbolos-menos-um, cinco bits de
símbolos, sendo o traço um 1 e o primeiro símbolo no bit mais alto que a
contagem usa. Não existe contagem acima de cinco, então `0xFF` encerra a
tabela e **um chip virgem é lido como uma tabela vazia** — nada a
inicializar. Dois bytes extras por macro, e 480 bytes para dividir como
quiser.

Uma tecla é um usage HID e não um caractere, porque quem faz o mapeamento de
layout é o aplicativo:

```
 bit 7     needs Shift
 bits 0-6  HID usage
```

Os usages `0x01`–`0x03` são estados que um teclado relata, nunca teclas que
alguém aperte, então esses três valores ficam livres para servir de escapes,
assim como os mesmos três com o bit 7 ligado, e `0x80`, Shift sem tecla
nenhuma. Daí o teste para um escape: um byte cujos sete bits baixos são
menores que quatro. Os sete estão ocupados:

| | |
|---|---|
| `0x01 nn` | pausa de `nn` décimos de segundo |
| `0x02 mm` | aplica a máscara de modificadores HID `mm` à próxima tecla |
| `0x03 nn` | repete a tecla anterior mais `nn` vezes |
| `0x81 hh` | continua na macro cujo cabeçalho é `hh-1` |
| `0x82` | marca: o que vem depois se repete como grupo, não como uma tecla só |
| `0x83 nn` | para, a menos que o computador tenha mandado a chave há pouco: em silêncio se `nn` for 1, como um código vazio se for 2 |
| `0x80 a c x` | onde o botão `a`/`c` do controle remoto inicia esta macro: `a` e `c` com o bit 7 forçado para 1, `x` = `0x80` mais os bits altos originais deles (o de `a` no bit 0, o de `c` no bit 1). Na execução normal, não faz nada |

Nenhum argumento pode ser zero, já que um byte zero encerra a macro. `0x03` é
codificação run-length: "seta para a esquerda onze vezes" custa três bytes em
vez de onze, e repetir *o que veio antes* leva junto os modificadores
aplicados. `0x82` é o que mantém isso funcionando quando uma tecla deixa de
ser um caractere: num teclado com acentos mortos, `ç` são dois
pressionamentos, o acento e depois a letra, então repetir só "a tecla
anterior" digitaria `çcccc`. Uma marca diz onde começou a coisa repetida.
Ela não tem argumento — sua posição é o argumento — e marcas não se
aninham.

Essa é a interface entre o firmware e o aplicativo, então nenhum dos lados é
confiável sozinho: `web/table.js` é um original único, compartilhado pelo
aplicativo e por um teste que executa o que ele codifica no firmware de
verdade, e outro teste lê os valores dos escapes em `DigiMacro.ino` e confere
se os dois concordam.

### A EEPROM

| | |
|---|---|
| `0x000`–`0x1DF` | a tabela de macros |
| `0x1E0`–`0x1E3` | o número de série |
| `0x1E4`–`0x1EB` | a chave de computador; tudo `0xFF` se não houver |
| `0x1EC` | bloqueio: códigos errados antes de um bloqueio leve; 0 ou `0xFF` para sem bloqueio |
| `0x1ED` | ...bloqueios leves seguidos antes de um bloqueio forte; 0 ou `0xFF` para nunca |
| `0x1EE` | ...duração de um bloqueio leve em dezenas de segundos, 1–240; qualquer outro valor vale 6 |
| `0x1EF` | ...códigos errados até agora |
| `0x1F0` | ...bloqueios leves seguidos até agora |
| `0x1F1` | ...bloqueios fortes desfeitos com a senha, desde sempre |
| `0x1F2` | por quanto tempo o computador continua confiável, em dezenas de segundos, 1–240; qualquer outro valor vale 6 |
| `0x1F3` | o cabeçalho da macro de inicialização; qualquer valor impossível como cabeçalho significa nenhuma |
| `0x1F4`–`0x1FF` | a senha, completada com zeros; `0xFF` no primeiro byte se não houver |

Os três contadores do bloqueio são guardados invertidos, para que um byte
apagado seja lido como zero. O bloco reservado está cheio: qualquer coisa a
mais vai ter que sair da tabela.

## Compilando

```
make                            # the core and DigiMacro, into build/v2/
make release                    # ...and into web/firmware/, for the page
make test                       # the core's own tests
```

`make` gera `digimacro.hex`, o firmware para atualizações, e
`digimacro-full.hex`, o núcleo e o firmware para um gravador. Opções, cada
uma `=1`:

- `BUTTON`: um botão do P0 para o terra em vez de um sensor.
- `RC`: um sensor lido através de um resistor de dreno em vez do pull-up do
  chip.
- `LEDECHO`: o LED acompanha a entrada enquanto se toca. Só para trabalhar
  no caminho de entrada, já que mostra o código a quem estiver perto.
- `TOUCHDEBUG`: a placa digita as próprias leituras do sensor duas vezes por
  segundo, o único canal de diagnóstico de um dispositivo sem porta serial. O
  `web/README.md` explica como lê-las.

`make release` não aceita nenhuma: é o firmware padrão. `make spike` e `make
page` geram o que provou a v2 antes de o DigiMacro passar para ela — um
núcleo que fica abaixo do Micronucleus, três aplicativos pequenos e uma
página que alterna entre eles — em `build/v2-spike/`.

## Testes

```
test/run.sh
```

compila o `DigiMacro.ino` de verdade para o computador contra um hardware
simulado e exercita o percurso da tabela, cada escape, os limites que impedem
uma tabela malformada de chegar aos bytes reservados, a decodificação do
morse a partir do pino de entrada, a senha, o bloqueio e a verificação do
computador, e os dois jeitos de ler o sensor. Depois, a linguagem de macros
do aplicativo sozinha; as duas linguagens uma contra a outra; o núcleo da v2
montado para o Micronucleus, com os dois saltos decodificados e seguidos até
onde vão parar; as atualizações da página contra uma placa v2 simulada que
trava, devolve páginas antigas, é interrompida por outro programa e é
desconectada no meio; o próprio núcleo, compilado para o computador com a
biblioteca do firmware e submetido a tudo o que um computador pode perguntar,
duas vezes — com a região do firmware indo até o fim do chip e parando abaixo
do Micronucleus; as cópias de segurança, com o scrypt conferido contra o do
próprio node e um arquivo que só abre com a sua senha; a página publicada, um
arquivo só com o firmware dentro; e, por último, atravessando a fronteira
entre as linguagens, uma tabela codificada pelo `web/table.js` executada no
firmware. Onze suítes, poucos segundos. Sem `node`, as sete que precisam dele
são puladas.

## Organização

| | |
|---|---|
| `DigiMacro.ino` | o firmware: toque, morse, o percurso da tabela, a execução, o bloqueio, a verificação do computador |
| `app/` | o que todo firmware linka: o cabeçalho, as chamadas ao núcleo e o `hidusb.c` — o relógio, a janela, a digitação e os verbos do protocolo. Também os aplicativos do spike, o `spikectl.py` e a página dele |
| `core/` | o núcleo: o V-USB com sua configuração, os descritores, a verificação da senha e o gravador de páginas; `layout.h`, o contrato com o firmware; e os testes dele no computador |
| `Makefile` | compila tudo isso |
| `test/` | o build no computador do sketch de verdade contra hardware simulado, e os testes do lado web |
| `tools/hostauth/` | o cliente que manda a chave de computador, para Linux e Windows |
| `web/` | `index.html` é o aplicativo, `hid.html` uma página de protocolo cru para desenvolvedores, `table.js` e `i18n.js` o que eles compartilham, `coreupdate.js` atualiza placas v2, `micronucleus.js` acessa o Micronucleus, `backup.js` as cópias de segurança, `60-digimacro.rules` a regra do udev |
| `web/firmware/` | o que a página instala: o firmware, o núcleo e a imagem completa da v2 |
| `docs/` | as fotos lá de cima |
| `HARDENED_MODE.md` | como deixar uma placa apta a guardar segredos, e o que isso ainda não protege |
| `*.pt-BR.md` | este README e o HARDENED_MODE.md em português do Brasil |

## Relacionados

- O [DigiISP](https://digiisp.postcogito.org) transforma um Digispark num
  gravador AVR pelo navegador. É com ele que se configura o modo protegido, e
  de onde vieram o uploader do Micronucleus e o truque do número de série.
- O [V-USB](https://www.obdev.at/products/vusb/) é a pilha USB, e o
  [Micronucleus](https://github.com/micronucleus/micronucleus) o bootloader
  que a v1 usava e pelo qual a v2 é preparada.

## Licença

Copyright © 2026 Marco Carnut. GPLv2, seguindo o V-USB; veja `LICENSE`.
