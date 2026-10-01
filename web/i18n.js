/* i18n.js -- what the app says, in each language it says it.
 *
 * One entry per string, so a missing translation is a missing key rather than
 * a sentence half in the wrong language; test/i18n.js checks that the two
 * dictionaries hold exactly the same keys. A string may be a template, where
 * {name} is filled from the parameters, or a function where a language needs
 * to decide something for itself -- plurals, mostly.
 *
 * The encoder's messages are here too. They reach the person editing a macro,
 * so they are part of the interface, not diagnostics: web/table.js keeps the
 * English as its default and takes a translator through setMessages().
 */

const STRINGS = {

en: {
  'lang-name':      'English',
  tagline:          'Tap a code, type a macro.',
  keyboard:         'keyboard',
  language:         'language',
  connect:          'Connect',
  'connect-hint':   'not connected yet',
  macros:           'Macros',
  'th-code':        'Code',
  'th-types':       'Types',
  'ph-types':       'what this code types',
  'add-macro':      'Add a macro',
  'load-example':   'Load the example',
  budget:           '{n} of {max} bytes',
  keys:             'Keys',
  'on-plug':        'when plugged in',
  nothing:          'nothing',
  'auto-dangling':  'The macro this board typed on power-up is not in the '
                  + 'table any more, so it will type nothing. Choose another, '
                  + 'or leave it.',
  'mods-hint':      'held for the next key',
  'a-key':          'a key',
  insert:           'insert',
  pause:            'pause',
  repeat:           'repeat',
  'pad-hint':       'Inserts at the cursor in whichever macro you were last '
                  + 'editing. A modifier stays pressed until the next key.',
  read:             'Read from the board',
  write:            'Write to the board',
  'log-initial':    'Nothing yet. Connect, then hold the pad for four seconds '
                  + 'when the board asks — it stays a keyboard throughout, and '
                  + 'the hold is the one thing a web page cannot do for itself.',

  'sec-summary':    'Password',
  'sec-none':       'This board has no password, which is the usual thing and '
                  + 'perfectly fine: holding the pad is what keeps a web page '
                  + 'out, and plenty of macros are not secret. Set one if the '
                  + 'table is worth protecting from somebody holding the '
                  + 'board.',
  'sec-locked':     'This board has a password. It will be asked for before '
                  + 'the table can be read or written, once per session.',
  'sec-unlocked':   'This board has a password, and it has been given. It will '
                  + 'be asked for again after the window closes.',
  'set-password':   'Set',
  'change-password':'Change',
  'clear-password': 'Remove it',
  'ph-password':    'a password, up to 12 characters',
  unlock:           'Unlock',
  unlocked:         'Unlocked, until the window closes.',
  'needs-password': 'This board has a password. Type it in the Password '
                  + 'section below and press Unlock.',
  'unlock-to-change': 'Press Unlock with the current password first, then type '
                  + 'the new one and press Change.',
  'wrong-password': 'That is not the password. The board waits a second after '
                  + 'each wrong answer, which is most of what a short password '
                  + 'has going for it.',
  'password-empty': 'Type a password first, or use Remove it.',
  'password-set':   'Password set. It is asked for once per session, and the '
                  + 'board still types its macros without it.',
  'password-cleared': 'Password removed. The board is open to anyone who can '
                  + 'hold the pad.',
  'ref-summary':    'What can go in a macro',
  'ref-anything':   'typed as itself',
  'ref-escapes':    'newline, tab, backslash',
  'ref-brace':      'a literal {',
  'ref-named':      'a key that is not a character',
  'ref-mods':       'modifiers on any key or character',
  'ref-combined':   '...combined, in any order',
  'ref-pause':      'wait, in seconds, 0.1 to 25.5',
  'ref-repeat':     'eleven of them in all, as in a regular expression',
  'ref-group':      '...of a whole group rather than one thing',
  'ref-paren':      'a parenthesis that is only a parenthesis',
  'ref-accent':     'on a dead-key keyboard: the accent, then the letter',
  'ref-bare-mark':  'the accent key alone, with nothing joined to it',
  'ref-codes':      'A code is one to five dots and dashes: a press under a '
                  + 'quarter of a second is a dot, longer is a dash, and '
                  + '700 ms of silence ends it. Sixty-two codes exist, so a '
                  + 'table that uses many of them is easier to guess — which '
                  + 'is what the lockout, still to be built, is for.',
  'ref-packed':     'Macros are packed end to end, so the only limit is 497 '
                  + 'bytes in total: two bytes of overhead each, one byte per '
                  + 'keystroke, two per pause or repeat.',
  'ref-layout':     'The board sends key presses, not letters, so what appears '
                  + 'is decided by the keyboard layout of the machine it is '
                  + 'plugged into — which is what the setting at the top says. '
                  + 'On US International an accented character is two presses, '
                  + 'the accent and then the letter, and a bare accent mark is '
                  + 'that key followed by a space. On a plain US keyboard there is '
                  + 'no way to type one at all, so the editor says so rather '
                  + 'than typing something else.',

  'no-webhid':      'navigator.hid is missing. WebHID needs Chrome or Edge, '
                  + 'over https or localhost.',
  'hold-to-open':   'Hold the pad for four seconds to let the app read and '
                  + 'write the table. The board stays a keyboard the whole '
                  + 'time; the hold is the part a web page cannot do for '
                  + 'itself.',
  refused:          'The board said no. Hold the pad for four seconds and try '
                  + 'again — reaching the table needs somebody at the board.',
  'udev-hint':      'On Linux a hidraw device is root-only until a udev rule '
                  + 'says otherwise. web/60-digimacro.rules is that rule: copy '
                  + 'it into /etc/udev/rules.d, run "sudo udevadm control '
                  + '--reload", then unplug the board and plug it back in.',
  'connect-failed': 'could not connect: {msg}',
  connected:        'connected — firmware {fw}, protocol {proto}',
  'connected-say':  'Connected. Read the board to edit what is on it, or write '
                  + 'what is here over it.',
  'not-connected':  'not connected',
  'board-left':     'the board left — it is a keyboard again',
  'board-left-say': 'The board is a keyboard again, so it is no longer a device '
                  + 'this page can talk to. Hold the pad four seconds to bring '
                  + 'it back.',
  'wrong-magic':    'that device did not answer "DM"',
  'read-none':      'The board has no macros on it yet.',
  'read-ok':        p => `Read ${p.n} macro${p.n > 1 ? 's' : ''} off the board.`,
  'write-ok':       'Wrote {n} bytes and read every one of them back.',
  'write-mismatch': 'mismatch at {at}: wrote {wrote}, read {read}',
  'write-then-tap': 'Hold the pad for four seconds to become a keyboard again, '
                  + 'then tap a code.',
  'leave-ok':       'The board is a keyboard again. Tap a code.',
  'sample-say':     'An example of each thing a macro can do. Write it, then '
                  + 'hold the pad four seconds to become a keyboard again and '
                  + 'tap the codes.',

  'lone-backslash': 'a lone \\ at the end',
  'bad-escape':     '\\{what} means nothing',
  unclosed:         'a { with no }',
  'empty-braces':   '{} is empty',
  'pause-range':    '{{what}}: a pause runs from 0.1 to 25.5 seconds',
  'repeat-range':   '{{what}}: a repeat runs from 1 to 256',
  'group-empty':    '() has nothing in it',
  'nested-repeat':  'a count inside a group: the firmware has one mark, so the '
                  + 'inner count would take the outer one over',
  'goto-nowhere':   '{{goto {what}}}: there is no macro with that code',
  'loop-empty':     '{what}: these only jump to each other and never type or '
                  + 'pause, so the firmware would give up on them rather than '
                  + 'run them',
  'ref-goto':       'carry on with another macro, and never come back',
  'nothing-to-rep': '{{what}}: nothing to repeat yet',
  'no-key-after':   '{{what}}: no key after the +',
  'not-a-modifier': '"{what}" is not a modifier',
  'not-a-key':      '"{what}" is not a key',
  'no-key-for':     'no key for "{what}"',
  'needs-dead-keys':'"{what}" needs a keyboard with dead keys — see the '
                  + 'keyboard setting above',
  'not-a-code':     'not a code: "{what}"',
  'code-twice':     '"{what}" is used twice',
  'not-a-byte':     '"{what}" has a byte that is not a byte',
  'has-a-zero':     '"{what}" contains a zero, which would end it early',
  'too-big':        '{n} bytes of table, and only {fits} fit',
},

pt: {
  'lang-name':      'Português',
  tagline:          'Toque um código, digite uma macro.',
  keyboard:         'teclado',
  language:         'idioma',
  connect:          'Conectar',
  'connect-hint':   'ainda não conectada',
  macros:           'Macros',
  'th-code':        'Código',
  'th-types':       'Digita',
  'ph-types':       'o que este código digita',
  'add-macro':      'Adicionar macro',
  'load-example':   'Carregar o exemplo',
  budget:           '{n} de {max} bytes',
  keys:             'Teclas',
  'on-plug':        'ao conectar',
  nothing:          'nada',
  'auto-dangling':  'A macro que esta placa digitava ao ligar não está mais na '
                  + 'tabela, então ela não digitará nada. Escolha outra, ou '
                  + 'deixe como está.',
  'mods-hint':      'fica pressionado até a próxima tecla',
  'a-key':          'uma tecla',
  insert:           'inserir',
  pause:            'pausa',
  repeat:           'repetir',
  'pad-hint':       'Insere no cursor da macro que você estava editando. Um '
                  + 'modificador fica pressionado até a próxima tecla.',
  read:             'Ler da placa',
  write:            'Gravar na placa',
  'log-initial':    'Nada ainda. Conecte e segure o sensor por quatro segundos '
                  + 'quando a placa pedir — ela continua sendo teclado o tempo '
                  + 'todo, e segurar é a única coisa que uma página não '
                  + 'consegue fazer sozinha.',

  'sec-summary':    'Senha',
  'sec-none':       'Esta placa não tem senha, que é o normal e está ótimo: '
                  + 'segurar o sensor é o que mantém uma página web de fora, e '
                  + 'muitas macros não são secretas. Defina uma se a tabela '
                  + 'merecer proteção contra quem estiver com a placa na mão.',
  'sec-locked':     'Esta placa tem senha. Ela será pedida antes de ler ou '
                  + 'gravar a tabela, uma vez por sessão.',
  'sec-unlocked':   'Esta placa tem senha, e ela já foi dada. Será pedida de '
                  + 'novo depois que a janela fechar.',
  'set-password':   'Definir',
  'change-password':'Trocar',
  'clear-password': 'Remover',
  'ph-password':    'uma senha, até 12 caracteres',
  unlock:           'Destravar',
  unlocked:         'Destravada, até a janela fechar.',
  'needs-password': 'Esta placa tem senha. Digite-a na seção Senha abaixo e '
                  + 'clique em Destravar.',
  'unlock-to-change': 'Clique em Destravar com a senha atual primeiro, depois '
                  + 'digite a nova e clique em Trocar.',
  'wrong-password': 'Essa não é a senha. A placa espera um segundo a cada '
                  + 'resposta errada, que é quase tudo o que uma senha curta '
                  + 'tem a seu favor.',
  'password-empty': 'Digite uma senha primeiro, ou use Remover.',
  'password-set':   'Senha definida. É pedida uma vez por sessão, e a placa '
                  + 'continua digitando as macros sem ela.',
  'password-cleared': 'Senha removida. A placa está aberta a quem conseguir '
                  + 'segurar o sensor.',
  'ref-summary':    'O que pode entrar numa macro',
  'ref-anything':   'digitado como está',
  'ref-escapes':    'nova linha, tabulação, barra invertida',
  'ref-brace':      'uma chave { literal',
  'ref-named':      'uma tecla que não é um caractere',
  'ref-mods':       'modificadores em qualquer tecla ou caractere',
  'ref-combined':   '...combinados, em qualquer ordem',
  'ref-pause':      'espera, em segundos, de 0,1 a 25,5',
  'ref-repeat':     'onze deles ao todo, como numa expressão regular',
  'ref-group':      '...de um grupo inteiro, em vez de uma coisa só',
  'ref-paren':      'um parêntese que é só um parêntese',
  'ref-accent':     'num teclado com acentos mortos: o acento e depois a letra',
  'ref-bare-mark':  'a tecla de acento sozinha, sem nada ligado a ela',
  'ref-codes':      'Um código são de um a cinco pontos e traços: um toque de '
                  + 'menos de um quarto de segundo é ponto, mais longo é '
                  + 'traço, e 700 ms de silêncio encerram o código. Existem '
                  + 'sessenta e dois códigos, então uma tabela que use muitos '
                  + 'deles é mais fácil de adivinhar — que é para isso que '
                  + 'serve o bloqueio, ainda por construir.',
  'ref-packed':     'As macros ficam empacotadas uma após a outra, então o '
                  + 'único limite são 497 bytes no total: dois bytes de '
                  + 'cabeçalho cada, um byte por tecla, dois por pausa ou '
                  + 'repetição.',
  'ref-layout':     'A placa envia pressionamentos de tecla, não letras, então '
                  + 'o que aparece é decidido pelo layout de teclado da '
                  + 'máquina em que ela está ligada — que é o que a opção lá '
                  + 'em cima informa. No US International um caractere '
                  + 'acentuado são dois pressionamentos, o acento e depois a '
                  + 'letra, e um acento sozinho é a tecla dele seguida de espaço. '
                  + 'Num teclado US puro não há como digitar um acento, então '
                  + 'o editor avisa em vez de digitar outra coisa.',

  'no-webhid':      'navigator.hid não existe. WebHID precisa do Chrome ou do '
                  + 'Edge, por https ou localhost.',
  'hold-to-open':   'Segure o sensor por quatro segundos para o aplicativo '
                  + 'poder ler e gravar a tabela. A placa continua sendo '
                  + 'teclado o tempo todo; segurar é a parte que uma página '
                  + 'não consegue fazer sozinha.',
  refused:          'A placa recusou. Segure o sensor por quatro segundos e '
                  + 'tente de novo — chegar à tabela exige alguém na placa.',
  'udev-hint':      'No Linux um dispositivo hidraw é só do root até que uma '
                  + 'regra do udev diga o contrário. web/60-digimacro.rules é '
                  + 'essa regra: copie para /etc/udev/rules.d, rode "sudo '
                  + 'udevadm control --reload", depois desconecte e reconecte '
                  + 'a placa.',
  'connect-failed': 'não foi possível conectar: {msg}',
  connected:        'conectada — firmware {fw}, protocolo {proto}',
  'connected-say':  'Conectada. Leia a placa para editar o que está nela, ou '
                  + 'grave por cima o que está aqui.',
  'not-connected':  'não conectada',
  'board-left':     'a placa saiu — voltou a ser teclado',
  'board-left-say': 'A placa voltou a ser teclado, então não é mais um '
                  + 'dispositivo com que esta página possa falar. Segure o '
                  + 'sensor por quatro segundos para trazê-la de volta.',
  'wrong-magic':    'esse dispositivo não respondeu "DM"',
  'read-none':      'A placa ainda não tem macros.',
  'read-ok':        p => p.n > 1 ? `Lidas ${p.n} macros da placa.`
                                 : 'Lida 1 macro da placa.',
  'write-ok':       'Gravados {n} bytes, e todos lidos de volta.',
  'write-mismatch': 'divergência em {at}: gravado {wrote}, lido {read}',
  'write-then-tap': 'Segure o sensor por quatro segundos para voltar a ser '
                  + 'teclado e toque um código.',
  'leave-ok':       'A placa voltou a ser teclado. Toque um código.',
  'sample-say':     'Um exemplo de cada coisa que uma macro pode fazer. Grave, '
                  + 'depois segure o sensor por quatro segundos para voltar a '
                  + 'ser teclado e toque os códigos.',

  'lone-backslash': 'uma \\ sozinha no fim',
  'bad-escape':     '\\{what} não significa nada',
  unclosed:         'um { sem }',
  'empty-braces':   '{} está vazio',
  'pause-range':    '{{what}}: uma pausa vai de 0,1 a 25,5 segundos',
  'repeat-range':   '{{what}}: uma repetição vai de 1 a 256',
  'group-empty':    '() não tem nada dentro',
  'nested-repeat':  'uma contagem dentro de um grupo: o firmware tem uma marca '
                  + 'só, então a contagem de dentro tomaria o lugar da de fora',
  'goto-nowhere':   '{{goto {what}}}: não existe macro com esse código',
  'loop-empty':     '{what}: estas só pulam umas para as outras e nunca '
                  + 'digitam nem pausam, então o firmware desiste delas em vez '
                  + 'de executá-las',
  'ref-goto':       'continua em outra macro, e não volta',
  'nothing-to-rep': '{{what}}: não há nada para repetir ainda',
  'no-key-after':   '{{what}}: falta a tecla depois do +',
  'not-a-modifier': '"{what}" não é um modificador',
  'not-a-key':      '"{what}" não é uma tecla',
  'no-key-for':     'não há tecla para "{what}"',
  'needs-dead-keys':'"{what}" precisa de um teclado com acentos mortos — veja '
                  + 'a opção de teclado acima',
  'not-a-code':     'não é um código: "{what}"',
  'code-twice':     '"{what}" está sendo usado duas vezes',
  'not-a-byte':     '"{what}" tem um byte que não é um byte',
  'has-a-zero':     '"{what}" contém um zero, que a encerraria antes da hora',
  'too-big':        '{n} bytes de tabela, e só cabem {fits}',
},

};

