from PIL import Image, ImageDraw, ImageFont
import os

# Constants
WIDTH = 320
HEIGHT = 240
BG_COLOR = (0, 0, 0)
TEXT_COLOR = (255, 255, 255)

# Colors (Approximate RGB)
TFT_BLACK = (0, 0, 0)
TFT_WHITE = (255, 255, 255)
TFT_RED = (255, 0, 0)
TFT_GREEN = (0, 255, 0)
TFT_BLUE = (0, 0, 255)
TFT_CYAN = (0, 255, 255)
TFT_MAGENTA = (255, 0, 255)
TFT_YELLOW = (255, 255, 0)
TFT_ORANGE = (255, 165, 0)
TFT_PURPLE = (128, 0, 128)
TFT_NAVY = (0, 0, 128)
TFT_DARKGREEN = (0, 100, 0)
TFT_MAROON = (128, 0, 0)
TFT_DARKGREY = (64, 64, 64)

def create_blank_screen():
    return Image.new('RGB', (WIDTH, HEIGHT), BG_COLOR)

def draw_rounded_rect(draw, xy, radius, fill=None, outline=None, width=1):
    x, y, w, h = xy
    draw.rounded_rectangle([x, y, x+w, y+h], radius=radius, fill=fill, outline=outline, width=width)

