// Renders the GitHub wiki (a clone) into styled, OG-tagged static pages under
// docs/wiki/, matching the landing page (docs/index.html) look. Deterministic:
// same input -> same output. The wiki is the source of truth; the sync workflow
// re-runs this on wiki edits.
//
//   WIKI_DIR=/path/to/wiki.git-clone  node build.mjs
//
// No Jekyll (repo keeps .nojekyll): command samples contain {{ }} that Jekyll
// would try to interpret.

import fs from "node:fs";
import path from "node:path";
import MarkdownIt from "markdown-it";

const WIKI_DIR = process.env.WIKI_DIR || "/tmp/impwiki";
const OUT_DIR = process.env.OUT_DIR || path.resolve("docs/wiki");
const SITE = "https://mangokingtw.github.io/ImeModePersistence";
const REPO = "https://github.com/mangokingTW/ImeModePersistence";
const OG_IMAGE = `${SITE}/og-image.jpg`;
const ICON = "https://raw.githubusercontent.com/mangokingTW/ImeModePersistence/main/assets/app_icon_trim.png";

const LANGS = [
  { suffix: "", name: "繁體中文", code: "zh-Hant" },
  { suffix: "-English", name: "English", code: "en" },
  { suffix: "-Simplified", name: "简体中文", code: "zh-Hans" },
  { suffix: "-Japanese", name: "日本語", code: "ja" },
  { suffix: "-Korean", name: "한국어", code: "ko" },
];
// Longest base first so "Helldivers-2" is matched before any shorter prefix.
const TOPICS = [
  { base: "Sidecar-Helper-Architecture", title: "Sidecar Helper" },
  { base: "Helldivers-2", title: "Helldivers 2" },
  { base: "Similar-tools", title: "Similar tools · 類似工具" },
  { base: "Home", title: "Overview · 總覽" },
];

const md = new MarkdownIt({ html: true, linkify: true, typographer: false });

function listPages() {
  return fs
    .readdirSync(WIKI_DIR)
    .filter((f) => f.endsWith(".md") && !f.startsWith("_"))
    .map((f) => f.slice(0, -3))
    .sort();
}

function classify(slug) {
  for (const t of TOPICS) {
    for (const l of LANGS) {
      if (slug === t.base + l.suffix) return { topic: t, lang: l };
    }
  }
  return { topic: { base: slug, title: slug }, lang: { name: "", code: "en" } };
}

function esc(s) {
  return s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}

