"""Loads every Niagara system of the Fab effect packs and says which ones open in this engine. Runs without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/vfx_loadtest.py

The packs were made for older engines (5.3 to 5.7). Result: unreal/Saved/vfx_loadtest.txt, one line per system
(ok / FAILED, emitter count, what it depends on outside its own pack). Compile warnings go to the editor's log; grep it
for LogNiagara after the run.
"""
import os
import unreal

PACKS = ['/Game/MixedVFX', '/Game/Vefects', '/Game/FreeStylizedLaserBeamVFX', '/Game/PotaVFX_Smoke', '/Game/NiagaraExamples']
OUT = os.path.join(unreal.Paths.project_saved_dir(), 'vfx_loadtest.txt')
registry = unreal.AssetRegistryHelpers.get_asset_registry()
lines, bad = [], 0
for pack in PACKS:
    found = registry.get_assets_by_path(pack, recursive=True)
    systems = sorted(str(a.package_name) for a in found if str(a.asset_class_path.asset_name) == 'NiagaraSystem')
    lines.append('PACK %s assets=%d systems=%d' % (pack, len(found), len(systems)))
    for name in systems:
        asset = unreal.EditorAssetLibrary.load_asset(name)
        if not asset:
            bad += 1
            lines.append('FAILED %s' % name)
            continue
        options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
        deps = [str(d) for d in (registry.get_dependencies(name, options) or [])]
        outside = sorted(d for d in deps if d.startswith('/Game/') and not d.startswith(pack))
        missing = sorted(d for d in deps if d.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(d))
        if missing: bad += 1
        lines.append('%s %s deps=%d%s%s' % ('MISSING-DEPS' if missing else 'ok', name, len(deps),
                                           ' outside=' + ','.join(outside) if outside else '', ' missing=' + ','.join(missing) if missing else ''))
lines.append('DONE bad=%d' % bad)
with open(OUT, 'w') as f: f.write('\n'.join(lines) + '\n')
unreal.log('vfx_loadtest: %d lines, %d bad, written to %s' % (len(lines), bad, OUT))
