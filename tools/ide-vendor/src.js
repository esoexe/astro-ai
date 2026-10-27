// Astro AI Workspace editor + terminal bundle (CodeMirror 6 + xterm.js).
// Built into web/vendor/ide.js as window.AstroIDE. Colours come from CSS variables (--cm-*),
// so the editor follows the app's dark / light theme without rebuilding.
import { EditorView, keymap, lineNumbers, highlightActiveLine, highlightActiveLineGutter, drawSelection, dropCursor,
  rectangularSelection, crosshairCursor, highlightSpecialChars } from '@codemirror/view';
import { EditorState, Compartment, Prec, Text } from '@codemirror/state';
import { defaultKeymap, history, historyKeymap, indentWithTab, toggleComment, undo, redo, selectAll } from '@codemirror/commands';
import { searchKeymap, highlightSelectionMatches, openSearchPanel, gotoLine, search, selectNextOccurrence } from '@codemirror/search';
import { autocompletion, completionKeymap, closeBrackets, closeBracketsKeymap, completeAnyWord, completeFromList, startCompletion } from '@codemirror/autocomplete';
import { foldGutter, foldKeymap, indentOnInput, bracketMatching, syntaxHighlighting, HighlightStyle, StreamLanguage, LanguageSupport,
  indentUnit, foldAll, unfoldAll } from '@codemirror/language';
import { tags as t } from '@lezer/highlight';
import { javascript } from '@codemirror/lang-javascript';
import { html } from '@codemirror/lang-html';
import { css } from '@codemirror/lang-css';
import { python } from '@codemirror/lang-python';
import { cpp } from '@codemirror/lang-cpp';
import { java } from '@codemirror/lang-java';
import { rust } from '@codemirror/lang-rust';
import { php } from '@codemirror/lang-php';
import { markdown } from '@codemirror/lang-markdown';
import { json } from '@codemirror/lang-json';
import { xml } from '@codemirror/lang-xml';
import { sql } from '@codemirror/lang-sql';
import { go } from '@codemirror/lang-go';
import { yaml } from '@codemirror/lang-yaml';
import { shell } from '@codemirror/legacy-modes/mode/shell';
import { ruby } from '@codemirror/legacy-modes/mode/ruby';
import { lua } from '@codemirror/legacy-modes/mode/lua';
import { toml } from '@codemirror/legacy-modes/mode/toml';
import { dockerFile } from '@codemirror/legacy-modes/mode/dockerfile';
import { MergeView, unifiedMergeView, Chunk } from '@codemirror/merge';
import { Terminal } from '@xterm/xterm';
import { FitAddon } from '@xterm/addon-fit';

const legacy = (mode) => new LanguageSupport(StreamLanguage.define(mode));

const KEYWORDS = {
  py: 'False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield print len range enumerate zip open input int str float bool list dict set tuple sorted reversed sum min max abs map filter isinstance hasattr getattr setattr super self __init__ __name__ __main__ __str__ __repr__ Exception ValueError TypeError KeyError IndexError',
  c: 'auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while include define ifdef ifndef endif printf scanf malloc free calloc realloc strlen strcpy strcmp memcpy memset NULL size_t stdio stdlib string main',
  cpp: 'alignas alignof auto bool break case catch char class const constexpr consteval constinit continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend if inline int long mutable namespace new noexcept nullptr operator override private protected public return short signed sizeof static static_cast struct switch template this throw true try typedef typename union unsigned using virtual void volatile while std cout cin endl string vector map unordered_map set array pair make_pair push_back emplace_back size begin end sort find include iostream algorithm memory unique_ptr shared_ptr make_unique main',
  java: 'abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for if implements import instanceof int interface long native new package private protected public return short static strictfp super switch synchronized this throw throws transient try void volatile while true false null String System out println printf Scanner ArrayList HashMap List Map Integer Math main args',
  go: 'break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil fmt Println Printf Sprintf len cap make append new error string int int64 float64 bool byte rune main',
  rs: 'as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while println print format vec Vec String Option Some None Result Ok Err Box main',
  php: 'abstract and array as break callable case catch class clone const continue declare default do echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile extends final finally fn for foreach function global goto if implements include instanceof insteadof interface isset list match namespace new or print private protected public readonly require return static switch throw trait try unset use var while yield true false null',
  sh: 'if then else elif fi for while until do done case esac in function return local export echo printf read cd ls mkdir rm cp mv cat grep sed awk find chmod source exit',
  rb: 'BEGIN END alias and begin break case class def defined do else elsif end ensure false for if in module next nil not or redo rescue retry return self super then true undef unless until when while yield puts print require attr_accessor',
  lua: 'and break do else elseif end false for function goto if in local nil not or repeat return then true until while print pairs ipairs require table string math',
};
const kwFor = (ext) => ({ py: 'py', c: 'c', h: 'c', cpp: 'cpp', cc: 'cpp', cxx: 'cpp', hpp: 'cpp', java: 'java', go: 'go', rs: 'rs', php: 'php', sh: 'sh', bash: 'sh', rb: 'rb', lua: 'lua' }[ext]);

