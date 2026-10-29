const vscode = require('vscode');
function activate(context) {
  const out = vscode.window.createOutputChannel('Hello');
  out.appendLine('activated in ' + vscode.workspace.workspaceFolders[0].name);
  const sb = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 10);
  sb.text = '$(smiley) Hello'; sb.command = 'hello.say'; sb.show();
  const diags = vscode.languages.createDiagnosticCollection('hello');
  const lint = (doc) => {
    const list = [];
    for (let i = 0; i < doc.lineCount; i++) { const t = doc.lineAt(i).text; const c = t.indexOf('TODO'); if (c >= 0) list.push(new vscode.Diagnostic(new vscode.Range(i, c, i, c + 4), 'TODO found', vscode.DiagnosticSeverity.Warning)); }
    diags.set(doc.uri, list);
  };
  context.subscriptions.push(
    vscode.commands.registerCommand('hello.say', async () => {
      const g = vscode.workspace.getConfiguration('hello').get('greeting');
      const pick = await vscode.window.showInformationMessage(g + ' from an extension!', 'Nice', 'Meh');
      out.appendLine('picked ' + pick);
      return pick;
    }),
    vscode.commands.registerCommand('hello.upper', async () => {
      const ed = vscode.window.activeTextEditor; if (!ed) return 'no editor';
      const r = ed.selection.isEmpty ? ed.document.getWordRangeAtPosition(ed.selection.active) : ed.selection;
      const ok = await ed.edit((b) => b.replace(r, ed.document.getText(r).toUpperCase()));
      return ok ? ed.document.getText(r).toUpperCase() : 'failed';
    }),
    vscode.commands.registerCommand('hello.ask', async () => {
      const name = await vscode.window.showInputBox({ prompt: 'Your name?' });
      const lang = await vscode.window.showQuickPick(['C++', 'Rust', 'Go'], { placeHolder: 'Favourite language' });
      await context.globalState.update('last', name);
      return `${name}:${lang}:${context.globalState.get('last')}`;
    }),
    vscode.languages.registerCompletionItemProvider('plaintext', {
      provideCompletionItems(doc, pos) {
        const a = new vscode.CompletionItem('astro', vscode.CompletionItemKind.Keyword); a.detail = 'A.S.T.R.O';
        const b = new vscode.CompletionItem('greet', vscode.CompletionItemKind.Snippet); b.insertText = new vscode.SnippetString('Hello ${1:name}!');
        b.documentation = new vscode.MarkdownString('Inserts a **greeting**');
        return [a, b];
      },
    }, '.'),
    vscode.languages.registerHoverProvider({ language: 'plaintext' }, {
      provideHover(doc, pos) { const r = doc.getWordRangeAtPosition(pos); return r ? new vscode.Hover(new vscode.MarkdownString('Word: `' + doc.getText(r) + '`'), r) : null; },
    }),
    vscode.languages.registerDocumentFormattingEditProvider('plaintext', {
      provideDocumentFormattingEdits(doc) { const edits = []; for (let i = 0; i < doc.lineCount; i++) { const l = doc.lineAt(i); const m = /\s+$/.exec(l.text); if (m) edits.push(vscode.TextEdit.delete(new vscode.Range(i, m.index, i, l.text.length))); } return edits; },
    }),
    vscode.workspace.onDidOpenTextDocument(lint), vscode.workspace.onDidChangeTextDocument((e) => lint(e.document)),
    sb, diags, out,
  );
  vscode.workspace.textDocuments.forEach(lint);
  vscode.someNamespaceThatDoesNotExist; vscode.window.createWebviewPanel('x', 'x', 1, {});
  return { api: 42 };
}
module.exports = { activate, deactivate() {} };
