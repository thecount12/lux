# Lux for VS Code

Syntax highlighting, comments, and bracket matching for `.lux` files.

## Install

From the repository root, link this folder into your editor's extensions directory, then reload the window (`Developer: Reload Window`).

VS Code:

```bash
mkdir -p ~/.vscode/extensions
ln -sfn "$PWD/editors/vscode" ~/.vscode/extensions/lux9.lux-0.1.0
```

Cursor:

```bash
mkdir -p ~/.cursor/extensions
ln -sfn "$PWD/editors/vscode" ~/.cursor/extensions/lux9.lux-0.1.0
```

Open any `.lux` file. The language mode in the status bar should say **Lux**.
