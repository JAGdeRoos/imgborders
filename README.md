As seen in 𝕽𝖎𝖛𝖊𝖓𝖉𝖊𝖑𝖑, the winning submission to Hyprland's 4th ricing competition: https://codeberg.org/zacoons/rivendell-hyprdots

# Installation

Build from source:

```
% git clone https://codeberg.org/zacoons/imgborders
% cmake -B build
% cmake --build build
```

Then add it to Hyprland:

```
% hyprctl plugin load /home/you/code/imgborders/build/imgborders.so
```

To remove it just replace `load` with `unload` in the above.

# Config'ing

## Basic Configuration

```
plugin:imgborders {
    enabled = true
    image = ~/path/to/file
    sizes = 1,2,3,4 # left, right, top, bottom
    insets = 1,2,3,4 # left, right, top, bottom
    scale = 1.2
    smooth = true
    blur = false
}
```

I don't think I need to explain `enabled` or `image`.

`sizes` - (4 integers) Defines the number of pixels from each edge of the image to take.

`insets` - (4 integers) Defines the amount by which to inset each side into the window.

`scale` - Scale the borders by some amount.

`smooth` - Whether the image pixels should have smoothing (true) or if it should be pixelated (false).

`blur` - Whether transparency should have blur (true) or if it should be clear (false).

## Enhanced 7-Section Border System

For advanced border customization, you can now split each edge into 7 sections instead of the basic single tiling edge:

```
plugin:imgborders {
    # ... basic config above ...
    
    # Section sizes (pixels from source image)
    horsizes = 10,20,15,25      # horizontal sections: left-left, left-right, right-left, right-right
    versizes = 12,18,18,12      # vertical sections: top-top, top-bottom, bottom-top, bottom-bottom
    
    # Custom section placements (percentages 0-100)
    horplacements = 25,75       # horizontal custom positions: left-custom%, right-custom%
    verplacements = 30,70       # vertical custom positions: top-custom%, bottom-custom%
}
```

### How It Works

Each edge is divided into **7 sections**:
- **Corner** (static)
- **Edge section 1** (tiling) 
- **Custom section 1** (static, positioned)
- **Middle edge** (tiling)
- **Custom section 2** (static, positioned) 
- **Edge section 2** (tiling)
- **Corner** (static)

### Section Parameters

**`horsizes`** - (4 integers) Pixel widths from source image for horizontal edge sections:
- `[0]` = left edge section 1 width
- `[1]` = left custom section width  
- `[2]` = right custom section width
- `[3]` = right edge section 2 width

**`versizes`** - (4 integers) Pixel heights from source image for vertical edge sections:
- `[0]` = top edge section 1 height
- `[1]` = top custom section height
- `[2]` = bottom custom section height  
- `[3]` = bottom edge section 2 height

**`horplacements`** - (2 integers) Percentage positions (0-100) for custom sections on horizontal edges:
- `[0]` = position of left custom section
- `[1]` = position of right custom section

**`verplacements`** - (2 integers) Percentage positions (0-100) for custom sections on vertical edges:
- `[0]` = position of top custom section  
- `[1]` = position of bottom custom section

### Creative Examples

**Centered decorations:**
```
horplacements = 45,55  # customs close to center
verplacements = 45,55  # creates large outer tiling sections
```

**Wide spread:**
```
horplacements = 20,80  # customs far apart  
verplacements = 25,75  # creates large middle tiling section
```

**Asymmetric design:**
```
horplacements = 15,90  # uneven positioning
verplacements = 10,60  # for unique layouts
```

The **3 tiling sections** per edge automatically adjust their sizes based on custom positions, seamlessly filling the space between corners and custom elements with your tiling textures.

## Window rules

`plugin:imgborders:noimgborders` - Disables image borders.

## Advanced Border Design Tips

1. **Source Image Layout**: Design your source image with the enhanced system in mind. Place different textures in the areas that will be sliced for each section type.

2. **Tiling Textures**: The 3 tiling sections per edge (`tle`/`tme`/`tre`, `rte`/`rme`/`rbe`, etc.) should contain seamlessly tileable patterns.

3. **Custom Elements**: The 2 custom sections per edge (`tlc`/`trc`, `rtc`/`rbc`, etc.) can contain logos, decorative elements, or special graphics that will be positioned exactly where you specify.

4. **Placement Strategy**: 
   - Use close placements (45%, 55%) for prominent outer tiling sections
   - Use wide placements (20%, 80%) for a dominant middle tiling section
   - Use asymmetric placements for unique, non-uniform designs

5. **Section Sizing**: Balance your `horsizes`/`versizes` with your `horplacements`/`verplacements` to ensure custom sections don't overlap and tiling sections have adequate space.
