"""The spectators. NEW: name -> (what to draw, shape, build).

shape: 'tall' cells are 1:2 (people), 'wide' cells are 1:1 (beasts).
build: tiny / small / normal / tall / giant / flyer. The game seats giants at the back, the tiny ones at the front and
hangs flyers above the stands (KSGameMode.cpp, the table in SeatCrowd, written by slice_crowd.py).
"""
NEW = {
    # people of the decade
    'aerobics': ("an aerobics instructor in a hot pink leotard, turquoise tights, yellow leg warmers and a sweatband, big permed hair", 'tall', 'normal'),
    'boombox':  ("a street kid carrying an enormous silver boombox on one shoulder, a red tracksuit, fat-laced sneakers and a flat-top haircut", 'tall', 'normal'),
    'glam':     ("a glam metal singer with gigantic teased blond hair, leopard-print spandex trousers, a studded jacket and make-up", 'tall', 'tall'),
    'newwave':  ("a new wave girl with an asymmetric neon blue haircut, giant shoulder pads, a yellow blazer and huge triangle earrings", 'tall', 'normal'),
    'mullet':   ("a trucker with a mullet and moustache, a mesh cap, a sleeveless denim vest and a giant red foam finger on one hand", 'tall', 'normal'),
    'granny':   ("a tiny hunched grandmother with a grey bun, round glasses, a floral dress, a handbag and red boxing gloves", 'tall', 'small'),
    'sumo':     ("an enormous round sumo wrestler in a blue mawashi belt with a topknot, holding a paper fan", 'wide', 'tall'),
    'hoops':    ("an extremely tall, thin basketball player with a high-top fade, short shorts, striped tube socks and a yellow jersey", 'tall', 'giant'),
    # monsters in the cheap seats
    'skeleton': ("a skeleton wearing black sunglasses, a hawaiian shirt and a backwards cap", 'tall', 'normal'),
    'mummy':    ("a bandaged mummy with one glowing eye and loose trailing bandages, holding a bucket of popcorn", 'tall', 'normal'),
    'werewolf': ("a werewolf in a torn red varsity jacket and ripped jeans, with pointed ears and fangs", 'tall', 'tall'),
    'reaper':   ("a grim reaper in a black hooded robe with a skull face, holding a scythe in one hand and a soda cup in the other", 'tall', 'tall'),
    'medusa':   ("a gorgon woman with a head of live green snakes, a purple toga and gold bracelets, with a snake tail instead of legs", 'tall', 'normal'),
    'stitch':   ("a huge square-headed patchwork monster made of stitched grey-blue skin with neck bolts, in a too-small brown jacket", 'tall', 'giant'),
    'yeti':     ("a big shaggy white yeti with a blue face, wearing a red knitted scarf", 'wide', 'giant'),
    'minotaur': ("a muscular minotaur with a bull's head, a nose ring and curved horns, in gym shorts and a tank top", 'tall', 'giant'),
    # small fry
    'gnome':    ("a tiny garden gnome with a tall red pointed hat, a white beard and a blue coat", 'tall', 'tiny'),
    'slime':    ("a wobbly purple slime blob with two big eyes and a wide mouth, stretching its goo into arms", 'wide', 'tiny'),
    # the big ones
    'trex':     ("a huge orange tyrannosaurus rex with tiny arms, a mouth full of teeth, wearing a tiny baseball cap", 'wide', 'giant'),
    'dragon':   ("a fat red dragon with small wings, yellow belly scales and horns, puffing flames when excited", 'wide', 'giant'),
    'cyclops':  ("a towering one-eyed cyclops with a single huge eye, a fur loincloth and a wooden club", 'tall', 'giant'),
    'ape':      ("a huge grey silverback gorilla wearing a loud hawaiian shirt and mirrored aviator sunglasses, holding a banana", 'wide', 'giant'),
    'kraken':   ("a big teal octopus-like sea monster with many curling tentacles and angry yellow eyes, sitting in a metal washtub of water", 'wide', 'tall'),
    'unicorn':  ("a white unicorn standing on its hind legs with a rainbow mane and tail, a golden horn and star-shaped sunglasses", 'wide', 'tall'),
    'mermaid':  ("a punk mermaid with a tall green mohawk, a studded black jacket and an orange scaly fish tail, sitting in an inflatable yellow paddling pool", 'wide', 'normal'),
    # in the air
    'ghost':    ("a floating white bedsheet ghost with black eyes, wearing headphones", 'tall', 'flyer'),
    'ufo':      ("a small silver flying saucer with coloured lights and a glass dome, a little grey alien pilot inside waving", 'wide', 'flyer'),
    'genie':    ("a blue genie with a turban, gold cuffs and a vest, his lower body a wisp of smoke rising from a brass lamp", 'tall', 'flyer'),
    'phoenix':  ("a flaming phoenix bird with orange and yellow fire feathers and spread wings, flapping in the air", 'wide', 'flyer'),
    'fairy':    ("a tiny fairy with glittering dragonfly wings, a neon green dress and a star wand, hovering", 'tall', 'flyer'),
}

# The first twenty, redrawn from their old sheets (art/arena/spec_<name>.jpg): name -> build.
OLD_BUILD = {
    'kid': 'small', 'robot': 'normal', 'mom': 'normal', 'luchador': 'normal', 'punk': 'normal', 'alien': 'small', 'nerd': 'normal',
    'rocker': 'tall', 'skater': 'normal', 'boss': 'normal', 'bboy': 'normal', 'oldman': 'small', 'ninja': 'normal', 'astro': 'tall',
    'vampire': 'tall', 'chef': 'normal', 'cheer': 'normal', 'cowboy': 'tall', 'prof': 'normal', 'wizard': 'tall',
}
OLD = list(OLD_BUILD)
