# Lux for PyCharm

Syntax highlighting for `.lux` files. PyCharm loads this as a TextMate bundle (`Lux.tmbundle`). The grammar matches `editors/vscode/syntaxes/lux.tmLanguage.json`.

## Install

1. Open **Settings → Editor → TextMate Bundles**.
2. Confirm the **TextMate Bundles** plugin is enabled (**Settings → Plugins**).
3. Click **+** and choose `editors/pycharm/Lux.tmbundle`.
4. Restart PyCharm if `.lux` files still open as plain text.

If `.lux` was previously associated with **Text**, remove that pattern under **Settings → Editor → File Types** so the bundle can claim it.