function langFor(ext) {
  switch (ext) {
    case 'js': case 'mjs': case 'cjs': return javascript();
    case 'jsx': return javascript({ jsx: true });
    case 'ts': case 'mts': case 'cts': return javascript({ typescript: true });
    case 'tsx': return javascript({ jsx: true, typescript: true });
    case 'html': case 'htm': case 'vue': case 'svelte': return html();
    case 'css': case 'scss': case 'less': return css();
    case 'py': case 'pyw': return python();
    case 'c': case 'h': case 'cpp': case 'cc': case 'cxx': case 'hpp': case 'hh': case 'ino': return cpp();
    case 'java': case 'kt': case 'cs': return java();
    case 'rs': return rust();
    case 'php': return php();
    case 'md': case 'markdown': return markdown({ codeLanguages: (info) => { const l = langFor(String(info).trim().toLowerCase()); return l && l.language ? l.language : null; } });
    case 'json': case 'jsonc': case 'webmanifest': return json();
    case 'xml': case 'svg': case 'plist': return xml();
    case 'sql': return sql();
    case 'go': return go();
    case 'yml': case 'yaml': return yaml();
    case 'sh': case 'bash': case 'zsh': case 'env': return legacy(shell);
    case 'rb': return legacy(ruby);
    case 'lua': return legacy(lua);
    case 'toml': case 'ini': case 'cfg': return legacy(toml);
    case 'dockerfile': return legacy(dockerFile);
    // common aliases used in Markdown code fences
    case 'javascript': case 'node': return javascript();
    case 'typescript': return javascript({ typescript: true });
    case 'python': return python();
    case 'c++': return cpp();
    case 'rust': return rust();
    case 'shell': case 'console': return legacy(shell);
    case 'ruby': return legacy(ruby);
    default: return null;
  }
}

const highlight = HighlightStyle.define([
  { tag: [t.keyword, t.controlKeyword, t.moduleKeyword, t.operatorKeyword, t.definitionKeyword, t.modifier, t.self], color: 'var(--cm-kw)' },
  { tag: [t.string, t.special(t.string), t.regexp, t.character, t.attributeValue], color: 'var(--cm-str)' },
  { tag: [t.number, t.bool, t.null, t.atom, t.unit], color: 'var(--cm-num)' },
  { tag: [t.comment, t.lineComment, t.blockComment, t.docComment], color: 'var(--cm-com)', fontStyle: 'italic' },
  { tag: [t.function(t.variableName), t.function(t.propertyName), t.function(t.definition(t.variableName)), t.macroName], color: 'var(--cm-fn)' },
  { tag: [t.typeName, t.className, t.namespace, t.standard(t.variableName), t.standard(t.typeName)], color: 'var(--cm-type)' },
  { tag: [t.propertyName, t.attributeName, t.labelName], color: 'var(--cm-prop)' },
  { tag: [t.tagName, t.angleBracket], color: 'var(--cm-tag)' },
  { tag: [t.operator, t.punctuation, t.separator, t.bracket, t.derefOperator], color: 'var(--cm-op)' },
  { tag: [t.meta, t.processingInstruction, t.annotation, t.documentMeta], color: 'var(--cm-meta)' },
  { tag: [t.definition(t.variableName), t.definition(t.propertyName)], color: 'var(--cm-def)' },
  { tag: t.heading, color: 'var(--cm-kw)', fontWeight: '700' },
  { tag: t.emphasis, fontStyle: 'italic' },
  { tag: t.strong, fontWeight: '700' },
  { tag: t.strikethrough, textDecoration: 'line-through' },
  { tag: [t.link, t.url], color: 'var(--cm-fn)', textDecoration: 'underline' },
  { tag: t.invalid, color: 'var(--cm-err)' },
]);

