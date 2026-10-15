"""Draws icon0.png (512x512): green text on black background."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

S = 512
img = Image.new('RGB', (S, S), (0, 0, 0))
d = ImageDraw.Draw(img)
bold = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
def center(text, font, y, fill):
    w = d.textlength(text, font=font)
    d.text(((S - w) / 2, y), text, font=font, fill=fill)
green = (47, 158, 87)
center('PS4', ImageFont.truetype(bold, 190), 100, green)
center('NeoEnabler', ImageFont.truetype(bold, 56), 340, green)
img.save(Path(__file__).resolve().parent.parent / 'assets' / 'icon0.png', optimize=True)
