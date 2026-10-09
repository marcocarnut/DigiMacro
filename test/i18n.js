/* Checks web/i18n.js: that the languages say the same things.
 *
 * The failure this exists to catch is a sentence half in the wrong language.
 * A missing translation is invisible in use -- t() falls back to English, so
 * the page still works and reads as if nobody noticed -- which is exactly why
 * it wants a test rather than an eye.
 */
const i18n = require('../web/i18n.js');
const table = require('../web/table.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };

const langs = Object.keys(i18n.STRINGS);
console.log(`1. ${langs.length} languages: ${langs.join(', ')}`);
check(langs.includes('en') && langs.includes('pt'), 'English and Portuguese');

console.log('2. every language says everything');
const en = Object.keys(i18n.STRINGS.en).sort();
for (const l of langs) {
  const keys = Object.keys(i18n.STRINGS[l]).sort();
  const missing = en.filter(k => !keys.includes(k));
  const extra = keys.filter(k => !en.includes(k));
  check(missing.length === 0, `${l} is missing: ${missing.join(', ')}`);
  check(extra.length === 0, `${l} has keys English does not: ${extra.join(', ')}`);
}

console.log('3. every message the encoder can throw is translated');
/* table.js keeps English defaults so the tests can read them; the app hands
   it a translator. A key here with no entry there would fall back silently. */
for (const key of Object.keys(table.MSG))
  check(en.includes(key), `the encoder can say "${key}", which no language has`);

console.log('4. the parameters survive translation');
for (const l of langs) {
  i18n.setLang(l);
  const budget = i18n.t('budget', { n: 44, max: 497 });
  check(/44/.test(budget) && /497/.test(budget), `${l}: budget lost a number`);
  /* The lockout's lines are mostly numbers, and a translation that dropped
     one would mislead about exactly the thing it exists to explain. */
  const counts = i18n.t('lock-counts', { misses: 31, softs: 32, cleared: 33 });
  check(/31/.test(counts) && /32/.test(counts) && /33/.test(counts),
        `${l}: lock-counts lost a number`);
  const odds = i18n.t('lock-odds', { tries: 41, wait: '42 min', soft: 43,
                                      hard: 1, n: 44, pct: 45 });
  check([41, 42, 43, 44, 45].every(n => odds.includes(String(n))),
        `${l}: lock-odds lost a number`);
  /* ...and says one as one rather than "every 1" */
  const one = i18n.t('lock-odds', { tries: 1, wait: '1 min', soft: 1,
                                     hard: 1, n: 3, pct: 5 });
  check(!/ 1 (wrong|erros)/.test(one) && one !== odds,
        `${l}: lock-odds should say one wrong code as one`);
  check(/Blah/.test(i18n.t('not-a-key', { what: 'Blah' })),
        `${l}: not-a-key lost what it was about`);
  /* {what} inside braces, which is how the brace forms are quoted back */
  check(/\{x300\}/.test(i18n.t('repeat-range', { what: 'x300' })),
        `${l}: repeat-range should quote the whole {x300}`);
}
i18n.setLang('en');

console.log('5. the encoder speaks whatever it is handed');
table.setMessages((key, params) => `TRANSLATED:${key}:${params.what || ''}`);
let msg = null;
try { table.encodeMacro('{Blah}'); } catch (e) { msg = e.message; }
check(msg === 'TRANSLATED:not-a-key:Blah', `got ${JSON.stringify(msg)}`);
table.setMessages(null);
msg = null;
try { table.encodeMacro('{Blah}'); } catch (e) { msg = e.message; }
check(/is not a key/.test(msg), `English should come back: ${JSON.stringify(msg)}`);

console.log('6. Portuguese wherever the browser lists it');
for (const [langs, want] of [[['pt-BR'], 'pt'], [['en-US', 'pt-BR'], 'pt'],
                             [['en-US', 'en', 'pt'], 'pt'], [['fr-FR', 'en-GB'], 'en'],
                             [['en-US'], 'en'], [['de'], 'en'], [[], 'en']])
  check(i18n.preferredLang(langs) === want,
        `${JSON.stringify(langs)} -> ${i18n.preferredLang(langs)}, not ${want}`);

console.log(fails ? '\nSOME CHECKS FAILED' : '\nall checks passed');
process.exit(fails ? 1 : 0);