const theme = EditorView.theme({
  '&': { height: '100%', fontSize: 'var(--cm-font, 13.5px)', backgroundColor: 'transparent', color: 'var(--cm-fg)' },
  '.cm-scroller': { fontFamily: 'var(--mono, ui-monospace, SFMono-Regular, Menlo, Consolas, monospace)', lineHeight: '1.65', overflow: 'auto' },
  '.cm-content': { caretColor: 'var(--cm-caret)', padding: '10px 0 40vh' },
  '.cm-cursor, .cm-dropCursor': { borderLeftColor: 'var(--cm-caret)', borderLeftWidth: '2px' },
  '&.cm-focused > .cm-scroller > .cm-selectionLayer .cm-selectionBackground, .cm-selectionBackground, .cm-content ::selection': { backgroundColor: 'var(--cm-sel) !important' },
  '.cm-gutters': { backgroundColor: 'transparent', color: 'var(--cm-gutter)', border: 'none' },
  '.cm-lineNumbers .cm-gutterElement': { padding: '0 10px 0 14px', minWidth: '34px' },
  '.cm-activeLine': { backgroundColor: 'var(--cm-active)' },
  '.cm-activeLineGutter': { backgroundColor: 'transparent', color: 'var(--cm-fg)' },
  '.cm-foldGutter .cm-gutterElement': { padding: '0 4px', cursor: 'pointer', color: 'var(--cm-gutter)' },
  '.cm-foldPlaceholder': { backgroundColor: 'var(--cm-sel)', border: 'none', color: 'var(--cm-fg)', borderRadius: '4px', padding: '0 6px', margin: '0 3px' },
  '.cm-matchingBracket': { backgroundColor: 'var(--cm-bracket)', outline: '1px solid var(--cm-bracket-line)', color: 'inherit' },
  '.cm-nonmatchingBracket': { color: 'var(--cm-err)' },
  '.cm-searchMatch': { backgroundColor: 'var(--cm-match)', outline: '1px solid var(--cm-match-line)' },
  '.cm-searchMatch.cm-searchMatch-selected': { backgroundColor: 'var(--cm-match-sel)' },
  '.cm-selectionMatch': { backgroundColor: 'var(--cm-selmatch)' },
  '.cm-tooltip': { backgroundColor: 'var(--cm-pop)', color: 'var(--cm-fg)', border: '1px solid var(--cm-line)', borderRadius: '10px', boxShadow: '0 12px 32px rgba(0,0,0,.35)', overflow: 'hidden' },
  '.cm-tooltip-autocomplete > ul': { fontFamily: 'var(--mono, monospace)', maxHeight: '260px' },
  '.cm-tooltip-autocomplete > ul > li': { padding: '3px 10px !important' },
  '.cm-tooltip-autocomplete > ul > li[aria-selected]': { backgroundColor: 'var(--cm-pop-sel)', color: 'var(--cm-fg)' },
  '.cm-completionDetail': { color: 'var(--cm-gutter)', fontStyle: 'normal', marginLeft: '8px' },
  '.cm-completionMatchedText': { textDecoration: 'none', color: 'var(--cm-kw)', fontWeight: '700' },
  '.cm-panels': { backgroundColor: 'var(--cm-pop)', color: 'var(--cm-fg)', borderColor: 'var(--cm-line)' },
  '.cm-panels-top': { borderBottom: '1px solid var(--cm-line)' },
  '.cm-mergeView, .cm-merge-a, .cm-merge-b': { height: '100%' },
  '.cm-changedLine': { backgroundColor: 'var(--cm-chg) !important' },
  '.cm-deletedChunk': { backgroundColor: 'var(--cm-del) !important' },
  '.cm-insertedLine, .cm-merge-b .cm-changedLine': { backgroundColor: 'var(--cm-add) !important' },
  '.cm-merge-a .cm-changedLine, .cm-deletedLine': { backgroundColor: 'var(--cm-del) !important' },
  '.cm-changedText': { background: 'var(--cm-chg-text) !important' },
  '.cm-collapsedLines': { background: 'var(--cm-active)', color: 'var(--cm-gutter)' },
});

