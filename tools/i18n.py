#!/usr/bin/env python3
"""Helpers for Vocal Ink's interface translations (i18n/vocalink_<lang>.ts).

    python3 tools/i18n.py stats
    python3 tools/i18n.py export de            # unfinished strings -> i18n/work/de/NNN.json
    python3 tools/i18n.py import de            # i18n/work/de/*.json -> vocalink_de.ts (validated)
    python3 tools/i18n.py check                # validate every finished translation
    python3 tools/i18n.py en-plurals           # fill English plural forms from "(s)"

Refresh the .ts files from the sources first:
    cmake --build build --target update_translations

Standard library only.
"""

import argparse
import glob
import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N = os.path.join(ROOT, "i18n")
WORK = os.path.join(I18N, "work")

LANGUAGES = {
    # code: (English name, plural forms, how Qt picks the form, register)
    "en": ("English", 2, "form 0: n == 1; form 1: everything else", ""),
    "es": ("Spanish", 2, "form 0: n == 1; form 1: everything else", "informal (tú)"),
    "fr": ("French", 2, "form 0: n == 0 or 1; form 1: n > 1", "formal (vous)"),
    "de": ("German", 2, "form 0: n == 1; form 1: everything else", "informal (du), capitalised Du not needed"),
    "pt_BR": ("Brazilian Portuguese", 2, "form 0: n == 0 or 1; form 1: n > 1", "você"),
    "it": ("Italian", 2, "form 0: n == 1; form 1: everything else", "informal (tu)"),
    "nl": ("Dutch", 2, "form 0: n == 1; form 1: everything else", "informal (je/jij)"),
    "pl": ("Polish", 3,
           "form 0: n == 1; form 1: n % 10 in 2..4 and n % 100 not in 12..14; form 2: everything else",
           "informal (ty), gender-neutral phrasing where possible"),
    "tr": ("Turkish", 1, "one form for every n", "informal (sen)"),
    "ja": ("Japanese", 1, "one form for every n", "polite (です/ます)"),
    "ko": ("Korean", 1, "one form for every n", "polite informal (해요체)"),
    "zh_CN": ("Simplified Chinese", 1, "one form for every n", "你"),
}

# Contexts that are never shown as interface text.
SKIP_CONTEXTS = {"Emoji", "Spoken"}

# Names that must survive translation unchanged when the source has them.
KEEP_NAMES = [
    "Vocal Ink Mic", "Vocal Ink", "VTube Studio", "VSeeFace", "VNyan", "Warudo", "VirtualMotionCapture",
    "veadotube", "Streamer.bot", "OBS", "Twitch", "Whisper", "Piper", "ElevenLabs", "Azure", "Fish Audio",
    "OpenAI", "VB-CABLE", "BlackHole", "PipeWire", "PulseAudio", "eSpeak", "Discord", "Zoom", "PNGtuber",
]

PLACEHOLDER = re.compile(r"%(?:L?\d{1,2}|n)")
VARIABLE = re.compile(r"\{[a-zA-Z_][a-zA-Z0-9_]*\}")
TAG = re.compile(r"</?[a-zA-Z][^>]*>")


def ts_path(lang):
    return os.path.join(I18N, "vocalink_%s.ts" % lang)


def load(lang):
    tree = ET.parse(ts_path(lang))
    return tree


def save(tree, lang):
    root = tree.getroot()
    ET.indent(root, space="    ")
    body = ET.tostring(root, encoding="unicode")
    with open(ts_path(lang), "w", encoding="utf-8", newline="\n") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
        f.write(body)
        f.write("\n")


def text(el):
    return "" if el is None or el.text is None else el.text


def message_key(context, source, comment):
    return hashlib.sha1(("%s\x00%s\x00%s" % (context, source, comment)).encode("utf-8")).hexdigest()[:12]


def iter_messages(tree):
    """Yields (context, message element, absolute location) in document order."""
    last_line = {}
    last_file = None
    for ctx in tree.getroot().findall("context"):
        name = text(ctx.find("name"))
        for msg in ctx.findall("message"):
            where = None
            for loc in msg.findall("location"):
                fname = loc.get("filename") or last_file
                line = loc.get("line", "0")
                if fname != last_file:
                    last_file = fname
                base = last_line.get(fname, 0)
                if line.startswith(("+", "-")):
                    absolute = base + int(line)
                else:
                    absolute = int(line or 0)
                last_line[fname] = absolute
                if where is None and fname:
                    where = "%s:%d" % (os.path.normpath(fname).replace("\\", "/").lstrip("./"), absolute)
            yield name, msg, where


