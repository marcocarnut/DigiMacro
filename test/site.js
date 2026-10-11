/* The published page is one file built by tools/site.py: it must still be
   JavaScript, it must carry the firmware exactly, and nothing in it may
   reach for a file it does not carry. */
const fs = require('fs'), path = require('path'), vm = require('vm');
const { execFileSync } = require('child_process');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };

const out = path.join(process.env.TMPDIR || '/tmp', 'digimacro_site.html');
execFileSync('python3', [path.join(__dirname, '../tools/site.py'), out]);
const page = fs.readFileSync(out, 'utf8');

const scripts = [...page.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
check(scripts.length === 9, `nine scripts, not ${scripts.length}`);
for (const [i, code] of scripts.entries())
  try { new vm.Script(code); } catch (e) { check(false, `script ${i}: ${e.message}`); }
check(!/<script[^>]*\ssrc=/.test(page) && !/<link[^>]*href=/.test(page),
      'nothing loaded from beside it');

const ctx = {};
vm.runInNewContext(scripts[0] + ';this.E = EMBEDDED;', ctx);
for (const [name, text] of Object.entries(ctx.E))
  check(text === fs.readFileSync(path.join(__dirname, '../web', name), 'utf8'),
        `${name} is carried exactly`);

/* fetch() only inside fetchText, and fetchText only for what is carried. */
check((page.match(/\bfetch\(/g) || []).length === 1, 'one fetch(), in fetchText');
for (const m of page.matchAll(/fetchText\('([^']+)'\)/g))
  check(m[1] in ctx.E, `${m[1]} is in the page`);
/* ...and every call names its file, or the check above cannot see it. */
const calls = (page.match(/\bfetchText\(/g) || []).length - 1;   /* less its definition */
const named = [...page.matchAll(/\bfetchText\('[^']+'\)/g)].length;
check(calls === named, `${calls - named} fetchText() calls without a file name the test can read`);

console.log(fails ? `${fails} failed` : 'all checks passed');
process.exit(fails ? 1 : 0);
