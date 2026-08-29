# NDJSON in Lux

NDJSON is one JSON object per line, not one JSON array. Do not `parseJSON` the whole file. Parse **each line**.

Lux has no generators. The two correct approaches are:

1. **Stream** with `File.readLine()` — one line in memory at a time
2. **Cache** with `readFile` + `strSplit` — all line strings in an array, reused by later methods

## Which to use

| | Stream `File.readLine()` | Cache `strSplit` once |
|---|---|---|
| Peak memory | File handle + ~8KiB buffer + current line + current `parseJSON` object | Whole file string (briefly) + array of every line string |
| Best for | Large files, one pass, remote disks (`/mnt/term`, 9P) | Small/medium files you walk **more than once** |
| CPU | Interpreted `while` + one native read per line | One native split, then cheap `lines[i]` |
| 17MB once | Seconds | Seconds, if you split **once** |
| 17MB if you re-read every iteration | — | Minutes to hours (see anti-pattern) |

Keep parsed objects in an array only when you need them. Streaming does not forbid that; it only avoids holding **every line string** by default.

Copy remote files to local disk before parsing when you can. Even a single `readFile` of 17MB over 9P is slow.

## Anti-pattern

Never call a method that re-reads the file in the loop condition **and** the body:

```lux
// Wrong: readFile + strSplit twice per line
for (var i = 0; i < this.lines().length; i = i + 1) {
    var obj = parseJSON(this.lines()[i]);
}
```

On ~40k lines that is tens of thousands of full-file reads. Bind the array once, or use `File`.

Do not `appendFile` once per matching line on a remote filesystem. Collect output and `writeFile` once.

## Option 1 — Stream (`File`)

`File(path)` always returns an instance. Check `f.ok`. `readLine()` returns the line without `\n` (trailing `\r` stripped). Empty line is `""`. `nil` is EOF, closed, open failed, or a line over 32 MiB.

```lux
class Ndjson {
    init(fileName) {
        this.fileName = fileName;
    }

    operations(metric_value) {
        var f = File(this.fileName);
        if (!f.ok) {
            return [];
        }
        var list = [];
        var line = f.readLine();
        while (line != nil) {
            if (line != "") {
                var obj = parseJSON(line);
                if (obj != nil) {
                    var metric = getField(obj, "metric");
                    if (metric == metric_value) {
                        var dat = getField(obj, "data");
                        if (dat != nil) {
                            var tags = getField(dat, "tags");
                            if (tags != nil) {
                                var op = getField(tags, "operation");
                                if (!arrayContains(list, op)) {
                                    list[list.length] = op;
                                }
                            }
                        }
                    }
                }
            }
            line = f.readLine();
        }
        f.close();
        return list;
    }
}

var ndjson = Ndjson("results.ndjson");
print ndjson.operations("http_reqs");
```

Each method that needs a full scan should `File` / loop / `close` itself. Do not store `this.file` across methods unless you reopen or document the offset.

Keep 10k parsed rows:

```lux
    take(n) {
        var f = File(this.fileName);
        var list = [];
        if (!f.ok) {
            return list;
        }
        var line = f.readLine();
        while (line != nil and list.length < n) {
            if (line != "") {
                var obj = parseJSON(line);
                if (obj != nil) {
                    list[list.length] = obj;
                }
            }
            line = f.readLine();
        }
        f.close();
        return list;
    }
```

The file is still streamed. `list` holds only the objects you kept.

## Option 2 — Cache line strings (`strSplit`)

Use this when several methods will walk the same file and the file is small enough to keep as strings (tens of MB is fine; hundreds of MB usually is not).

`strSplit` **copies** each line. During the split you briefly hold the original `readFile` string and the line array (~2×). After `lines()` returns, if you only keep `this.value`, GC can drop the original file string.

```lux
class Ndjson {
    init(fileName) {
        this.fileName = fileName;
        this.value = nil;
    }

    lines() {
        if (this.value != nil) {
            return this.value;
        }
        var text = "";
        if (fileExists(this.fileName)) {
            text = readFile(this.fileName);
        }
        if (text == nil) {
            text = "";
        }
        this.value = strSplit(text, "\n");
        return this.value;
    }

    operations(metric_value) {
        var lines = this.lines();
        var list = [];
        var i = 0;
        while (i < lines.length) {
            var line = lines[i];
            i = i + 1;
            if (line == "") {
                continue;
            }
            var obj = parseJSON(line);
            if (obj == nil) {
                continue;
            }
            var metric = getField(obj, "metric");
            if (metric != metric_value) {
                continue;
            }
            var dat = getField(obj, "data");
            if (dat == nil) {
                continue;
            }
            var tags = getField(dat, "tags");
            if (tags == nil) {
                continue;
            }
            var op = getField(tags, "operation");
            if (!arrayContains(list, op)) {
                list[list.length] = op;
            }
        }
        return list;
    }
}
```

Call `this.lines()` **once** per method (or cache on `this.value` as above). Then index `lines[i]`.

Do not also store the raw `readFile` string on `this` unless you need it. That pins both copies.

Drop the cache when finished: `this.value = nil`.

## `parseJSON` notes

- One object per line. Skip `""`.
- Failure returns `nil`; skip that line.
- Nested fields: `getField(obj, "metric")`, then `getField(data, "tags")`. There is no `{ k: v }` literal.
- `toJSON` only when you need a string (writing a file). Do not `toJSON` just to print during a scan.

## Tests and natives

- `File` / `readLine` / `close`: [README.md](README.md) File Operations, [SPEC.md](SPEC.md) §14.3, [tests/test_file_readline.lux](tests/test_file_readline.lux)
- `readFile`, `strSplit`, `parseJSON`, `getField` are natives; see [SPEC.md](SPEC.md) §14
