const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");

function makoPath() {
  return vscode.workspace.getConfiguration("mako").get("path", "mako");
}

function lspEnabled() {
  return vscode.workspace.getConfiguration("mako").get("lsp.enabled", true);
}

function debugAdapter() {
  return vscode.workspace.getConfiguration("mako").get("debug.adapter", "lldb");
}

function workspaceCwd() {
  const folders = vscode.workspace.workspaceFolders;
  if (folders && folders.length > 0) {
    return folders[0].uri.fsPath;
  }
  const editor = vscode.window.activeTextEditor;
  if (editor) {
    return require("path").dirname(editor.document.uri.fsPath);
  }
  return process.cwd();
}

function activeTarget(defaultTarget) {
  const editor = vscode.window.activeTextEditor;
  if (editor && editor.document.languageId === "mako") {
    return editor.document.uri.fsPath;
  }
  return defaultTarget;
}

function runInTerminal(command, args) {
  const terminal = vscode.window.createTerminal({
    name: "Mako",
    cwd: workspaceCwd()
  });
  const quoted = [command, ...args].map(shellQuote).join(" ");
  terminal.show();
  terminal.sendText(quoted);
}

function shellQuote(value) {
  if (/^[A-Za-z0-9_./:=@+-]+$/.test(value)) {
    return value;
  }
  return `"${value.replace(/(["\\$`])/g, "\\$1")}"`;
}

function binarySuffix() {
  return process.platform === "win32" ? ".exe" : "";
}

function defaultProgramForSource(source) {
  const parsed = path.parse(source || "main.mko");
  return path.join(workspaceCwd(), `${parsed.name}${binarySuffix()}`);
}

function normalizeDebugArgs(args) {
  return Array.isArray(args) ? args.map((arg) => String(arg)) : [];
}

function nativeDebugConfig(config) {
  const source = config.source || activeTarget(path.join(workspaceCwd(), "main.mko"));
  const adapter = config.adapter || debugAdapter();
  const program = config.program || defaultProgramForSource(source);
  const cwd = config.cwd || workspaceCwd();
  const args = normalizeDebugArgs(config.args);
  const preLaunchTask = config.preLaunchTask || "Mako: Build Active File";

  if (adapter === "cppdbg") {
    return {
      type: "cppdbg",
      request: "launch",
      name: config.name || "Mako: Debug",
      program,
      args,
      cwd,
      stopAtEntry: config.stopAtEntry || false,
      externalConsole: config.externalConsole || false,
      MIMode: process.platform === "darwin" ? "lldb" : "gdb",
      preLaunchTask
    };
  }

  return {
    type: "lldb",
    request: "launch",
    name: config.name || "Mako: Debug",
    program,
    args,
    cwd,
    preLaunchTask
  };
}

class MakoDebugConfigurationProvider {
  resolveDebugConfiguration(_folder, config) {
    return nativeDebugConfig(config || {});
  }
}

function debugActiveFile() {
  const source = activeTarget("");
  if (!source) {
    vscode.window.showWarningMessage("Open a .mko file before starting Mako debug.");
    return;
  }
  return vscode.debug.startDebugging(undefined, nativeDebugConfig({
    name: "Mako: Debug active file",
    source,
    program: defaultProgramForSource(source),
    cwd: workspaceCwd()
  }));
}

function registerCommand(context, name, args) {
  context.subscriptions.push(
    vscode.commands.registerCommand(name, () => runInTerminal(makoPath(), args()))
  );
}

class MakoLanguageClient {
  constructor(context) {
    this.context = context;
    this.proc = null;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.ready = false;
  }

  start() {
    if (!lspEnabled() || this.proc) {
      return;
    }
    const command = makoPath();
    this.proc = cp.spawn(command, ["lsp"], {
      cwd: workspaceCwd(),
      stdio: ["pipe", "pipe", "pipe"]
    });
    this.proc.stdout.on("data", (chunk) => this.onData(chunk));
    this.proc.stderr.on("data", (chunk) => {
      const text = chunk.toString().trim();
      if (text) {
        console.warn(`mako lsp: ${text}`);
      }
    });
    this.proc.on("exit", () => {
      this.proc = null;
      this.ready = false;
      for (const { reject } of this.pending.values()) {
        reject(new Error("mako lsp exited"));
      }
      this.pending.clear();
    });
    this.request("initialize", {
      processId: process.pid,
      rootUri: firstWorkspaceUri(),
      capabilities: {}
    })
      .then(() => {
        this.ready = true;
        this.notify("initialized", {});
        for (const doc of vscode.workspace.textDocuments) {
          if (doc.languageId === "mako") {
            this.didOpen(doc);
          }
        }
      })
      .catch((err) => {
        vscode.window.showWarningMessage(`Mako LSP failed to start: ${err.message}`);
      });
  }

