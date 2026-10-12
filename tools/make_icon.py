"""Draws icon0.png (512x512): "PS4" mark over a dark blue gradient, "NEO ENABLER" below."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

S = 512
img = Image.new('RGB', (S, S))
d = ImageDraw.Draw(img)
for y in range(S):
    t = y / (S - 1)
    d.line([(0, y), (S, y)], fill=(int(0 + 10 * t), int(55 - 35 * t), int(150 - 90 * t)))
bold = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
def center(text, font, y, fill):
    w = d.textlength(text, font=font)
    d.text(((S - w) / 2, y), text, font=font, fill=fill)
center('PS4', ImageFont.truetype(bold, 190), 120, (255, 255, 255))
center('NEO ENABLER', ImageFont.truetype(bold, 46), 360, (190, 210, 255))
img.save(Path(__file__).resolve().parent.parent / 'assets' / 'icon0.png', optimize=True)