def is_unfinished(msg):
    tr = msg.find("translation")
    if tr is None:
        return True
    if tr.get("type") in ("unfinished",):
        return True
    if msg.get("numerus") == "yes":
        return any(not text(f) for f in tr.findall("numerusform"))
    return not text(tr)


def forms_of(msg):
    tr = msg.find("translation")
    return [] if tr is None else tr.findall("numerusform")


def check_one(source, translation, numerus, forms_expected, problems, warnings, where):
    """Validates one translation (a string, or a list of plural forms)."""
    values = translation if isinstance(translation, list) else [translation]
    if numerus and len(values) != forms_expected:
        problems.append("%s: needs %d plural forms, got %d" % (where, forms_expected, len(values)))
        return
    src_ph = sorted(p for p in PLACEHOLDER.findall(source) if p != "%n")
    for value in values:
        if not isinstance(value, str) or not value.strip():
            problems.append("%s: empty translation" % where)
            continue
        ph = sorted(p for p in PLACEHOLDER.findall(value) if p != "%n")
        if ph != src_ph:
            problems.append("%s: placeholders %s != source %s in %r" % (where, ph, src_ph, value))
        if numerus and "%n" in source and "%n" not in value:
            problems.append("%s: every plural form needs %%n: %r" % (where, value))
        if sorted(VARIABLE.findall(value)) != sorted(VARIABLE.findall(source)):
            problems.append("%s: {variables} changed in %r" % (where, value))
        if sorted(TAG.findall(value)) != sorted(TAG.findall(source)):
            problems.append("%s: HTML tags changed in %r" % (where, value))
        for name in KEEP_NAMES:
            if name in source and name not in value:
                warnings.append("%s: %r missing in %r" % (where, name, value))
                break
        if source.count("→") != value.count("→"):
            warnings.append("%s: number of → differs in %r" % (where, value))
        if source[:1].isspace() != value[:1].isspace() or source[-1:].isspace() != value[-1:].isspace():
            warnings.append("%s: leading/trailing space differs in %r" % (where, value))
        if source.count("\n") != value.count("\n"):
            warnings.append("%s: number of line breaks differs in %r" % (where, value))


def cmd_stats(_args):
    for lang in LANGUAGES:
        if not os.path.exists(ts_path(lang)):
            continue
        tree = load(lang)
        total = done = 0
        for ctx, msg, _ in iter_messages(tree):
            if ctx in SKIP_CONTEXTS:
                continue
            total += 1
            done += 0 if is_unfinished(msg) else 1
        print("%-6s %4d / %4d" % (lang, done, total))


def cmd_export(args):
    lang = args.lang
    name, forms, rule, register = LANGUAGES[lang]
    tree = load(lang)
    out_dir = os.path.join(WORK, lang)
    os.makedirs(out_dir, exist_ok=True)
    for old in glob.glob(os.path.join(out_dir, "*.json")):
        os.remove(old)
    batch, n = [], 0

    def flush():
        nonlocal batch, n
        if not batch:
            return
        n += 1
        doc = {
            "language": lang,
            "language_name": name,
            "register": register,
            "plural_forms": forms,
            "plural_rule": rule,
            "instructions": (
                "Fill every 'translation'. Keep placeholders (%1, %2, %L1, %n), {variables}, HTML tags, "
                "leading/trailing spaces and line breaks exactly. Numerus messages take a list with "
                "'plural_forms' entries, each containing %n. Keep product and app names in English "
                "(Vocal Ink, Vocal Ink Mic, OBS, Twitch, VTube Studio...). 'comment' disambiguates; "
                "'extracomment' is a note for translators. Short labels stay short."),
            "messages": batch,
        }
        with open(os.path.join(out_dir, "%03d.json" % n), "w", encoding="utf-8") as f:
            json.dump(doc, f, ensure_ascii=False, indent=1)
        batch = []

    for ctx, msg, where in iter_messages(tree):
        if ctx in SKIP_CONTEXTS or not is_unfinished(msg):
            continue
        source = text(msg.find("source"))
        comment = text(msg.find("comment"))
        numerus = msg.get("numerus") == "yes"
        entry = {
            "key": message_key(ctx, source, comment),
            "context": ctx,
            "where": where,
            "source": source,
        }
        if comment:
            entry["comment"] = comment
        extra = text(msg.find("extracomment"))
        if extra:
            entry["extracomment"] = extra
        if numerus:
            entry["numerus"] = True
            entry["translation"] = [""] * forms
        else:
            entry["translation"] = ""
        batch.append(entry)
        if len(batch) >= args.chunk:
            flush()
    flush()
    print("%s: %d file(s) in %s" % (lang, n, os.path.relpath(out_dir, ROOT)))