  stop() {
    if (!this.proc) {
      return;
    }
    const proc = this.proc;
    this.proc = null;
    this.ready = false;
    for (const { reject } of this.pending.values()) {
      reject(new Error("mako lsp stopped"));
    }
    this.pending.clear();
    try {
      const shutdown = JSON.stringify({ jsonrpc: "2.0", id: this.nextId++, method: "shutdown", params: {} });
      proc.stdin.write(`Content-Length: ${Buffer.byteLength(shutdown)}\r\n\r\n${shutdown}`);
      const exit = JSON.stringify({ jsonrpc: "2.0", method: "exit", params: {} });
      proc.stdin.write(`Content-Length: ${Buffer.byteLength(exit)}\r\n\r\n${exit}`);
    } catch (_err) {
      proc.kill();
    }
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const sep = this.buffer.indexOf("\r\n\r\n");
      if (sep < 0) {
        return;
      }
      const header = this.buffer.slice(0, sep).toString();
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        this.buffer = this.buffer.slice(sep + 4);
        continue;
      }
      const len = Number(match[1]);
      const start = sep + 4;
      const end = start + len;
      if (this.buffer.length < end) {
        return;
      }
      const body = this.buffer.slice(start, end).toString();
      this.buffer = this.buffer.slice(end);
      this.handleMessage(body);
    }
  }

  handleMessage(body) {
    let msg;
    try {
      msg = JSON.parse(body);
    } catch (err) {
      console.warn(`mako lsp: invalid JSON: ${err.message}`);
      return;
    }
    if (Object.prototype.hasOwnProperty.call(msg, "id")) {
      const pending = this.pending.get(msg.id);
      if (pending) {
        this.pending.delete(msg.id);
        if (msg.error) {
          pending.reject(new Error(msg.error.message || "LSP error"));
        } else {
          pending.resolve(msg.result);
        }
      }
      return;
    }
    if (msg.method === "textDocument/publishDiagnostics") {
      const params = msg.params || {};
      const diagnostics = (params.diagnostics || []).map((diag) => {
        const diagnostic = new vscode.Diagnostic(
          fromLspRange(diag.range),
          diag.message || "",
          fromLspSeverity(diag.severity)
        );
        diagnostic.source = diag.source || "mako";
        return diagnostic;
      });
      this.context.diagnostics.set(vscode.Uri.parse(params.uri), diagnostics);
    }
  }

  send(payload) {
    if (!this.proc || !this.proc.stdin.writable) {
      return;
    }
    const body = JSON.stringify(payload);
    this.proc.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
  }

  request(method, params) {
    if (!this.proc) {
      return Promise.reject(new Error("mako lsp is not running"));
    }
    const id = this.nextId++;
    this.send({ jsonrpc: "2.0", id, method, params });
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error(`${method} timed out`));
        }
      }, 5000);
    });
  }

  notify(method, params) {
    this.send({ jsonrpc: "2.0", method, params });
  }

  didOpen(document) {
    if (!this.ready || document.languageId !== "mako") {
      return;
    }
    this.notify("textDocument/didOpen", {
      textDocument: {
        uri: document.uri.toString(),
        languageId: "mako",
        version: document.version,
        text: document.getText()
      }
    });
  }

  didChange(document) {
    if (!this.ready || document.languageId !== "mako") {
      return;
    }
    this.notify("textDocument/didChange", {
      textDocument: {
        uri: document.uri.toString(),
        version: document.version
      },
      contentChanges: [{ text: document.getText() }]
    });
  }

  didClose(document) {
    if (!this.ready || document.languageId !== "mako") {
      return;
    }
    this.notify("textDocument/didClose", {
      textDocument: { uri: document.uri.toString() }
    });
  }

  textDocumentPosition(method, document, position, extra = {}) {
    return this.request(method, {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
      ...extra
    });
  }
}

function firstWorkspaceUri() {
  const folders = vscode.workspace.workspaceFolders;
  return folders && folders.length > 0 ? folders[0].uri.toString() : null;
}

function fromLspRange(range) {
  if (!range) {
    return new vscode.Range(0, 0, 0, 0);
  }
  return new vscode.Range(
    range.start.line,
    range.start.character,
    range.end.line,
    range.end.character
  );
}

function fromLspSeverity(severity) {
  switch (severity) {
    case 1:
      return vscode.DiagnosticSeverity.Error;
    case 2:
      return vscode.DiagnosticSeverity.Warning;
    case 3:
      return vscode.DiagnosticSeverity.Information;
    case 4:
      return vscode.DiagnosticSeverity.Hint;
    default:
      return vscode.DiagnosticSeverity.Error;
  }
}