function foldMarker(open) {
  const s = document.createElement('span');
  s.className = `cm-fold-ic${open ? '' : ' closed'}`;
  s.textContent = open ? '⌄' : '›';
  s.title = open ? 'Fold' : 'Unfold';
  return s;
}

function langExtensions(ext) {
  const out = [];
  const lang = langFor(ext);
  if (lang) out.push(lang);
  out.push(EditorState.languageData.of(() => [{ autocomplete: completeAnyWord }]));
  const kw = KEYWORDS[kwFor(ext)];
  if (kw) out.push(EditorState.languageData.of(() => [{ autocomplete: completeFromList(kw.split(' ').map((label) => ({ label, type: 'keyword' }))) }]));
  return out;
}

function createEditor(parent, handlers = {}) {
  const states = new Map();
  const scrolls = new Map();
  let key = null;
  let wrap = Boolean(handlers.wrap);
  const wrapC = new Compartment();
  const roC = new Compartment();
  const fire = (name, ...args) => { try { if (handlers[name]) handlers[name](...args); } catch (e) { console.error(e); } };

  function extensions(ext) {
    return [
      lineNumbers(), highlightActiveLineGutter(), highlightSpecialChars(), history(), foldGutter({ markerDOM: foldMarker }),
      drawSelection(), dropCursor(), EditorState.allowMultipleSelections.of(true), indentOnInput(), syntaxHighlighting(highlight),
      bracketMatching(), closeBrackets(), autocompletion({ activateOnTyping: true, maxRenderedOptions: 80, icons: true }),
      rectangularSelection(), crosshairCursor(), highlightActiveLine(), highlightSelectionMatches(), search({ top: true }),
      indentUnit.of(ext === 'go' || ext === 'makefile' ? '\t' : ext === 'py' || ext === 'java' || ext === 'rs' || ext === 'php' || ext === 'cs' ? '    ' : '  '),
      EditorState.tabSize.of(4), wrapC.of(wrap ? EditorView.lineWrapping : []), roC.of(EditorState.readOnly.of(false)), theme,
      ...langExtensions(ext),
      Prec.highest(keymap.of([
        { key: 'Mod-s', preventDefault: true, run: () => { fire('onSave'); return true; } },
        { key: 'Mod-Enter', preventDefault: true, run: () => { fire('onRun'); return true; } },
        { key: 'Mod-/', run: toggleComment },
        { key: 'Mod-h', preventDefault: true, run: (v) => { openSearchPanel(v); setTimeout(() => v.dom.querySelector('.cm-search input[name=replace]')?.focus(), 0); return true; } },
        { key: 'Alt-z', preventDefault: true, run: () => { api.toggleWrap(); fire('onWrap', wrap); return true; } },
        { key: 'Mod-d', preventDefault: true, run: selectNextOccurrence },
      ])),
      keymap.of([...closeBracketsKeymap, ...defaultKeymap, ...searchKeymap, ...historyKeymap, ...foldKeymap, ...completionKeymap, indentWithTab, { key: 'Mod-g', run: gotoLine }]),
      EditorView.updateListener.of((u) => {
        if (u.docChanged) fire('onChange', key, u.state.doc.toString());
        if (u.selectionSet || u.docChanged) {
          const sel = u.state.selection.main; const line = u.state.doc.lineAt(sel.head);
          fire('onCursor', { line: line.number, col: sel.head - line.from + 1, cursors: u.state.selection.ranges.length, selected: sel.to - sel.from, lines: u.state.doc.lines });
        }
      }),
    ];
  }

  const view = new EditorView({ parent, state: EditorState.create({ doc: '', extensions: extensions('txt') }) });

  const api = {
    view,
    // Shows file `k`. Each file keeps its own state (undo history, selection, folds).
    open(k, doc, ext, opts = {}) {
      if (key !== null) { states.set(key, view.state); scrolls.set(key, [view.scrollDOM.scrollTop, view.scrollDOM.scrollLeft]); }
      key = k;
      let st = states.get(k);
      if (!st || opts.reload) st = EditorState.create({ doc, extensions: extensions(ext) });
      view.setState(st);
      if (view.state.doc.toString() !== doc) view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: doc } });
      view.dispatch({ effects: [roC.reconfigure(EditorState.readOnly.of(Boolean(opts.readOnly))), wrapC.reconfigure(wrap ? EditorView.lineWrapping : [])] });
      const sc = scrolls.get(k);
      requestAnimationFrame(() => { view.scrollDOM.scrollTop = sc ? sc[0] : 0; view.scrollDOM.scrollLeft = sc ? sc[1] : 0; });
      const sel = view.state.selection.main; const line = view.state.doc.lineAt(sel.head);
      fire('onCursor', { line: line.number, col: sel.head - line.from + 1, cursors: view.state.selection.ranges.length, selected: sel.to - sel.from, lines: view.state.doc.lines });
    },
    // Replaces the text of the open file as one undoable change (used when the AI edits it).
    setDoc(doc) { if (view.state.doc.toString() !== doc) view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: doc } }); },
    forget(k) { states.delete(k); scrolls.delete(k); if (key === k) key = null; },
    rename(from, to) { if (states.has(from)) { states.set(to, states.get(from)); states.delete(from); } if (key === from) key = to; },
    current: () => key,
    getDoc: () => view.state.doc.toString(),
    focus: () => view.focus(),
    find: () => { openSearchPanel(view); },
    replace: () => { openSearchPanel(view); setTimeout(() => view.dom.querySelector('.cm-search input[name=replace]')?.focus(), 0); },
    gotoLine(line, col = 1, len = 0) {
      const doc = view.state.doc;
      const l = doc.line(Math.min(Math.max(1, line | 0), doc.lines));
      const from = Math.min(l.from + Math.max(0, col - 1), l.to);
      view.dispatch({ selection: { anchor: from, head: Math.min(from + len, l.to) }, effects: EditorView.scrollIntoView(from, { y: 'center' }) });
      view.focus();
    },
    foldAll: () => foldAll(view),
    unfoldAll: () => unfoldAll(view),
    toggleWrap() { wrap = !wrap; view.dispatch({ effects: wrapC.reconfigure(wrap ? EditorView.lineWrapping : []) }); return wrap; },
    get wrap() { return wrap; },
    undo: () => undo(view),
    redo: () => redo(view),
    selectAll: () => selectAll(view),
    complete: () => startCompletion(view),
    comment: () => toggleComment(view),
    destroy: () => view.destroy(),
  };
  return api;
}