def draw_button(draw, x, y, w, h, label, color, text_color=TFT_WHITE, filled=True):
    if filled:
        draw_rounded_rect(draw, (x, y, w, h), 5, fill=color, outline=TFT_WHITE)
    else:
        draw_rounded_rect(draw, (x, y, w, h), 5, fill=None, outline=color, width=2)
        
    # Simple text centering
    font = ImageFont.load_default()
    # Scale font for "large" look (simulated)
    bbox = draw.textbbox((0, 0), label, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    draw.text((x + (w - text_w) / 2, y + (h - text_h) / 2), label, fill=text_color, font=font)

def draw_mandolin(draw, x, y, w, h, color):
    cx = x + w // 2
    cy = y + h // 2
    body_x = cx + 10
    rx, ry = 18, 13
    draw.ellipse([body_x - rx, cy - ry, body_x + rx, cy + ry], fill=color, outline=TFT_WHITE)
    draw.ellipse([body_x - 4, cy - 5, body_x + 6, cy + 5], fill=TFT_BLACK) # Sound hole
    
    neck_w, neck_h = 25, 6
    neck_x = (body_x - rx) - neck_w + 2
    draw.rectangle([neck_x, cy - neck_h//2, neck_x + neck_w, cy + neck_h//2], fill=(139, 69, 19)) # Brown
    
    head_w, head_h = 10, 12
    head_x = neck_x - head_w
    draw.rectangle([head_x, cy - head_h//2, head_x + head_w, cy + head_h//2], fill=color, outline=TFT_WHITE)

def generate_main_screen():
    img = create_blank_screen()
    draw = ImageDraw.Draw(img)
    
    # Title (Hidden by BPM in code, but let's draw BPM area)
    # BPM Area
    draw.text((260, 35), "BPM", fill=TFT_CYAN)
    font_large = ImageFont.load_default() # Should be larger
    draw.text((220, 25), "120", fill=TFT_CYAN, anchor="mm", font_size=30) # Simulated large font
    
    # Idle Mandolin
    draw_mandolin(draw, 290, 5, 20, 40, TFT_BLACK)

    # Buttons (Filled)
    draw_button(draw, 5, 5, 70, 40, "4/4", TFT_PURPLE, filled=True)
    draw_button(draw, 80, 5, 70, 40, "TUNE", TFT_PURPLE, filled=True)
    
    draw_button(draw, 5, 55, 70, 60, "-10", TFT_BLUE, filled=True)
    draw_button(draw, 80, 55, 70, 60, "-1", TFT_NAVY, filled=True)
    draw_button(draw, 170, 55, 70, 60, "+1", TFT_NAVY, filled=True)
    draw_button(draw, 245, 55, 70, 60, "+10", TFT_BLUE, filled=True)
    
    # Start Button (Custom)
    x, y, w, h = 5, 125, 145, 60
    draw_rounded_rect(draw, (x, y, w, h), 8, fill=TFT_DARKGREEN, outline=TFT_WHITE)
    draw_mandolin(draw, x + 20, y, 50, h, TFT_ORANGE)
    draw.text((x + w/2 + 37, y + h/2), "START", fill=TFT_WHITE, anchor="mm")
    
    draw_button(draw, 170, 125, 70, 60, "PROG", TFT_NAVY, filled=True)
    draw_button(draw, 245, 125, 70, 60, "SND", TFT_MAROON, filled=True)
    
    draw_button(draw, 5, 195, 60, 40, "-", TFT_DARKGREY, filled=True)
    draw_button(draw, 255, 195, 60, 40, "+", TFT_DARKGREY, filled=True)
    
    # Volume Bar
    draw.rectangle([75, 205, 75+170, 205+20], outline=TFT_WHITE)
    fill_w = int(170 * 0.5) # 50% volume
    draw.rectangle([76, 206, 76+fill_w, 206+18], fill=TFT_YELLOW)
    
    return img

def generate_tuner_screen():
    img = create_blank_screen()
    draw = ImageDraw.Draw(img)
    
    draw.text((160, 10), "Mandolin Tuner", fill=TFT_WHITE, anchor="mt")
    
    # Strings
    draw_button(draw, 20, 50, 60, 60, "G", TFT_BLUE, filled=False)
    draw_button(draw, 95, 50, 60, 60, "D", TFT_BLUE, filled=False)
    draw_button(draw, 170, 50, 60, 60, "A", TFT_BLUE, filled=False)
    draw_button(draw, 245, 50, 60, 60, "E", TFT_BLUE, filled=False)
    
    # Stop
    draw_button(draw, 110, 130, 100, 50, "STOP", TFT_RED, filled=False)
    
    # Back
    draw_button(draw, 10, 200, 80, 35, "BACK", TFT_DARKGREY, filled=False)
    
    return img

def generate_sound_select():
    img = create_blank_screen()
    draw = ImageDraw.Draw(img)
    
    # Tabs
    draw_rounded_rect(draw, (10, 5, 145, 30), 5, fill=TFT_GREEN)
    draw.text((10 + 145/2, 5 + 30/2), "Downbeat", fill=TFT_WHITE, anchor="mm")
    
    draw_rounded_rect(draw, (165, 5, 145, 30), 5, fill=TFT_DARKGREY)
    draw.text((165 + 145/2, 5 + 30/2), "Upbeat", fill=TFT_WHITE, anchor="mm")
    
    # List
    draw.rectangle([10, 40, 250, 180], outline=TFT_WHITE)
    items = ["Metro", "Digital", "Rimshot", "Clave", "Cowbell"]
    y = 45
    for i, item in enumerate(items):
        color = TFT_YELLOW if i == 0 else TFT_WHITE
        draw.text((25, y), item, fill=color)
        y += 28
        
    # Scroll
    draw_button(draw, 260, 40, 50, 65, "/\\", TFT_DARKGREY, filled=False)
    draw_button(draw, 260, 115, 50, 65, "\\/", TFT_DARKGREY, filled=False)
    
    # Controls (Outlined)
    draw_button(draw, 10, 190, 100, 35, "BACK", TFT_BLUE, filled=False)
    draw_button(draw, 210, 190, 100, 35, "SELECT", TFT_GREEN, filled=False)
    
    return img

def generate_program_select():
    img = create_blank_screen()
    draw = ImageDraw.Draw(img)
    
    draw.text((10, 5), "Select Program", fill=TFT_WHITE)
    
    # List
    draw.rectangle([10, 30, 250, 170], outline=TFT_WHITE)
    items = ["MandoRock", "MandoJazz", "MandoBlues", "MandoPop", "MandoMetal"]
    y = 35
    for i, item in enumerate(items):
        color = TFT_YELLOW if i == 2 else TFT_WHITE
        draw.text((25, y), item, fill=color)
        y += 28
        
    # Scroll
    draw_button(draw, 260, 30, 50, 65, "/\\", TFT_DARKGREY, filled=False)
    draw_button(draw, 260, 105, 50, 65, "\\/", TFT_DARKGREY, filled=False)
    
    # Controls (Outlined)
    y_base = 185
    draw_button(draw, 10, y_base, 60, 35, "BACK", TFT_BLUE, filled=False)
    draw_button(draw, 80, y_base, 60, 35, "NEW", TFT_GREEN, filled=False)
    draw_button(draw, 150, y_base, 60, 35, "EDIT", TFT_NAVY, filled=False)
    draw_button(draw, 220, y_base, 60, 35, "PLAY", TFT_DARKGREEN, filled=False)
    draw_button(draw, 290, y_base, 25, 35, "X", TFT_RED, filled=False)
    
    return img

def generate_editor():
    img = create_blank_screen()
    draw = ImageDraw.Draw(img)
    
    draw.text((10, 5), "MandoBlues", fill=TFT_WHITE)
    
    # List
    y = 35
    steps = [
        (4, 4, 120),
        (4, 4, 120),
        (2, 4, 120),
        (4, 4, 120)
    ]
    
    for i, step in enumerate(steps):
        bars, sig, bpm = step
        bg = TFT_BLUE if i == 1 else TFT_BLACK
        if bg != TFT_BLACK:
            draw.rectangle([10, y-2, 210, y+28], fill=bg)
        
        text = f"{i+1}. {bars}x {sig}/4 {bpm}"
        draw.text((20, y), text, fill=TFT_WHITE)
        y += 32
        
    # Controls (Outlined)
    y_base = 200
    draw_button(draw, 10, y_base, 50, 35, "ADD", TFT_GREEN, filled=False)
    draw_button(draw, 65, y_base, 50, 35, "DEL", TFT_RED, filled=False)
    draw_button(draw, 120, y_base, 50, 35, "RET", TFT_BLUE, filled=False)
    draw_button(draw, 175, y_base, 60, 35, "LOOP", TFT_CYAN, filled=False)
    draw_button(draw, 240, y_base, 70, 35, "SAVE", TFT_ORANGE, filled=False)
    
    # Edit Controls (Right side)
    x_base = 220
    y_start = 40
    
    # Bars
    draw.text((x_base + 40, y_start), "Bars", fill=TFT_WHITE)
    draw_rounded_rect(draw, (x_base, y_start+10, 30, 30), 3, outline=TFT_WHITE)
    draw.text((x_base+15, y_start+25), "-", fill=TFT_WHITE, anchor="mm")
    draw_rounded_rect(draw, (x_base+50, y_start+10, 30, 30), 3, outline=TFT_WHITE)
    draw.text((x_base+65, y_start+25), "+", fill=TFT_WHITE, anchor="mm")
    
    # Sig
    y_start += 50
    draw.text((x_base + 40, y_start), "Sig", fill=TFT_WHITE)
    draw_rounded_rect(draw, (x_base, y_start+10, 30, 30), 3, outline=TFT_WHITE)
    draw.text((x_base+15, y_start+25), "-", fill=TFT_WHITE, anchor="mm")
    draw_rounded_rect(draw, (x_base+50, y_start+10, 30, 30), 3, outline=TFT_WHITE)
    draw.text((x_base+65, y_start+25), "+", fill=TFT_WHITE, anchor="mm")
    
    # BPM (Added)
    y_start += 50
    draw.text((x_base + 40, y_start), "BPM", fill=TFT_WHITE)
    draw_rounded_rect(draw, (x_base, y_start+10, 30, 30), 3, outline=TFT_WHITE)
    draw.text((x_base+15, y_start+25), "-", fill=TFT_WHITE, anchor="mm")
    draw_rounded_rect(draw, (x_base+50, y_start+10, 30, 30), 3, outline=TFT_WHITE)
    draw.text((x_base+65, y_start+25), "+", fill=TFT_WHITE, anchor="mm")
    
    return img

if __name__ == "__main__":
    output_dir = os.path.join(os.path.dirname(__file__), "../images")
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    generate_main_screen().save(os.path.join(output_dir, "ui_main.png"))
    generate_sound_select().save(os.path.join(output_dir, "ui_sound.png"))
    generate_program_select().save(os.path.join(output_dir, "ui_program.png"))
    generate_editor().save(os.path.join(output_dir, "ui_editor.png"))
    generate_tuner_screen().save(os.path.join(output_dir, "ui_tuner.png"))
    print("Mockups generated in images/")