function fromLspLocation(loc) {
  if (!loc) {
    return null;
  }
  return new vscode.Location(vscode.Uri.parse(loc.uri), fromLspRange(loc.range));
}

function fromLspSymbolKind(kind) {
  switch (kind) {
    case 12:
      return vscode.SymbolKind.Function;
    case 23:
      return vscode.SymbolKind.Struct;
    case 10:
      return vscode.SymbolKind.Enum;
    default:
      return vscode.SymbolKind.Variable;
  }
}

function fromLspCodeActionKind(kind) {
  if (kind === "quickfix") {
    return vscode.CodeActionKind.QuickFix;
  }
  if (kind && kind.startsWith("source.fixAll")) {
    return vscode.CodeActionKind.SourceFixAll;
  }
  if (kind && kind.startsWith("source")) {
    return vscode.CodeActionKind.Source;
  }
  return vscode.CodeActionKind.Empty;
}

function toLspRange(range) {
  return {
    start: { line: range.start.line, character: range.start.character },
    end: { line: range.end.line, character: range.end.character }
  };
}

function toLspDiagnostic(diagnostic) {
  return {
    range: toLspRange(diagnostic.range),
    severity: diagnostic.severity + 1,
    source: diagnostic.source || "mako",
    message: diagnostic.message
  };
}

function workspaceEditFromLsp(edit) {
  const workspaceEdit = new vscode.WorkspaceEdit();
  const changes = (edit && edit.changes) || {};
  for (const [uri, edits] of Object.entries(changes)) {
    for (const e of edits) {
      workspaceEdit.replace(vscode.Uri.parse(uri), fromLspRange(e.range), e.newText || "");
    }
  }
  return workspaceEdit;
}

function fromLspCodeAction(action) {
  if (!action || !action.title) {
    return null;
  }
  const codeAction = new vscode.CodeAction(action.title, fromLspCodeActionKind(action.kind));
  if (action.edit) {
    codeAction.edit = workspaceEditFromLsp(action.edit);
  }
  if (action.command) {
    const command = typeof action.command === "string"
      ? { command: action.command, title: action.title, arguments: [] }
      : action.command;
    codeAction.command = {
      command: command.command,
      title: command.title || action.title,
      arguments: command.arguments || []
    };
  }
  return codeAction;
}

function registerLspProviders(context, client) {
  const selector = { language: "mako", scheme: "file" };
  context.subscriptions.push(
    vscode.languages.registerHoverProvider(selector, {
      provideHover(document, position) {
        return client.textDocumentPosition("textDocument/hover", document, position).then((result) => {
          if (!result || !result.contents) {
            return null;
          }
          const value = typeof result.contents === "string" ? result.contents : result.contents.value;
          return new vscode.Hover(new vscode.MarkdownString(value || ""));
        });
      }
    }),
    vscode.languages.registerCompletionItemProvider(selector, {
      provideCompletionItems(document, position) {
        return client.textDocumentPosition("textDocument/completion", document, position).then((result) => {
          const items = Array.isArray(result) ? result : (result && result.items) || [];
          return items.map((item) => new vscode.CompletionItem(item.label, vscode.CompletionItemKind.Keyword));
        });
      }
    }, "."),
    vscode.languages.registerDefinitionProvider(selector, {
      provideDefinition(document, position) {
        return client.textDocumentPosition("textDocument/definition", document, position).then(fromLspLocation);
      }
    }),
    vscode.languages.registerReferenceProvider(selector, {
      provideReferences(document, position) {
        return client.textDocumentPosition("textDocument/references", document, position).then((result) => {
          return (Array.isArray(result) ? result : []).map(fromLspLocation).filter(Boolean);
        });
      }
    }),
    vscode.languages.registerCodeActionsProvider(selector, {
      provideCodeActions(document, range, codeActionContext) {
        return client.request("textDocument/codeAction", {
          textDocument: { uri: document.uri.toString() },
          range: toLspRange(range),
          context: {
            diagnostics: (codeActionContext.diagnostics || []).map(toLspDiagnostic)
          }
        }).then((result) => {
          return (Array.isArray(result) ? result : []).map(fromLspCodeAction).filter(Boolean);
        });
      }
    }, {
      providedCodeActionKinds: [
        vscode.CodeActionKind.QuickFix,
        vscode.CodeActionKind.Source,
        vscode.CodeActionKind.SourceFixAll
      ]
    }),
    vscode.languages.registerDocumentSymbolProvider(selector, {
      provideDocumentSymbols(document) {
        return client.request("textDocument/documentSymbol", {
          textDocument: { uri: document.uri.toString() }
        }).then((result) => {
          return (Array.isArray(result) ? result : []).map((sym) => new vscode.DocumentSymbol(
            sym.name,
            sym.detail || "",
            fromLspSymbolKind(sym.kind),
            fromLspRange(sym.range),
            fromLspRange(sym.selectionRange || sym.range)
          ));
        });
      }
    }),
    vscode.languages.registerRenameProvider(selector, {
      prepareRename(document, position) {
        return client.textDocumentPosition("textDocument/prepareRename", document, position).then((result) => {
          return result && result.range ? fromLspRange(result.range) : null;
        });
      },
      provideRenameEdits(document, position, newName) {
        return client.textDocumentPosition("textDocument/rename", document, position, { newName }).then((result) => {
          const edit = new vscode.WorkspaceEdit();
          const changes = (result && result.changes) || {};
          for (const [uri, edits] of Object.entries(changes)) {
            for (const e of edits) {
              edit.replace(vscode.Uri.parse(uri), fromLspRange(e.range), e.newText);
            }
          }
          return edit;
        });
      }
    }),
    vscode.languages.registerSignatureHelpProvider(selector, {
      provideSignatureHelp(document, position) {
        return client.textDocumentPosition("textDocument/signatureHelp", document, position).then((result) => {
          if (!result || !Array.isArray(result.signatures)) {
            return null;
          }
          const help = new vscode.SignatureHelp();
          help.activeParameter = result.activeParameter || 0;
          help.activeSignature = result.activeSignature || 0;
          help.signatures = result.signatures.map((sig) => {
            const item = new vscode.SignatureInformation(sig.label || "");
            item.documentation = sig.documentation || "";
            item.parameters = (sig.parameters || []).map((p) => new vscode.ParameterInformation(p.label || ""));
            return item;
          });
          return help;
        });
      }
    }, "(", ","),
    vscode.languages.registerWorkspaceSymbolProvider({
      provideWorkspaceSymbols(query) {
        return client.request("workspace/symbol", { query }).then((result) => {
          return (Array.isArray(result) ? result : []).map((sym) => new vscode.SymbolInformation(
            sym.name,
            fromLspSymbolKind(sym.kind),
            sym.containerName || "",
            fromLspLocation(sym.location) || new vscode.Location(vscode.Uri.file(workspaceCwd()), new vscode.Position(0, 0))
          ));
        });
      }
    })
  );
}

