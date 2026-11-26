// Turns the guides (.md) into standalone pages (.html) that open in any browser.
// Usage: node md2html.js <docs folder>. Handles what the guides use: headings, paragraphs, lists, tables, code
// blocks, rules, **bold**, `code`.
const fs = require('fs');
const path = require('path');
const dir = process.argv[2] || __dirname;

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const inline = (s) => esc(s)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');

function convert(md, title) {
    const lines = md.replace(/\r/g, '').split('\n');
    const out = [];
    let para = [], list = null, i = 0;
    const flush = () => {
        if (para.length) { out.push('<p>' + inline(para.join(' ')) + '</p>'); para = []; }
        if (list) { out.push('</' + list + '>'); list = null; }
    };
    while (i < lines.length) {
        const l = lines[i];
        if (/^```/.test(l)) {
            flush();
            const code = [];
            i++;
            while (i < lines.length && !/^```/.test(lines[i])) code.push(lines[i++]);
            out.push('<pre><code>' + esc(code.join('\n')) + '</code></pre>');
            i++;
            continue;
        }
        let m;
        if ((m = /^(#{1,3}) (.*)$/.exec(l))) { flush(); const n = m[1].length; out.push(`<h${n}>${inline(m[2])}</h${n}>`); i++; continue; }
        if (/^---+$/.test(l)) { flush(); out.push('<hr>'); i++; continue; }
        if (/^\|/.test(l)) {
            flush();
            const rows = [];
            while (i < lines.length && /^\|/.test(lines[i])) rows.push(lines[i++]);
            const cells = (r) => r.replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
            out.push('<table><thead><tr>' + cells(rows[0]).map((c) => '<th>' + inline(c) + '</th>').join('') + '</tr></thead><tbody>');
            for (const r of rows.slice(2)) out.push('<tr>' + cells(r).map((c) => '<td>' + inline(c) + '</td>').join('') + '</tr>');
            out.push('</tbody></table>');
            continue;
        }
        if ((m = /^(\s*)(- |\d+\. )(.*)$/.exec(l))) {
            if (para.length) { out.push('<p>' + inline(para.join(' ')) + '</p>'); para = []; }
            const kind = /\d/.test(m[2]) ? 'ol' : 'ul';
            const nested = m[1].length > 0;
            if (nested) { out.push('<ul class="sub"><li>' + inline(m[3]) + '</li></ul>'); i++; continue; }
            if (list !== kind) { if (list) out.push('</' + list + '>'); out.push('<' + kind + '>'); list = kind; }
            let text = m[3];
            while (i + 1 < lines.length && /^ {3,}\S/.test(lines[i + 1]) && !/^\s*(- |\d+\. )/.test(lines[i + 1])) text += ' ' + lines[++i].trim();
            out.push('<li>' + inline(text) + '</li>');
            i++;
            continue;
        }
        if (l.trim() === '') { flush(); i++; continue; }
        if (list) { out.push('</' + list + '>'); list = null; }
        para.push(l.trim());
        i++;
    }
    flush();
    return `<!doctype html>
<html><head><meta charset="utf-8"><title>${esc(title)}</title>
<style>
body { font: 16px/1.55 "Segoe UI", system-ui, sans-serif; max-width: 860px; margin: 32px auto; padding: 0 20px; color: #1d2230; background: #fafbfc; }
h1 { font-size: 28px; margin-top: 0; } h2 { font-size: 21px; margin-top: 32px; } h3 { font-size: 17px; }
code { font: 14px Consolas, monospace; background: #eceff4; padding: 1px 4px; border-radius: 3px; }
pre { background: #1f2430; color: #e6e9ef; padding: 12px 14px; border-radius: 6px; overflow-x: auto; }
pre code { background: none; color: inherit; padding: 0; }
table { border-collapse: collapse; margin: 12px 0; width: 100%; } th, td { border: 1px solid #d5dae3; padding: 6px 9px; text-align: left; vertical-align: top; }
th { background: #eef1f6; } hr { border: 0; border-top: 1px solid #d5dae3; margin: 28px 0; } ul.sub { margin: 0 0 0 22px; }
@media (prefers-color-scheme: dark) { body { background: #16181d; color: #e2e5ec; } code { background: #2a2f3a; } th { background: #232833; } th, td, hr { border-color: #343a47; } }
</style></head><body>
${out.join('\n')}
</body></html>
`;
}

for (const f of fs.readdirSync(dir)) {
    if (!f.endsWith('.md')) continue;
    const md = fs.readFileSync(path.join(dir, f), 'utf8');
    const title = (/^# (.*)$/m.exec(md) || [, f])[1];
    fs.writeFileSync(path.join(dir, f.replace(/\.md$/, '.html')), convert(md, title));
    console.log('ok ' + f);
}