// First real paragraph -> plain text, for og:description.
function description(mdSrc) {
  const lines = mdSrc.split(/\r?\n/);
  for (let raw of lines) {
    const line = raw.trim();
    if (!line) continue;
    if (line.startsWith("#") || line.startsWith(">") || line.startsWith("<")) continue;
    if (line.startsWith("|") || line.startsWith("```")) continue;
    // skip the language-switcher line (links joined by · )
    if (/\]\(/.test(line) && line.includes("·")) continue;
    let t = line
      .replace(/!\[[^\]]*\]\([^)]*\)/g, "")
      .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
      .replace(/[*_`>#]/g, "")
      .replace(/\s+/g, " ")
      .trim();
    if (t.length > 8) return t.length > 155 ? t.slice(0, 152) + "…" : t;
  }
  return "IME Mode Persistence — carry your input mode across windows and bind apps to a fixed input language.";
}

function rewriteLinks(html, slugSet) {
  // href="Slug" or href="Slug#anchor" -> href="Slug.html[#anchor]" for wiki pages.
  return html.replace(/href="([^":/#?]+)(#[^"]*)?"/g, (m, target, anchor) => {
    if (slugSet.has(target)) return `href="${target}.html${anchor || ""}"`;
    return m;
  });
}

function page({ title, ogTitle, desc, url, bodyHtml, editUrl, lang }) {
  return `<!DOCTYPE html>
<html lang="${lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>
<meta name="description" content="${esc(desc)}">
<meta property="og:type" content="article">
<meta property="og:site_name" content="IME Mode Persistence">
<meta property="og:url" content="${esc(url)}">
<meta property="og:title" content="${esc(ogTitle)}">
<meta property="og:description" content="${esc(desc)}">
<meta property="og:image" content="${OG_IMAGE}">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="${esc(ogTitle)}">
<meta name="twitter:description" content="${esc(desc)}">
<meta name="twitter:image" content="${OG_IMAGE}">
<link rel="canonical" href="${esc(url)}">
<link rel="icon" href="${ICON}">
<style>
  :root{
    --bg:#ffffff; --bg-soft:#f6f7fb; --card:#ffffff; --border:#e6e8ef;
    --fg:#1a1c23; --muted:#5b6072; --accent:#4f46e5; --accent-fg:#ffffff;
    --code-bg:#f2f3f8; --shadow:0 1px 2px rgba(20,22,40,.06),0 8px 24px rgba(20,22,40,.08);
    --radius:14px;
  }
  @media (prefers-color-scheme:dark){
    :root{
      --bg:#0d0f16; --bg-soft:#12151f; --card:#161a26; --border:#242a3a;
      --fg:#e8eaf2; --muted:#9aa1b6; --accent:#8b8bff; --accent-fg:#0d0f16;
      --code-bg:#1b2030; --shadow:0 1px 2px rgba(0,0,0,.4),0 10px 30px rgba(0,0,0,.35);
    }
  }
  *{box-sizing:border-box}
  html{scroll-behavior:smooth}
  body{margin:0; background:var(--bg); color:var(--fg);
    font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang TC","Noto Sans TC","Microsoft JhengHei",Roboto,Helvetica,Arial,sans-serif;
    line-height:1.7; -webkit-font-smoothing:antialiased}
  a{color:var(--accent); text-decoration:none}
  a:hover{text-decoration:underline}
  header.nav{position:sticky; top:0; z-index:10; backdrop-filter:saturate(180%) blur(10px);
    background:color-mix(in srgb,var(--bg) 82%, transparent); border-bottom:1px solid var(--border)}
  .nav .wrap{max-width:900px; margin:0 auto; padding:0 20px; display:flex; align-items:center; gap:12px; height:56px}
  .nav .brand{display:flex; align-items:center; gap:9px; font-weight:700; color:var(--fg)}
  .nav .brand img{width:24px; height:24px}
  .nav .spacer{flex:1}
  .nav .pill{border:1px solid var(--border); background:var(--card); color:var(--fg);
    border-radius:999px; padding:5px 12px; font-size:.85rem; font-weight:600}
  .nav .pill:hover{border-color:var(--accent); text-decoration:none}
  main{max-width:820px; margin:0 auto; padding:34px 20px 8px}
  .content h1{font-size:1.9rem; line-height:1.2; letter-spacing:-.02em; margin:.2em 0 .5em}
  .content h1 img{vertical-align:middle}
  .content h2{font-size:1.4rem; margin:1.6em 0 .5em; padding-bottom:.2em; border-bottom:1px solid var(--border)}
  .content h3{font-size:1.15rem; margin:1.3em 0 .4em}
  .content h4{font-size:1rem; margin:1.2em 0 .3em}
  .content p, .content li{font-size:1.02rem}
  .content img{max-width:100%; height:auto; border-radius:10px}
  .content code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; font-size:.9em;
    background:var(--code-bg); padding:.15em .4em; border-radius:6px}
  .content pre{background:var(--code-bg); border:1px solid var(--border); border-radius:10px;
    padding:14px 16px; overflow:auto}
  .content pre code{background:none; padding:0; font-size:.86rem}
  .content table{border-collapse:collapse; width:100%; margin:1em 0; font-size:.95rem; display:block; overflow:auto}
  .content th,.content td{border:1px solid var(--border); padding:8px 12px; text-align:left; vertical-align:top}
  .content th{background:var(--bg-soft)}
  .content blockquote{margin:1em 0; padding:.4em 1.1em; color:var(--muted);
    background:var(--bg-soft); border-left:3px solid var(--accent); border-radius:0 10px 10px 0}
  .content blockquote h3{margin-top:.3em}
  .content hr{border:0; border-top:1px solid var(--border); margin:2em 0}
  .content ul,.content ol{padding-left:1.4em}
  footer{max-width:820px; margin:0 auto; padding:24px 20px 46px; color:var(--muted); font-size:.9rem;
    border-top:1px solid var(--border); margin-top:30px; display:flex; gap:18px; flex-wrap:wrap}
  footer a{color:var(--muted)}
</style>
</head>
<body>
<header class="nav"><div class="wrap">
  <a class="brand" href="../"><img src="${ICON}" alt="">IME Mode Persistence</a>
  <span class="spacer"></span>
  <a class="pill" href="./">Guides · 說明</a>
  <a class="pill" href="${REPO}">GitHub</a>
</div></header>
<main><article class="content">
${bodyHtml}
</article></main>
<footer>
  <a href="../">← 介紹頁 Home</a>
  <a href="./">所有指南 All guides</a>
  <a href="${esc(editUrl)}">在 Wiki 編輯 · Edit on wiki</a>
</footer>
</body>
</html>
`;
}

function indexPage(pagesByTopic) {
  let sections = "";
  for (const t of TOPICS) {
    const items = pagesByTopic.get(t.base);
    if (!items || !items.length) continue;
    const links = items
      .map((p) => `<a class="lang" href="${p.slug}.html">${esc(p.lang.name || p.slug)}</a>`)
      .join("");
    sections += `<div class="card"><h3>${esc(t.title)}</h3><div class="langs">${links}</div></div>\n`;
  }
  const desc = "IME Mode Persistence guides — setup, per-app bindings, the Sidecar Helper, Helldivers 2, and comparisons.";
  return `<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Guides · 說明文件 — IME Mode Persistence</title>
<meta name="description" content="${esc(desc)}">
<meta property="og:type" content="website">
<meta property="og:site_name" content="IME Mode Persistence">
<meta property="og:url" content="${SITE}/wiki/">
<meta property="og:title" content="Guides · 說明文件 — IME Mode Persistence">
<meta property="og:description" content="${esc(desc)}">
<meta property="og:image" content="${OG_IMAGE}">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:image" content="${OG_IMAGE}">
<link rel="canonical" href="${SITE}/wiki/">
<link rel="icon" href="${ICON}">
<style>
  :root{--bg:#ffffff;--bg-soft:#f6f7fb;--card:#ffffff;--border:#e6e8ef;--fg:#1a1c23;--muted:#5b6072;--accent:#4f46e5;--shadow:0 1px 2px rgba(20,22,40,.06),0 8px 24px rgba(20,22,40,.08);--radius:14px}
  @media (prefers-color-scheme:dark){:root{--bg:#0d0f16;--bg-soft:#12151f;--card:#161a26;--border:#242a3a;--fg:#e8eaf2;--muted:#9aa1b6;--accent:#8b8bff;--shadow:0 1px 2px rgba(0,0,0,.4),0 10px 30px rgba(0,0,0,.35)}}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);
    font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang TC","Noto Sans TC","Microsoft JhengHei",Roboto,Helvetica,Arial,sans-serif;line-height:1.65}
  a{color:var(--accent);text-decoration:none}
  a:hover{text-decoration:underline}
  header.nav{position:sticky;top:0;z-index:10;backdrop-filter:saturate(180%) blur(10px);
    background:color-mix(in srgb,var(--bg) 82%,transparent);border-bottom:1px solid var(--border)}
  .nav .wrap{max-width:900px;margin:0 auto;padding:0 20px;display:flex;align-items:center;gap:12px;height:56px}
  .nav .brand{display:flex;align-items:center;gap:9px;font-weight:700;color:var(--fg)}
  .nav .brand img{width:24px;height:24px}
  .nav .spacer{flex:1}
  .nav .pill{border:1px solid var(--border);background:var(--card);color:var(--fg);border-radius:999px;padding:5px 12px;font-size:.85rem;font-weight:600}
  main{max-width:900px;margin:0 auto;padding:40px 20px}
  h1{font-size:1.9rem;letter-spacing:-.02em;margin:.1em 0 .1em}
  .lead{color:var(--muted);margin:0 0 26px}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}
  .card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:20px;box-shadow:var(--shadow)}
  .card h3{margin:.1em 0 .7em;font-size:1.1rem}
  .langs{display:flex;flex-wrap:wrap;gap:8px}
  .langs .lang{border:1px solid var(--border);border-radius:999px;padding:5px 12px;font-size:.9rem;color:var(--fg)}
  .langs .lang:hover{border-color:var(--accent);text-decoration:none}
  footer{max-width:900px;margin:0 auto;padding:10px 20px 46px;color:var(--muted);font-size:.9rem}
</style>
</head>
<body>
<header class="nav"><div class="wrap">
  <a class="brand" href="../"><img src="${ICON}" alt="">IME Mode Persistence</a>
  <span class="spacer"></span>
  <a class="pill" href="${REPO}">GitHub</a>
</div></header>
<main>
  <h1>Guides · 說明文件</h1>
  <p class="lead">${esc(desc)}</p>
  <div class="grid">
${sections}  </div>
</main>
<footer><a href="../">← 介紹頁 Home</a></footer>
</body>
</html>
`;
}

// ---- build ----
const slugs = listPages();
const slugSet = new Set(slugs);
fs.mkdirSync(OUT_DIR, { recursive: true });

const pagesByTopic = new Map();
for (const t of TOPICS) pagesByTopic.set(t.base, []);

for (const slug of slugs) {
  const src = fs.readFileSync(path.join(WIKI_DIR, `${slug}.md`), "utf8");
  const { topic, lang } = classify(slug);
  const bodyHtml = rewriteLinks(md.render(src), slugSet);
  const desc = description(src);
  const nice = `${topic.title} — ${lang.name}`.replace(/ — $/, "");
  const html = page({
    title: `${nice} · IME Mode Persistence`,
    ogTitle: `${nice} · IME Mode Persistence`,
    desc,
    url: `${SITE}/wiki/${slug}.html`,
    bodyHtml,
    editUrl: `${REPO}/wiki/${slug}`,
    lang: lang.code,
  });
  fs.writeFileSync(path.join(OUT_DIR, `${slug}.html`), html);
  if (pagesByTopic.has(topic.base)) pagesByTopic.get(topic.base).push({ slug, lang });
}

// stable lang order within each topic
for (const [, arr] of pagesByTopic) {
  arr.sort((a, b) => LANGS.findIndex((l) => l.suffix === a.lang.suffix) - LANGS.findIndex((l) => l.suffix === b.lang.suffix));
}

fs.writeFileSync(path.join(OUT_DIR, "index.html"), indexPage(pagesByTopic));

console.log(`Rendered ${slugs.length} pages + index -> ${OUT_DIR}`);