let lang = 'en';

function setLang(l) { lang = STRINGS[l] ? l : 'en'; }
function getLang() { return lang; }

/* {name} is filled from the parameters. A language that has to decide
   something for itself -- a plural, usually -- gets a function instead. */
function t(key, p = {}) {
  const s = STRINGS[lang][key] ?? STRINGS.en[key];
  if (s === undefined) return key;
  if (typeof s === 'function') return s(p);
  return s.replace(/\{(\w+)\}/g, (m, k) => (k in p ? p[k] : m));
}

/* Everything with data-t in the markup, so the page can change language
   without being rebuilt. */
function applyLang(root) {
  (root || document).querySelectorAll('[data-t]').forEach(el => {
    el.textContent = t(el.dataset.t);
  });
  (root || document).querySelectorAll('[data-ph]').forEach(el => {
    el.placeholder = t(el.dataset.ph);
  });
  if (typeof document !== 'undefined')
    document.documentElement.lang = lang === 'pt' ? 'pt-BR' : 'en';
}

/* What the browser asks for, if we speak it. */
function preferredLang() {
  for (const l of (typeof navigator !== 'undefined' ? navigator.languages || [] : [])) {
    const short = l.toLowerCase().split('-')[0];
    if (STRINGS[short]) return short;
  }
  return 'en';
}

if (typeof module !== 'undefined')
  module.exports = { STRINGS, setLang, getLang, t, applyLang, preferredLang };
