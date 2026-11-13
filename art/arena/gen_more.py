import subprocess, json
chars = {
 'skater': "a roller-skating girl with a neon yellow visor, side ponytail, purple top and white roller skates",
 'boss': "a sweaty businessman in a grey suit with a loosened red tie holding a huge brick mobile phone",
 'bboy': "a breakdancer in a red tracksuit with a gold chain and a white bucket hat",
 'oldman': "a tiny old man with a giant white moustache, flat cap and walking cane waving a small pennant flag",
 'ninja': "a ninja in a purple suit and mask holding a paper cup of noodles",
 'astro': "an astronaut in a white space suit with an orange visor helmet",
 'vampire': "a vampire with slicked black hair, a red-lined black cape and paper 3D glasses",
 'chef': "a round chef with a tall white hat and apron holding a hot dog",
 'cheer': "a cheerleader with pink pom-poms, a blue and white uniform and a high ponytail",
 'cowboy': "a cowboy with an enormous ten-gallon hat, a sheriff star and a red bandana",
 'prof': "a mad scientist with wild white hair, round goggles and a lab coat",
 'wizard': "a wizard with a long white beard, a purple robe and a pointed hat covered in yellow stars",
}
ids = {}
for k, d in chars.items():
    prompt = ("Sprite sheet from an old 1980s arcade cabinet video game, authentic early-80s 8-bit pixel art: very large chunky square pixels, low resolution, limited palette of about 12 bright colours, hard pixel edges, no gradients, no anti-aliasing, no smooth shading. "
      "The SAME spectator character drawn exactly three times side by side in one row, full body from head to feet, front view facing the viewer, evenly spaced with clear empty gaps between them, all the same size: "
      "left: standing idle watching with arms down; middle: cheering with both arms raised high and mouth open; right: wincing in shock with hands on cheeks. "
      f"The character: {d}. Flat solid pure green #00FF00 background everywhere, no green on the character, no shadows, no floor, no text, no labels, no borders, no watermark.")
    r = subprocess.run(['dreamina-canvas','node','create','image','--mode','t2i','--model','seedream_4.7','--ratio','16:9','--resolution','2K','--title','80s spectator '+k,'--format','json','--prompt',prompt],capture_output=True,text=True)
    ids[k] = json.loads(r.stdout)['data']['node']['nodeId']
q = json.loads(subprocess.run(['dreamina-canvas','node','quote','--format','json'] + sum((['--node-id',v] for v in ids.values()),[]),capture_output=True,text=True).stdout)
print('quote', q['data']['totalMaxCredits'], flush=True)
assert q['data']['totalMaxCredits'] <= 24
out = json.loads(subprocess.run(['dreamina-canvas','node','run','--format','json'] + sum((['--node-id',v] for v in ids.values()),[]),capture_output=True,text=True).stdout)
for it in out['data']['items']:
    k = [a for a,b in ids.items() if b==it['nodeId']][0]
    w = subprocess.run(['dreamina-canvas','operation','wait',it['submitId'],'--format','json'],capture_output=True,text=True).stdout
    st = json.loads(w)['data']['state']
    if st=='succeeded':
        subprocess.run(['dreamina-canvas','resource','download',it['resources'][0]['resourceId'],'-o',f'spec_{k}.jpg','--format','json'],capture_output=True,text=True)
    print(k, st, flush=True)