function activate(context) {
  context.diagnostics = vscode.languages.createDiagnosticCollection("mako");
  context.subscriptions.push(context.diagnostics);

  registerCommand(context, "mako.check", () => ["check", activeTarget(".")]);
  registerCommand(context, "mako.build", () => ["build", activeTarget(".")]);
  registerCommand(context, "mako.run", () => ["run", activeTarget(".")]);
  registerCommand(context, "mako.test", () => ["test", "."]);
  registerCommand(context, "mako.format", () => ["fmt", "-w", activeTarget(".")]);
  registerCommand(context, "mako.initProject", () => ["init", "."]);
  context.subscriptions.push(
    vscode.commands.registerCommand("mako.debugActiveFile", debugActiveFile),
    vscode.debug.registerDebugConfigurationProvider(
      "mako-native",
      new MakoDebugConfigurationProvider()
    )
  );

  // Format on save — runs `mako fmt -w` and returns the formatted text
  context.subscriptions.push(
    vscode.languages.registerDocumentFormattingEditProvider("mako", {
      provideDocumentFormattingEdits(document) {
        return new Promise((resolve) => {
          const filePath = document.uri.fsPath;
          const mako = makoPath();
          cp.execFile(mako, ["fmt", "-w", filePath], { cwd: workspaceCwd() }, (err) => {
            if (err) {
              resolve([]);
              return;
            }
            const fs = require("fs");
            try {
              const formatted = fs.readFileSync(filePath, "utf8");
              const fullRange = new vscode.Range(
                document.positionAt(0),
                document.positionAt(document.getText().length)
              );
              resolve([vscode.TextEdit.replace(fullRange, formatted)]);
            } catch (_) {
              resolve([]);
            }
          });
        });
      }
    })
  );

  const client = new MakoLanguageClient(context);
  context.makoLanguageClient = client;
  registerLspProviders(context, client);
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => client.didOpen(doc)),
    vscode.workspace.onDidChangeTextDocument((event) => client.didChange(event.document)),
    vscode.workspace.onDidCloseTextDocument((doc) => client.didClose(doc)),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration("mako.path") || event.affectsConfiguration("mako.lsp.enabled")) {
        client.stop();
        client.start();
      }
    }),
    vscode.commands.registerCommand("mako.restartLanguageServer", () => {
      client.stop();
      client.start();
    }),
    { dispose: () => client.stop() }
  );
  client.start();
}

function deactivate() {
}

module.exports = {
  activate,
  deactivate
};
