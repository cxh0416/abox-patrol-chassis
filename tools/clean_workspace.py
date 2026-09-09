"""Preview or remove disposable local output; legacy migration archives evidence first."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import zipfile

from artifact_store import checked_path, checked_tree, digest, publish, remove_tree

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = "docs/operations/evidence/raw/2026-09-09-out.zip"
INDEX = "docs/operations/evidence/raw/2026-09-09-out.index.json"
MIGRATIONS = {
    "out/bench-patrol-chassis-0.2.9": "out/candidates/bench-steering",
    "out/bench-patrol-chassis-0.2.14": "out/candidates/bench-drive",
}
KEEP_OUT = {"candidates", "debug", "logs"}
RECORD_TYPES = {".json", ".jsonl", ".csv", ".md", ".png"}
PATH_RE = re.compile(r"out[/\\]+[^\s\"'`<>|,;\]\)]+")


def references(text: str) -> set[str]:
    try:
        document = json.loads(text)
    except ValueError:
        document = None
    if document is not None:
        def strings(value):
            if isinstance(value, str):
                yield value
            elif isinstance(value, dict):
                for key, child in value.items():
                    yield key
                    yield from strings(child)
            elif isinstance(value, list):
                for child in value:
                    yield from strings(child)
        text = "\n".join(strings(document))
    return {re.sub(r"[/\\]+", "/", p).rstrip(".:。") for p in PATH_RE.findall(text)}


def verify_archive(root: Path) -> dict:
    index_path = checked_path(root, root / INDEX)
    archive_path = checked_path(root, root / ARCHIVE)
    index = json.loads(index_path.read_text(encoding="utf-8"))
    if digest(archive_path) != index["archive_sha256"]:
        raise ValueError("Evidence archive SHA256 mismatch")
    with zipfile.ZipFile(archive_path) as archive:
        if set(archive.namelist()) != {e["member"] for e in index["entries"]}:
            raise ValueError("Evidence archive inventory mismatch")
        for entry in index["entries"]:
            data = archive.read(entry["member"])
            if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError(f"Evidence archive member mismatch: {entry['member']}")
    return index


def prune_archive(root: Path, prefixes: list[str], members: set[str]) -> dict:
    """Remove explicitly selected members and atomically update the archive index."""
    root = root.absolute()
    archive_path = checked_path(root, root / ARCHIVE)
    index_path = checked_path(root, root / INDEX)
    index = verify_archive(root)
    remove = {entry["member"] for entry in index["entries"]
              if entry["member"] in members or any(entry["member"].startswith(p) for p in prefixes)}
    if not remove:
        raise ValueError("No archive members matched the requested paths")
    temporary_archive = archive_path.with_suffix(".pruned.zip")
    temporary_index = index_path.with_suffix(".pruned.json")
    archive_backup = archive_path.with_suffix(".backup.zip")
    index_backup = index_path.with_suffix(".backup.json")
    for path in (temporary_archive, temporary_index, archive_backup, index_backup):
        checked_path(root, path)
        if path.exists():
            raise ValueError(f"Unexpected archive maintenance file: {path}")
    with zipfile.ZipFile(archive_path) as source, zipfile.ZipFile(temporary_archive, "x") as target:
        for info in source.infolist():
            if info.filename not in remove:
                target.writestr(info, source.read(info.filename))
    index["entries"] = [entry for entry in index["entries"] if entry["member"] not in remove]
    index.pop("cleanup_inventory", None)
    index["archive_sha256"] = digest(temporary_archive)
    temporary_index.write_text(json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with zipfile.ZipFile(temporary_archive) as archive:
        if set(archive.namelist()) != {entry["member"] for entry in index["entries"]}:
            raise ValueError("Pruned archive inventory mismatch")
        for entry in index["entries"]:
            data = archive.read(entry["member"])
            if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError(f"Pruned archive member mismatch: {entry['member']}")
    os.replace(archive_path, archive_backup)
    try:
        os.replace(temporary_archive, archive_path)
        os.replace(index_path, index_backup)
        try:
            os.replace(temporary_index, index_path)
            verify_archive(root)
        except BaseException:
            if index_backup.exists():
                os.replace(index_backup, index_path)
            raise
    except BaseException:
        if archive_backup.exists():
            os.replace(archive_backup, archive_path)
        raise
    archive_backup.unlink()
    index_backup.unlink()
    return {"removed": sorted(remove), "remaining": len(index["entries"])}


def make_plan(root: Path, legacy: bool, build: bool) -> dict:
    root = root.absolute()
    paths = []
    for name in ("tmp", "out/logs", "tools/__pycache__", "tests/__pycache__"):
        path = checked_path(root, root / name)
        if path.exists():
            paths.append(path)
    if build and (root / "app/build").exists():
        paths.append(checked_path(root, root / "app/build"))
    legacy_paths = []
    out = checked_path(root, root / "out")
    if legacy and out.exists():
        legacy_paths = [p for p in out.iterdir() if p.name not in KEEP_OUT]
        paths.extend(legacy_paths)
    all_files = []
    for path in paths:
        children = checked_tree(root, path)
        all_files.extend([p for p in children if p.is_file()] if path.is_dir() else [path])
    legacy_files = [p for p in all_files if any(p == d or p.is_relative_to(d) for d in legacy_paths)]
    refs = set()
    evidence_refs = set()
    for doc in (root / "docs").rglob("*"):
        if doc.suffix in {".md", ".json"} and "archive" not in doc.parts and "raw" not in doc.parts:
            refs.update(references(doc.read_text(encoding="utf-8-sig")))
            if doc.parent.name == "evidence" and doc.suffix == ".json":
                evidence_refs.update(references(doc.read_text(encoding="utf-8-sig")))
    for file in all_files:
        rel = file.relative_to(root).as_posix()
        if file not in legacy_files and any(rel == r or rel.startswith(r.rstrip("/") + "/") for r in evidence_refs):
            raise ValueError(f"Evidence references disposable output; archive it before cleanup: {rel}")
    # Keep observational records from the same sessions as cited summaries.
    selected = {p for p in legacy_files if p.suffix.lower() in RECORD_TYPES and
                not any(part.startswith("bench-patrol-chassis-") for part in p.parts) and
                "__pycache__" not in p.parts}
    while True:
        extra = {p for p in legacy_files if p.relative_to(root).as_posix() in refs and
                 p.suffix.lower() not in {".bin", ".elf", ".dll", ".exe", ".pyc"}}
        added = extra - selected
        selected.update(extra)
        old_refs = set(refs)
        for p in selected:
            if p.suffix.lower() in {".md", ".json", ".jsonl", ".py", ".ps1", ".txt"}:
                refs.update(references(p.read_text(encoding="utf-8-sig", errors="replace")))
        if not added and old_refs == refs:
            break
    # Local links in the controller session report also carry provenance.
    for report in [p for p in selected if p.suffix == ".md"]:
        for name in re.findall(r"\]\(([^)]+)\)", report.read_text(encoding="utf-8-sig")):
            linked = report.parent / name
            if linked in legacy_files and linked.is_file():
                selected.add(linked)
    inventory = [{"path": p.relative_to(root).as_posix(), "bytes": p.stat().st_size,
                  "sha256": digest(p), "archive": p in selected} for p in sorted(all_files)]
    saved = set()
    if (root / INDEX).exists():
        saved = {e["original_path"] for e in verify_archive(root)["entries"]}
    missing = sorted(r for r in (refs if legacy else evidence_refs)
                     if not (root / r).exists() and not any(c in r for c in "{}*<") and
                     r not in saved and not any(p.startswith(r.rstrip("/") + "/") for p in saved))
    return {"delete": [p.relative_to(root).as_posix() for p in paths], "files": inventory,
            "missing_references": missing, "archive_files": len(selected),
            "bytes": sum(f["bytes"] for f in inventory)}


def execute(root: Path, plan: dict) -> None:
    # Validate the entire snapshot before creating archives or deleting anything.
    for rel in plan["delete"]:
        parts = Path(rel).parts
        allowed = rel in {"tmp", "out/logs", "tools/__pycache__", "tests/__pycache__", "app/build"}
        legacy = len(parts) == 2 and parts[0] == "out" and parts[1] not in KEEP_OUT
        if not (allowed or legacy):
            raise ValueError(f"Protected cleanup path: {rel}")
        checked_tree(root, root / rel)
    for entry in plan["files"]:
        p = checked_path(root, root / entry["path"])
        if digest(p) != entry["sha256"]:
            raise ValueError(f"File changed since preview: {entry['path']}")
    selected = [e for e in plan["files"] if e["archive"]]
    if selected:
        archive_path = checked_path(root, root / ARCHIVE)
        index_path = checked_path(root, root / INDEX)
        if archive_path.exists() or index_path.exists():
            index = verify_archive(root)
            saved = {e["original_path"]: e["sha256"] for e in index["entries"]}
            if any(saved.get(e["path"]) != e["sha256"] for e in selected):
                raise ValueError("Existing archive does not cover this migration; no files deleted")
        else:
            archive_path.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(archive_path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
                for entry in selected:
                    archive.write(root / entry["path"], entry["path"])
            entries = [{"original_path": e["path"], "archive_path": ARCHIVE,
                        "member": e["path"], "bytes": e["bytes"], "sha256": e["sha256"]} for e in selected]
            index = {"product": "patrol_vehicle_chassis", "archived_at": "2026-09-09",
                     "archive_sha256": digest(archive_path), "entries": entries,
                     "missing_references": plan["missing_references"]}
            index_path.write_text(json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            verify_archive(root)
    for source, destination in MIGRATIONS.items():
        if source in plan["delete"]:
            if (root / destination).exists():
                # A newer fixed-location candidate must never be overwritten by migration.
                from artifact_store import validate
                validate(root / destination, "candidate")
            else:
                publish(root, root / source, destination)
    # Archive and candidate verification must all pass before any original removal.
    current = []
    for rel in plan["delete"]:
        p = root / rel
        current.extend([x for x in checked_tree(root, p) if x.is_file()] if p.is_dir() else [p])
    expected = {e["path"]: e["sha256"] for e in plan["files"]}
    actual = {p.relative_to(root).as_posix(): digest(p) for p in current}
    if actual != expected:
        raise ValueError("Cleanup snapshot changed; no originals removed")
    for rel in plan["delete"]:
        remove_tree(root, root / rel)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Execute; default is preview only")
    parser.add_argument("--build", action="store_true", help="Also remove app/build (never platform/build)")
    parser.add_argument("--migrate-legacy", action="store_true", help="One-time 2026-09-09 out evidence migration")
    parser.add_argument("--verify-archive", action="store_true")
    parser.add_argument("--remove-archive-prefix", action="append", default=[])
    parser.add_argument("--remove-archive-member", action="append", default=[])
    args = parser.parse_args()
    if args.remove_archive_prefix or args.remove_archive_member:
        result = prune_archive(ROOT, args.remove_archive_prefix, set(args.remove_archive_member))
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return
    if args.verify_archive:
        index = verify_archive(ROOT)
        print(f"Verified {len(index['entries'])} archived records")
        return
    plan = make_plan(ROOT, args.migrate_legacy, args.build)
    print(json.dumps({k: v for k, v in plan.items() if k != "files"}, ensure_ascii=False, indent=2))
    print(f"Files: {len(plan['files'])}; mode: {'APPLY' if args.apply else 'PREVIEW'}")
    if args.apply:
        execute(ROOT, plan)


if __name__ == "__main__":
    main()
