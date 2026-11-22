from PIL import Image

# 1. Charger l'image grille 3x3
img = Image.open("Manon.png").convert("RGB") #Modifier le nom du fichier et l'extension
largeur, hauteur = img.size
cell_w = largeur // 3
cell_h = hauteur // 3

# Récupérer la couleur exacte du fond jaune (pixel en haut à gauche)
bg_rgb = img.getpixel((5, 5))

def rgb_to_565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

fond_jaune_565 = rgb_to_565(*bg_rgb)

# Noms dans l'ordre de lecture de ta grille (haut -> bas, gauche -> droite)
noms = [
    "poussin_coeurs", "poussin_clin", "poussin_rire",
    "poussin_pleure", "poussin_joie", "poussin_colere",
    "poussin_timide", "poussin_dodo", "poussin_choque"
]

with open("images_poussin.h", "w", encoding="utf-8") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("const int IMG_LARGEUR = 160;\n")
    f.write("const int IMG_HAUTEUR = 160;\n")
    f.write(f"// Couleur a copier dans main.cpp : #define FOND_JAUNE 0x{fond_jaune_565:04X}\n\n")

    for idx, nom in enumerate(noms):
        col = idx % 3
        row = idx // 3

        # Découpe de la case
        case = img.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))

        # Redimensionnement proportionnel dans un carré 160x160 sans déformation
        case.thumbnail((160, 160), Image.Resampling.LANCZOS)
        carre = Image.new("RGB", (160, 160), bg_rgb)
        offset_x = (160 - case.width) // 2
        offset_y = (160 - case.height) // 2
        carre.paste(case, (offset_x, offset_y))

        # Conversion en tableau C++ RGB565
        pixels = list(carre.getdata())
        f.write(f"const uint16_t {nom}[] PROGMEM = {{\n  ")
        hex_vals = [f"0x{rgb_to_565(r, g, b):04X}" for (r, g, b) in pixels]
        for i in range(0, len(hex_vals), 16):
            f.write(", ".join(hex_vals[i:i+16]))
            if i + 16 < len(hex_vals):
                f.write(",\n  ")
        f.write("\n};\n\n")

    f.write("""const uint16_t* const IMAGES_POUSSIN[] PROGMEM = {
  poussin_clin,
  poussin_joie,
  poussin_coeurs,
  poussin_rire,
  poussin_timide,
  poussin_choque,
  poussin_colere,
  poussin_pleure,
  poussin_dodo
};

const int NB_EXPRESSIONS = 9;
""")

print(f"Termine ! Valeur exacte a mettre dans main.cpp : #define FOND_JAUNE 0x{fond_jaune_565:04X}")