def cmd_import(args):
    lang = args.lang
    forms = LANGUAGES[lang][1]
    tree = load(lang)
    wanted = {}
    for path in sorted(glob.glob(os.path.join(WORK, lang, "*.json"))):
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        for entry in doc.get("messages", []):
            wanted[entry["key"]] = (entry, os.path.basename(path))
    problems, warnings, applied = [], [], 0
    for ctx, msg, where in iter_messages(tree):
        source = text(msg.find("source"))
        key = message_key(ctx, source, text(msg.find("comment")))
        if key not in wanted:
            continue
        entry, file = wanted.pop(key)
        value = entry.get("translation")
        numerus = msg.get("numerus") == "yes"
        label = "%s %s (%s)" % (file, where, ctx)
        before = len(problems)
        if numerus and not isinstance(value, list):
            problems.append("%s: numerus message needs a list" % label)
            continue
        if not numerus and not isinstance(value, str):
            problems.append("%s: needs a string" % label)
            continue
        check_one(source, value, numerus, forms, problems, warnings, label)
        if len(problems) != before:
            continue
        tr = msg.find("translation")
        if tr is None:
            tr = ET.SubElement(msg, "translation")
        tr.attrib.pop("type", None)
        if numerus:
            for f in list(tr):
                tr.remove(f)
            tr.text = None
            for v in value:
                ET.SubElement(tr, "numerusform").text = v
        else:
            tr.text = value
        applied += 1
    for key, (entry, file) in wanted.items():
        warnings.append("%s: no message with key %s (%r) any more" % (file, key, entry.get("source", "")[:40]))
    save(tree, lang)
    for w in warnings:
        print("warning:", w)
    for p in problems:
        print("error:", p)
    print("%s: %d imported, %d error(s), %d warning(s)" % (lang, applied, len(problems), len(warnings)))
    return 1 if problems else 0


def cmd_check(args):
    failed = 0
    for lang in (args.langs or LANGUAGES):
        if not os.path.exists(ts_path(lang)):
            continue
        forms = LANGUAGES[lang][1]
        tree = load(lang)
        problems, warnings = [], []
        for ctx, msg, where in iter_messages(tree):
            if is_unfinished(msg):
                continue
            source = text(msg.find("source"))
            numerus = msg.get("numerus") == "yes"
            value = [text(f) for f in forms_of(msg)] if numerus else text(msg.find("translation"))
            check_one(source, value, numerus, forms, problems, warnings, "%s %s (%s)" % (lang, where, ctx))
        for p in problems:
            print("error:", p)
        if args.warnings:
            for w in warnings:
                print("warning:", w)
        failed += len(problems)
    return 1 if failed else 0


def cmd_en_plurals(_args):
    tree = load("en")
    filled = 0
    for _ctx, msg, where in iter_messages(tree):
        if msg.get("numerus") != "yes":
            continue
        source = text(msg.find("source"))
        if "(s)" not in source:
            print("warning: %s: no '(s)' in %r; fill it by hand" % (where, source))
            continue
        tr = msg.find("translation")
        tr.attrib.pop("type", None)
        for f in list(tr):
            tr.remove(f)
        tr.text = None
        ET.SubElement(tr, "numerusform").text = source.replace("(s)", "")
        ET.SubElement(tr, "numerusform").text = source.replace("(s)", "s")
        filled += 1
    save(tree, "en")
    print("en: %d plural message(s) filled" % filled)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("stats")
    p = sub.add_parser("export")
    p.add_argument("lang", choices=[l for l in LANGUAGES if l != "en"])
    p.add_argument("--chunk", type=int, default=400)
    p = sub.add_parser("import")
    p.add_argument("lang", choices=[l for l in LANGUAGES if l != "en"])
    p = sub.add_parser("check")
    p.add_argument("langs", nargs="*")
    p.add_argument("--warnings", action="store_true")
    sub.add_parser("en-plurals")
    args = parser.parse_args()
    handler = {"stats": cmd_stats, "export": cmd_export, "import": cmd_import, "check": cmd_check,
               "en-plurals": cmd_en_plurals}[args.cmd]
    sys.exit(handler(args) or 0)


if __name__ == "__main__":
    main()