const readOnly = (ext) => [lineNumbers(), syntaxHighlighting(highlight), theme, EditorView.editable.of(false), EditorState.readOnly.of(true), ...(langFor(ext) ? [langFor(ext)] : [])];

// Side-by-side (or unified on narrow screens) read-only diff between two versions of a file.
function createDiff(parent, { a = '', b = '', ext = '', unified = false }) {
  if (unified) {
    return new EditorView({ parent, state: EditorState.create({ doc: b, extensions: [...readOnly(ext), unifiedMergeView({ original: a, mergeControls: false, gutter: true, collapseUnchanged: { margin: 3, minSize: 8 } })] }) });
  }
  return new MergeView({ parent, a: { doc: a, extensions: readOnly(ext) }, b: { doc: b, extensions: readOnly(ext) }, gutter: true, highlightChanges: true, collapseUnchanged: { margin: 3, minSize: 8 } });
}

// Lines added / removed between two texts.
function diffStats(a = '', b = '') {
  const A = Text.of(String(a).split('\n')); const B = Text.of(String(b).split('\n'));
  let added = 0, removed = 0;
  for (const c of Chunk.build(A, B)) {
    if (c.toA > c.fromA) removed += A.lineAt(Math.max(c.fromA, c.toA - 1)).number - A.lineAt(c.fromA).number + 1;
    if (c.toB > c.fromB) added += B.lineAt(Math.max(c.fromB, c.toB - 1)).number - B.lineAt(c.fromB).number + 1;
  }
  return { added, removed };
}

export { createEditor, createDiff, diffStats, langFor, Terminal, FitAddon };
